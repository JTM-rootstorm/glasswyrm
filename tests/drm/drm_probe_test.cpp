#include "tools/drm_probe.hpp"

#include "backends/drm/fake_drm_api.hpp"
#include "tests/helpers/test_support.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <cstdlib>

namespace {

using glasswyrm::drm::ConnectionStatus;
using glasswyrm::drm::Connector;
using glasswyrm::drm::ConnectorType;
using glasswyrm::drm::Crtc;
using glasswyrm::drm::DeviceOpenStatus;
using glasswyrm::drm::DeviceSnapshot;
using glasswyrm::drm::FakeDrmApi;
using glasswyrm::drm::Mode;
using glasswyrm::drm::Plane;
using glasswyrm::drm::PlaneType;
using glasswyrm::drm::kFormatXrgb8888;
using glasswyrm::tools::DrmProbeOptions;
using glasswyrm::tools::DrmProbeParseResult;

constexpr std::string_view kExpectedJson = R"json({
  "device":{"path":"/dev/dri/card0","major":226,"minor":0},
  "driver":{"name":"virtio_\"gpu","version":"1.2.3","date":"2024\n01","description":"Virtio GPU","bus_info":"pci:0000:00:02.0"},
  "capabilities":{"dumb_buffer":true,"universal_planes":true,"atomic":true},
  "connectors":[{"id":11,"name":"HDMI-A-2","status":"disconnected","non_desktop":false,"possible_crtc_mask":2,"current_crtc_id":0,"modes":[]},{"id":10,"name":"Virtual-1","status":"connected","non_desktop":false,"possible_crtc_mask":1,"current_crtc_id":40,"modes":[{"name":"1024x768","width":1024,"height":768,"refresh_millihz":60000,"clock_khz":65000,"preferred":true},{"name":"1280x720","width":1280,"height":720,"refresh_millihz":60000,"clock_khz":74250,"preferred":false}]}],
  "crtcs":[{"id":40,"index":0,"current_mode":{"name":"1024x768","width":1024,"height":768,"refresh_millihz":60000,"clock_khz":65000},"current_framebuffer_id":81,"x":0,"y":0,"active":true,"connector_ids":[10]},{"id":60,"index":1,"current_mode":null,"current_framebuffer_id":0,"x":0,"y":0,"active":false,"connector_ids":[11]}],
  "planes":[{"id":50,"type":"primary","possible_crtc_mask":1,"current_crtc_id":40,"framebuffer_id":81,"crtc_x":0,"crtc_y":0,"crtc_width":1024,"crtc_height":768,"source_x":0,"source_y":0,"source_width":67108864,"source_height":50331648,"formats":[842094158,875713112]},{"id":70,"type":"cursor","possible_crtc_mask":2,"current_crtc_id":0,"framebuffer_id":0,"crtc_x":0,"crtc_y":0,"crtc_width":0,"crtc_height":0,"source_x":0,"source_y":0,"source_width":0,"source_height":0,"formats":[875713112]}],
  "selected_candidate":{"connector":"Virtual-1","connector_id":10,"mode":"1024x768","width":1024,"height":768,"refresh_millihz":60000,"crtc_id":40,"plane_id":50}
}
)json";

class TemporaryDirectory {
 public:
  TemporaryDirectory() {
    char pattern[] = "/tmp/glasswyrm-drm-probe-XXXXXX";
    gw::test::require(::mkdtemp(pattern) != nullptr,
                      "create DRM probe temporary directory");
    path_ = pattern;
  }

  ~TemporaryDirectory() { std::filesystem::remove_all(path_); }

  TemporaryDirectory(const TemporaryDirectory&) = delete;
  TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

  [[nodiscard]] std::filesystem::path file(const std::string_view name) const {
    return path_ / name;
  }

 private:
  std::filesystem::path path_;
};

