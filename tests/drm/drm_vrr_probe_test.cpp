#include "tools/drm_vrr_probe.hpp"

#include "backends/drm/fake_drm_api.hpp"
#include "backends/drm/fake_kms_api.hpp"
#include "tests/helpers/fake_kms.hpp"
#include "tests/helpers/test_support.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <cstdlib>

namespace {
using namespace glasswyrm;
using namespace glasswyrm::drm;
using namespace glasswyrm::tools;

class TemporaryDirectory {
 public:
  TemporaryDirectory() {
    std::string pattern = "/tmp/glasswyrm-vrr-probe-XXXXXX";
    gw::test::require(::mkdtemp(pattern.data()) != nullptr,
                      "create VRR probe temporary directory");
    path_ = pattern;
  }
  ~TemporaryDirectory() { std::filesystem::remove_all(path_); }
  std::filesystem::path file(std::string_view name) const { return path_ / name; }
 private:
  std::filesystem::path path_;
};

DeviceSnapshot snapshot() {
  DeviceSnapshot value;
  value.canonical_path = "/dev/dri/card0";
  value.primary_node = value.dumb_buffer = value.universal_planes = true;
  value.atomic = value.timestamp_monotonic = true;
  value.driver.name = "fake-nvidia";
  value.crtcs.push_back({40, 0, {10}});
  Mode mode{"2x2", 2, 2, 120'000, 25'000, true};
  mode.hsync_start = 3; mode.hsync_end = 4; mode.htotal = 5;
  mode.vsync_start = 3; mode.vsync_end = 4; mode.vtotal = 5;
  mode.vrefresh_hz = 120;
  Connector connector;
  connector.id = 10;
  connector.type = static_cast<std::uint32_t>(ConnectorType::DisplayPort);
  connector.type_id = 1;
  connector.status = ConnectionStatus::Connected;
  connector.modes.push_back(mode);
  connector.possible_crtc_mask = 1;
  connector.current_crtc_id = 40;
  connector.vrr_property_present = connector.vrr_capable = true;
  value.connectors.push_back(connector);
  value.planes.push_back({50, PlaneType::Primary, 1, {kFormatXrgb8888}, 40});
  return value;
}

void configure(FakeKmsApi& api) {
  api.master = true;
  api.dumb_allocation = {7, 8, 16};
  api.connector_crtcs[10] = 40;
  KmsMode mode;
  mode.hdisplay = mode.vdisplay = 2;
  mode.name = "2x2";
  api.crtcs[40] = {40, 60, 0, 0, true, mode};
  api.planes[50] = {50, 60, 40, 0, 0, 2, 2, 0, 0,
                    2U << 16U, 2U << 16U};
  api.properties[{KmsObjectType::Connector, 10}] =
      gw::test::kms_properties({"CRTC_ID"}, 10);
  auto crtc = gw::test::kms_properties({"MODE_ID", "ACTIVE"}, 20);
  crtc.push_back({22, "VRR_ENABLED", 0, 1, PropertyValueRange{0, 1}});
  api.properties[{KmsObjectType::Crtc, 40}] = std::move(crtc);
  api.properties[{KmsObjectType::Plane, 50}] = gw::test::kms_properties(
      {"FB_ID", "CRTC_ID", "SRC_X", "SRC_Y", "SRC_W", "SRC_H",
       "CRTC_X", "CRTC_Y", "CRTC_W", "CRTC_H"}, 30);
}

class Platform final : public DrmVrrProbePlatform {
 public:
  explicit Platform(FakeDrmApi& api, FakeKmsApi* kms = nullptr)
      : api_(api), kms_(kms) {}
  bool monotonic_nanoseconds(std::uint64_t& value, std::string&) override {
    value = now_ += 1'000;
    return true;
  }
  bool sleep_until(std::uint64_t deadline, std::string&) override {
    now_ = deadline;
    return true;
  }
  void submitted(std::uint64_t token, std::uint32_t crtc, bool enabled,
                 std::uint32_t) override {
    if (!queue_completions_) return;
    timestamp_ += enabled ? 14'286'000 : 8'333'000;
    api_.queue_page_flip(token, wrong_crtc_ ? crtc + 1 : crtc, 0,
                         timestamp_, timestamps_);
    if (fail_next_submission_ && submissions_++ == 0 && kms_)
      kms_->fail_next(KmsOperation::AtomicCommit);
  }
  bool timestamps_{true};
  bool wrong_crtc_{};
  bool fail_next_submission_{};
  bool queue_completions_{true};
 private:
  FakeDrmApi& api_;
  FakeKmsApi* kms_{};
  std::uint32_t submissions_{};
  std::uint64_t now_{1'000'000'000};
  std::uint64_t timestamp_{2'000'000'000};
};

DrmVrrProbeOptions options(const std::filesystem::path& output) {
  return {"/dev/dri/card0", "DP-1", "2x2@120000",
          std::string(32, '1'), output.string(), 70, 1, 2, 100};
}

std::string read(const std::filesystem::path& path) {
  std::ifstream input(path);
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

void successful_probe(const TemporaryDirectory& directory) {
  FakeDrmApi drm({"/dev/dri/card0", DeviceOpenStatus::Success, snapshot(), {}});
  FakeKmsApi kms;
  configure(kms);
  Platform platform(drm);
  std::ostringstream error;
  const auto output = directory.file("success.jsonl");
  gw::test::require(run_drm_vrr_probe(drm, kms, platform, options(output),
                                      error) == 0 && error.str().empty(),
                    "fake NVIDIA VRR probe completes");
  const auto report = read(output);
  gw::test::require(
      report.find("\"record\":\"probe-start\"") != std::string::npos &&
          report.find("\"phase\":\"off\"") != std::string::npos &&
          report.find("\"phase\":\"on\"") != std::string::npos &&
          report.find("\"phase\":\"restore-off\"") != std::string::npos &&
          report.find("\"raw_event_sequence\":0") != std::string::npos &&
          report.find("\"record\":\"restore\"") != std::string::npos &&
          report.find("\"passed\":true") != std::string::npos,
      "probe report preserves phases, zero sequences, and restoration");
  gw::test::require(kms.atomic_commits.size() == 11,
                    "probe issues two tests, one modeset, seven flips, and restore");
  std::vector<std::uint64_t> framebuffers;
  for (const auto& commit : kms.atomic_commits) {
    if ((commit.flags & AtomicPageFlipEvent) == 0) continue;
    for (const auto& property : commit.properties)
      if (property.object_id == 50 && property.property_id == 30)
        framebuffers.push_back(property.value);
  }
  gw::test::require(framebuffers.size() == 7 &&
                        std::ranges::adjacent_find(framebuffers) ==
                            framebuffers.end(),
                    "every probe completion alternates framebuffer IDs");
}

void rejected_controllability_restores(const TemporaryDirectory& directory) {
  FakeDrmApi drm({"/dev/dri/card0", DeviceOpenStatus::Success, snapshot(), {}});
  FakeKmsApi kms;
  configure(kms);
  kms.rejected_test_property = std::pair{22U, UINT64_C(1)};
  Platform platform(drm);
  std::ostringstream error;
  const auto output = directory.file("test-on-rejected.jsonl");
  gw::test::require(run_drm_vrr_probe(drm, kms, platform, options(output),
                                      error) == 1,
                    "TEST_ONLY on rejection stops before modesetting");
  const auto report = read(output);
  gw::test::require(report.find("\"atomic_test_on\":false") !=
                            std::string::npos &&
                        report.find("\"record\":\"restore\"") !=
                            std::string::npos &&
                        report.find("\"passed\":true") != std::string::npos,
                    "controllability failure still records exact restoration");
}

void degraded_and_failed_runs_restore(const TemporaryDirectory& directory) {
  {
    FakeDrmApi drm(
        {"/dev/dri/card0", DeviceOpenStatus::Success, snapshot(), {}});
    FakeKmsApi kms;
    configure(kms);
    Platform platform(drm);
    platform.timestamps_ = false;
    std::ostringstream error;
    const auto output = directory.file("timing-unavailable.jsonl");
    gw::test::require(run_drm_vrr_probe(
                          drm, kms, platform, options(output), error) == 0 &&
                          read(output).find(
                              "\"raw_timestamp_available\":false") !=
                              std::string::npos,
                      "missing raw timing degrades evidence but completes flips");
  }
  {
    FakeDrmApi drm(
        {"/dev/dri/card0", DeviceOpenStatus::Success, snapshot(), {}});
    FakeKmsApi kms;
    configure(kms);
    Platform platform(drm, &kms);
    platform.fail_next_submission_ = true;
    std::ostringstream error;
    const auto output = directory.file("atomic-failed.jsonl");
    gw::test::require(run_drm_vrr_probe(
                          drm, kms, platform, options(output), error) == 1 &&
                          read(output).find("\"atomic_status\":\"failed\"") !=
                              std::string::npos &&
                          read(output).find("\"record\":\"restore\"") !=
                              std::string::npos,
                      "atomic submission failure is recorded before restore");
  }
  {
    FakeDrmApi drm(
        {"/dev/dri/card0", DeviceOpenStatus::Success, snapshot(), {}});
    FakeKmsApi kms;
    configure(kms);
    Platform platform(drm);
    platform.wrong_crtc_ = true;
    std::ostringstream error;
    const auto output = directory.file("wrong-crtc.jsonl");
    gw::test::require(run_drm_vrr_probe(
                          drm, kms, platform, options(output), error) == 1 &&
                          error.str().find("identity diverged") !=
                              std::string::npos &&
                          read(output).find("\"record\":\"restore\"") !=
                              std::string::npos,
                      "wrong-CRTC completion fails after exact restore");
  }
}

void unresolved_flip_skips_unsafe_rollback(
    const TemporaryDirectory& directory) {
  FakeDrmApi drm({"/dev/dri/card0", DeviceOpenStatus::Success, snapshot(), {}});
  FakeKmsApi kms;
  configure(kms);
  kms.master = false;
  Platform platform(drm);
  platform.queue_completions_ = false;
  auto probe_options = options(directory.file("flip-timeout.jsonl"));
  probe_options.event_timeout_milliseconds = 1;
  std::ostringstream error;

  gw::test::require(
      run_drm_vrr_probe(drm, kms, platform, probe_options, error) == 1 &&
          error.str().find("page-flip event timed out") != std::string::npos,
      "unresolved fake page flip fails the bounded probe");
  const auto report = read(probe_options.output_path);
  gw::test::require(
      report.find("\"record\":\"restore\"") != std::string::npos &&
          report.find("\"kms_state_equal\":false") != std::string::npos &&
          report.find("page-flip completion was not observed") !=
              std::string::npos,
      "unresolved flip records that saved-state restoration was unsafe");
  gw::test::require(
      kms.atomic_commits.size() == 4 &&
          std::ranges::find(kms.calls, "set_master") != kms.calls.end() &&
          std::ranges::none_of(kms.calls, [](const std::string& call) {
            return call == "drop_master" || call.starts_with("rmfb:") ||
                   call.starts_with("unmap:") ||
                   call.starts_with("destroy_dumb:") ||
                   call.starts_with("destroy_blob:");
          }) &&
          drm.close_count() == 1,
      "unresolved flip closes the DRM fd without racing rollback or resources");
}

void parser_contract() {
  DrmVrrProbeOptions parsed;
  std::ostringstream output, error;
  std::vector<std::string> arguments{
      "gw_drm_vrr_probe", "--device", "/dev/dri/card0", "--connector",
      "DP-1", "--mode", "2x2@120000", "--run-id", std::string(32, 'a'),
      "--output", "/tmp/new.jsonl", "--target-hz", "70", "--warmup", "10",
      "--samples", "140"};
  std::vector<char*> argv;
  for (auto& argument : arguments) argv.push_back(argument.data());
  gw::test::require(parse_drm_vrr_probe_options(
                        static_cast<int>(argv.size()), argv.data(), parsed,
                        output, error) == DrmVrrProbeParseResult::Run &&
                        parsed.recorded_flips == 140 && error.str().empty(),
                    "probe parser accepts the reviewed bounded profile");
}

}  // namespace

int main() {
  TemporaryDirectory directory;
  parser_contract();
  successful_probe(directory);
  rejected_controllability_restores(directory);
  degraded_and_failed_runs_restore(directory);
  unresolved_flip_skips_unsafe_rollback(directory);
  return 0;
}