DeviceSnapshot valid_snapshot() {
  DeviceSnapshot snapshot;
  snapshot.canonical_path = "/dev/dri/card0";
  snapshot.device_major = 226;
  snapshot.device_minor = 0;
  snapshot.driver = {"virtio_\"gpu", "2024\n01", "Virtio GPU",
                     "pci:0000:00:02.0", 1, 2, 3};
  snapshot.primary_node = true;
  snapshot.dumb_buffer = true;
  snapshot.universal_planes = true;
  snapshot.atomic = true;

  Connector selected;
  selected.id = 10;
  selected.type = static_cast<std::uint32_t>(ConnectorType::Virtual);
  selected.type_id = 1;
  selected.status = ConnectionStatus::Connected;
  selected.modes = {
      Mode{"1280x720", 1280, 720, 60'000, 74'250, false},
      Mode{"1024x768", 1024, 768, 60'000, 65'000, true},
  };
  selected.possible_crtc_mask = 1;
  selected.current_crtc_id = 40;

  Connector disconnected;
  disconnected.id = 11;
  disconnected.type = static_cast<std::uint32_t>(ConnectorType::HdmiA);
  disconnected.type_id = 2;
  disconnected.status = ConnectionStatus::Disconnected;
  disconnected.possible_crtc_mask = 2;
  snapshot.connectors = {std::move(selected), std::move(disconnected)};
  const Mode active_mode{"1024x768", 1024, 768, 60'000, 65'000, true};
  snapshot.crtcs = {Crtc{60, 1, {11}},
                    Crtc{40, 0, {10}, 81, 0, 0, true, active_mode}};
  snapshot.planes = {
      Plane{70, PlaneType::Cursor, 2, {kFormatXrgb8888}, 0},
      Plane{50, PlaneType::Primary, 1,
            {kFormatXrgb8888, glasswyrm::drm::fourcc('N', 'V', '1', '2')},
            40, 81, 0, 0, 1024, 768, 0, 0, 1024U << 16U, 768U << 16U},
  };
  return snapshot;
}

FakeDrmApi fake_api(DeviceSnapshot snapshot = valid_snapshot()) {
  return FakeDrmApi({"/dev/dri/card0", DeviceOpenStatus::Success,
                     std::move(snapshot), {}});
}

DrmProbeOptions explicit_options(const std::filesystem::path& output) {
  DrmProbeOptions options;
  options.device = "/dev/dri/card0";
  options.connector = "Virtual-1";
  options.required_width = 1024;
  options.required_height = 768;
  options.output_path = output.string();
  return options;
}

std::string read_file(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  gw::test::require(static_cast<bool>(input), "open DRM probe output");
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

DrmProbeParseResult parse(const std::vector<std::string>& arguments,
                          DrmProbeOptions& options, std::string& output,
                          std::string& error) {
  std::vector<char*> argv;
  argv.reserve(arguments.size());
  for (const auto& argument : arguments)
    argv.push_back(const_cast<char*>(argument.c_str()));
  std::ostringstream output_stream;
  std::ostringstream error_stream;
  const auto result = glasswyrm::tools::parse_drm_probe_options(
      static_cast<int>(argv.size()), argv.data(), options, output_stream,
      error_stream);
  output = output_stream.str();
  error = error_stream.str();
  return result;
}

void require_parse_failure(const std::vector<std::string>& arguments,
                           const std::string_view expected_error) {
  DrmProbeOptions options;
  std::string output;
  std::string error;
  gw::test::require(parse(arguments, options, output, error) ==
                        DrmProbeParseResult::ExitFailure &&
                    error.find(expected_error) != std::string::npos,
                    expected_error);
}

void test_parser() {
  DrmProbeOptions options;
  std::string output;
  std::string error;
  const std::vector<std::string> valid{
      "gw_drm_probe",   "--device",       "/dev/dri/card0",
      "--connector",    "Virtual-1",      "--require-mode",
      "1024x768",       "--snapshot-state", "--expect-active",
      "--output",       "/tmp/probe.json"};
  gw::test::require(
      parse(valid, options, output, error) == DrmProbeParseResult::Run &&
          options.device == "/dev/dri/card0" &&
          options.connector == "Virtual-1" &&
          options.required_width == 1024 && options.required_height == 768 &&
          options.snapshot_state && options.expect_active &&
          options.output_path == "/tmp/probe.json" && error.empty(),
      "parse explicit probe state-validation command");

  const std::vector<std::string> restored{
      "gw_drm_probe",      "--device",        "/dev/dri/card0",
      "--connector",       "Virtual-1",       "--require-mode",
      "1024x768",          "--expect-restored", "/tmp/baseline.json",
      "--output",          "/tmp/restored.json"};
  gw::test::require(
      parse(restored, options, output, error) == DrmProbeParseResult::Run &&
          options.expected_restored_path == "/tmp/baseline.json" &&
          options.output_path == "/tmp/restored.json" && error.empty(),
      "parse explicit restoration comparison command");

  gw::test::require(parse({"gw_drm_probe", "--help"}, options, output,
                          error) == DrmProbeParseResult::ExitSuccess &&
                        output.find("Usage: gw_drm_probe") != std::string::npos,
                    "parse help command");
  require_parse_failure({"gw_drm_probe", "--device", "/dev/dri/card0"},
                        "--output is required");
  require_parse_failure({"gw_drm_probe", "--output"},
                        "--output requires a value");
  require_parse_failure(
      {"gw_drm_probe", "--output", "--list"},
      "--output requires a value");
  require_parse_failure({"gw_drm_probe", "--require-mode", "1024X768",
                         "--output", "/tmp/probe.json"},
                        "requires WIDTHxHEIGHT");
  require_parse_failure({"gw_drm_probe", "--require-mode", "0x768",
                         "--output", "/tmp/probe.json"},
                        "requires WIDTHxHEIGHT");
  require_parse_failure({"gw_drm_probe", "--output", "/tmp/a",
                         "--output", "/tmp/b"},
                        "duplicate option");
  require_parse_failure({"gw_drm_probe", "--mystery", "--output",
                         "/tmp/probe.json"},
                        "unknown option");
  require_parse_failure({"gw_drm_probe", "--device", "auto",
                         "--snapshot-state", "--output", "/tmp/probe.json"},
                        "state validation requires explicit");
  require_parse_failure({"gw_drm_probe", "--device", "/dev/dri/card0",
                         "--connector", "Virtual-1", "--expect-restored",
                         "/tmp/base.json", "--output", "/tmp/probe.json"},
                        "state validation requires explicit");
}

void test_deterministic_snapshot(const TemporaryDirectory& directory) {
  auto api = fake_api();
  auto options = explicit_options(directory.file("snapshot.json"));
  options.snapshot_state = true;
  std::ostringstream error;
  gw::test::require(glasswyrm::tools::run_drm_probe(api, options, error) == 0 &&
                        error.str().empty(),
                    "capture explicit read-only DRM snapshot");
  gw::test::require(read_file(options.output_path) == kExpectedJson,
                    "DRM snapshot JSON is exact and deterministic");
  gw::test::require(api.close_count() == 1 && !api.open(),
                    "DRM probe closes its read-only device");
}

void test_rejections(const TemporaryDirectory& directory) {
  {
    auto api = fake_api();
    auto options = explicit_options(directory.file("connector-reject.json"));
    options.connector = "DP-9";
    std::ostringstream error;
    gw::test::require(
        glasswyrm::tools::run_drm_probe(api, options, error) == 1 &&
            error.str().find("no eligible connector") != std::string::npos,
        "reject unavailable connector");
  }
  {
    auto api = fake_api();
    auto options = explicit_options(directory.file("mode-reject.json"));
    options.required_width = 800;
    options.required_height = 600;
    std::ostringstream error;
    gw::test::require(
        glasswyrm::tools::run_drm_probe(api, options, error) == 1 &&
            error.str().find("required mode") != std::string::npos,
        "reject unavailable exact mode");
  }
  {
    auto snapshot = valid_snapshot();
    snapshot.connectors[0].current_crtc_id = 0;
    snapshot.crtcs[1].connector_ids.clear();
    snapshot.planes[1].current_crtc_id = 0;
    auto api = fake_api(std::move(snapshot));
    auto options = explicit_options(directory.file("inactive.json"));
    options.expect_active = true;
    std::ostringstream error;
    gw::test::require(
        glasswyrm::tools::run_drm_probe(api, options, error) == 1 &&
            error.str().find("expected an active connector route") !=
                std::string::npos,
        "reject inactive connector route");
  }
  {
    auto snapshot = valid_snapshot();
    snapshot.connectors[0].current_crtc_id = 999;
    snapshot.crtcs[1].connector_ids.clear();
    snapshot.planes[1].current_crtc_id = 0;
    auto api = fake_api(std::move(snapshot));
    auto options = explicit_options(directory.file("stale-route.json"));
    options.expect_active = true;
    std::ostringstream error;
    gw::test::require(
        glasswyrm::tools::run_drm_probe(api, options, error) == 1 &&
            error.str().find("expected an active connector route") !=
                std::string::npos,
        "reject a stale current CRTC identifier as inactive");
  }
}

void test_restoration(const TemporaryDirectory& directory) {
  const auto baseline_path = directory.file("baseline.json");
  {
    auto api = fake_api();
    auto options = explicit_options(baseline_path);
    options.snapshot_state = true;
    std::ostringstream error;
    gw::test::require(glasswyrm::tools::run_drm_probe(api, options, error) == 0,
                      "write restoration baseline");
  }
  {
    auto api = fake_api();
    auto options = explicit_options(directory.file("restored.json"));
    options.expected_restored_path = baseline_path.string();
    std::ostringstream error;
    gw::test::require(glasswyrm::tools::run_drm_probe(api, options, error) == 0 &&
                          read_file(options.output_path) == kExpectedJson,
                      "accept exact restored DRM state");
  }
  {
    auto snapshot = valid_snapshot();
    snapshot.crtcs[1].framebuffer_id = 999;
    snapshot.planes[1].framebuffer_id = 999;
    auto api = fake_api(std::move(snapshot));
    auto options = explicit_options(directory.file("mismatch.json"));
    options.expected_restored_path = baseline_path.string();
    std::ostringstream error;
    gw::test::require(
        glasswyrm::tools::run_drm_probe(api, options, error) == 1 &&
            error.str().find("differs from baseline") != std::string::npos &&
            read_file(options.output_path).find(
                "\"current_framebuffer_id\":999") !=
                std::string::npos,
        "report KMS restoration mismatch and retain current snapshot");
  }
  {
    auto api = fake_api();
    auto options = explicit_options(directory.file("missing-baseline.json"));
    options.expected_restored_path =
        directory.file("does-not-exist.json").string();
    std::ostringstream error;
    gw::test::require(
        glasswyrm::tools::run_drm_probe(api, options, error) == 1 &&
            error.str().find("cannot read restoration baseline") !=
                std::string::npos,
        "reject missing restoration baseline");
  }
}

void test_deterministic_auto_scan(const TemporaryDirectory& directory) {
  auto api = fake_api();
  auto options = explicit_options(directory.file("auto.json"));
  options.device = "auto";
  options.auto_device_paths = {
      "/dev/dri/card9", "/dev/dri/card0", "/dev/dri/card0"};
  std::ostringstream error;
  gw::test::require(glasswyrm::tools::run_drm_probe(api, options, error) == 0 &&
                        read_file(options.output_path) == kExpectedJson &&
                        api.close_count() == 2,
                    "auto scan sorts, deduplicates, and reopens one candidate");
}

}  // namespace

int main() {
  TemporaryDirectory directory;
  test_parser();
  test_deterministic_snapshot(directory);
  test_rejections(directory);
  test_restoration(directory);
  test_deterministic_auto_scan(directory);
  return 0;
}
