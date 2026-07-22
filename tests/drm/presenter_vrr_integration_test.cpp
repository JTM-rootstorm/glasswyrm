#include "backends/drm/fake_drm_api.hpp"
#include "backends/drm/fake_kms_api.hpp"
#include "backends/drm/presenter.hpp"
#include "tests/helpers/test_support.hpp"

#include <algorithm>
#include <array>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <poll.h>
#include <string>
#include <unistd.h>
#include <vector>

namespace {
using namespace glasswyrm;
using namespace glasswyrm::drm;

std::filesystem::path temporary_directory() {
  std::string pattern = "/tmp/glasswyrm-presenter-vrr-XXXXXX";
  const auto result = ::mkdtemp(pattern.data());
  gw::test::require(result != nullptr, "create VRR presenter directory");
  return result;
}

DeviceSnapshot snapshot(const bool atomic = true,
                        const bool connector_vrr_capable = true) {
  DeviceSnapshot value;
  value.canonical_path = "/dev/dri/card0";
  value.primary_node = value.dumb_buffer = value.universal_planes = true;
  value.atomic = atomic;
  value.timestamp_monotonic = true;
  value.driver.name = "fake-vrr";
  value.crtcs.push_back({40, 0, {10}});
  Mode mode{"2x2", 2, 2, 60'000, 25'000, true};
  mode.hsync_start = 3;
  mode.hsync_end = 4;
  mode.htotal = 5;
  mode.vsync_start = 3;
  mode.vsync_end = 4;
  mode.vtotal = 5;
  mode.vrefresh_hz = 60;
  Connector connector;
  connector.id = 10;
  connector.type = static_cast<std::uint32_t>(ConnectorType::DisplayPort);
  connector.type_id = 1;
  connector.status = ConnectionStatus::Connected;
  connector.modes.push_back(mode);
  connector.possible_crtc_mask = 1;
  connector.current_crtc_id = 40;
  connector.vrr_property_present = connector_vrr_capable;
  connector.vrr_capable = connector_vrr_capable;
  value.connectors.push_back(connector);
  value.planes.push_back(
      {50, PlaneType::Primary, 1, {kFormatXrgb8888}, 40});
  return value;
}

std::vector<ObjectProperty> properties(
    const std::initializer_list<const char *> names, std::uint32_t id) {
  std::vector<ObjectProperty> result;
  for (const auto *name : names)
    result.push_back({id++, name, 0, 64});
  return result;
}

void configure(FakeKmsApi &api, const bool with_vrr = true) {
  api.dumb_allocation = {7, 8, 16};
  api.connector_crtcs[10] = 40;
  KmsMode mode;
  mode.hdisplay = mode.vdisplay = 2;
  mode.name = "2x2";
  api.crtcs[40] = {40, 60, 0, 0, true, mode};
  api.planes[50] = {50, 60, 40, 0, 0, 2, 2, 0, 0,
                    2U << 16U, 2U << 16U};
  api.properties[{KmsObjectType::Connector, 10}] =
      properties({"CRTC_ID"}, 10);
  auto crtc = properties({"MODE_ID", "ACTIVE"}, 20);
  if (with_vrr)
    crtc.push_back({22, "VRR_ENABLED", 1, 1, PropertyValueRange{0, 1}});
  api.properties[{KmsObjectType::Crtc, 40}] = std::move(crtc);
  api.properties[{KmsObjectType::Plane, 50}] = properties(
      {"FB_ID", "CRTC_ID", "SRC_X", "SRC_Y", "SRC_W", "SRC_H",
       "CRTC_X", "CRTC_Y", "CRTC_W", "CRTC_H"},
      30);
}

class FakeVtApi final : public session::VirtualTerminalApi {
public:
  int open_terminal(std::string_view) override { return 9; }
  bool identify(int, session::DeviceIdentity &value) override {
    value = {4, 2, true};
    return true;
  }
  bool get_state(int, session::VirtualTerminalState &value) override {
    value.active = 1;
    return true;
  }
  bool get_mode(int, session::VirtualTerminalMode &value) override {
    value = {};
    return true;
  }
  bool get_kd_mode(int, int &value) override {
    value = 0;
    return true;
  }
  bool get_keyboard_mode(int, int &value) override {
    value = 3;
    return true;
  }
  bool activate(int, unsigned) override { return true; }
  bool wait_until_active(int, unsigned) override { return true; }
  bool set_process_mode(int, int, int) override { return true; }
  bool set_mode(int, const session::VirtualTerminalMode &) override {
    return true;
  }
  bool set_graphics_mode(int) override { return true; }
  bool set_kd_mode(int, int) override { return true; }
  bool set_keyboard_mode(int, int) override { return true; }
  bool acknowledge_release(int) override { return true; }
  bool acknowledge_acquire(int) override { return true; }
  void close_terminal(int) noexcept override {}
  std::string last_error() const override { return "fake VT failure"; }
};

output::SoftwareFrameSet frame_set(
    const std::span<const std::uint32_t> pixels, const std::uint64_t ordinal,
    const bool desired_enabled,
    const output::vrr::Decision decision = output::vrr::Decision::Enabled) {
  output::SoftwareFrameSet result;
  output::OutputFrameResult item;
  std::string error;
  gw::test::require(item.frame.configure(1, 2, 2, error), error);
  std::ranges::copy(pixels, item.frame.pixels().begin());
  item.output = item.frame.spec(60'000);
  item.logical = {0, 0, 2, 2};
  item.damage = desired_enabled
                    ? std::vector<gw::compositor::Rectangle>{}
                    : std::vector<gw::compositor::Rectangle>{{0, 0, 2, 2}};
  item.vrr.valid = true;
  item.vrr.requested_mode = desired_enabled
                                ? output::vrr::PolicyMode::Fullscreen
                                : output::vrr::PolicyMode::Off;
  item.vrr.decision = decision;
  item.vrr.desired_enabled = desired_enabled;
  item.vrr.candidate_window_id = desired_enabled ? 100U : 0U;
  item.vrr.candidate_surface_id = desired_enabled ? 200U : 0U;
  item.vrr.state_generation = ordinal;
  item.vrr.transition_serial = ordinal;
  item.vrr.target_interval_nanoseconds = 16'666'667;
  const auto request = item.vrr;
  gw::test::require(result.append(std::move(item), error), error);
  gw::test::require(result.finalize(1, 1, ordinal, ordinal, ordinal, error),
                    error);
  gw::test::require(result.set_vrr_requests({{1, request}}, error), error);
  return result;
}

std::string contents(const std::filesystem::path &path) {
  std::ifstream input(path);
  return {std::istreambuf_iterator<char>(input), {}};
}

std::optional<std::uint64_t> property_value(
    const AtomicCommitRecord& commit, const std::uint32_t property_id) {
  const auto found = std::ranges::find_if(
      commit.properties, [&](const AtomicPropertyValue& value) {
        return value.property_id == property_id;
      });
  return found == commit.properties.end()
             ? std::nullopt
             : std::optional<std::uint64_t>{found->value};
}

struct Rig {
  std::filesystem::path directory{temporary_directory()};
  FakeDrmApi drm;
  FakeKmsApi kms;
  FakeVtApi vt;
  DrmReport report{directory / "report.jsonl"};
  std::unique_ptr<DrmPresenter> presenter;
  std::string error;

  explicit Rig(const DrmPresentationApi api = DrmPresentationApi::Atomic,
               const bool atomic = true, const bool reject_vrr_on = false,
               const bool negotiate_vrr = true,
               const bool connector_vrr_capable = true)
      : drm({"/dev/dri/card0", DeviceOpenStatus::Success,
             snapshot(atomic, connector_vrr_capable), {}}) {
    configure(kms);
    if (reject_vrr_on)
      kms.rejected_test_property = std::pair{22U, UINT64_C(1)};
    DeviceDiscovery discovery;
    auto device = Device::open(drm, "/dev/dri/card0", {}, discovery);
    gw::test::require(device.has_value(), discovery.error);
    presenter = std::make_unique<DrmPresenter>(std::move(*device), kms,
                                               nullptr, nullptr, &report);
    DrmPresenterConfig config;
    config.output = {1, 2, 2, 60'000};
    config.connector = "DP-1";
    config.api = api;
    config.tty_path = "/dev/tty2";
    config.vt_signals = {SIGUSR1, SIGUSR2};
    config.vrr_reporting = true;
    gw::test::require(presenter->initialize(config, &vt, error), error);
    if (negotiate_vrr)
      gw::test::require(presenter->configure_vrr_contract(true, error), error);
  }

  ~Rig() {
    presenter.reset();
    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
  }
};

void complete_flip(Rig &rig, const output::PresentResult &pending,
                   const std::uint32_t sequence,
                   const std::uint64_t timestamp) {
  rig.drm.queue_page_flip(pending.token, 40, sequence, timestamp, true);
  const auto event = rig.presenter->service(POLLIN);
  gw::test::require(event.kind == output::BackendEventKind::Complete &&
                        event.vrr_feedback.contains(1) &&
                        rig.presenter->finalize_pending(pending.token,
                                                        rig.error),
                    event.error.empty() ? rig.error : event.error);
}

void atomic_transition_suspend_restore() {
  Rig rig;
  const auto capability = rig.presenter->vrr_capability(1);
  gw::test::require(
      capability && capability->hardware_capable &&
          capability->atomic_kms_available && capability->atomic_test_passed &&
          capability->kms_controllable && rig.kms.atomic_commits.size() == 3 &&
          rig.kms.atomic_commits[0].properties.size() == 13 &&
          rig.kms.atomic_commits[1].properties.back().value == 0 &&
          rig.kms.atomic_commits[2].properties.back().value == 1,
      "initialization proves off/on controllability without changing state");

  const std::array pixels{0xff101010U, 0xff202020U, 0xff303030U,
                          0xff404040U};
  const auto off = frame_set(pixels, 1, false, output::vrr::Decision::Disabled);
  const auto initial = rig.presenter->present(off.view());
  gw::test::require(
      initial.disposition == output::PresentDisposition::Complete &&
          initial.vrr_feedback.at(1).property_readback_valid &&
          !initial.vrr_feedback.at(1).effective_enabled &&
          rig.kms.atomic_commits.back().properties.back().property_id == 22 &&
          rig.kms.atomic_commits.back().properties.back().value == 0,
      "first modeset explicitly commits and reads back VRR off");

  const auto enabled = frame_set(pixels, 2, true);
  auto pending = rig.presenter->present(enabled.view());
  gw::test::require(
      pending.disposition == output::PresentDisposition::Pending &&
          rig.kms.atomic_commits.back().flags ==
              (AtomicNonblock | AtomicPageFlipEvent) &&
          rig.kms.atomic_commits.back().properties.size() == 2 &&
          rig.kms.atomic_commits.back().properties.front().value == 71 &&
          rig.kms.atomic_commits.back().properties.back().property_id == 22 &&
          rig.kms.atomic_commits.back().properties.back().value == 1,
      "unchanged pixels still flip framebuffer and VRR in one transaction");
  complete_flip(rig, pending, 10, 1'000'000'000);

  const auto still_enabled = frame_set(pixels, 3, true);
  pending = rig.presenter->present(still_enabled.view());
  gw::test::require(rig.kms.atomic_commits.back().properties.size() == 1,
                    "stable effective state omits an unnecessary reaffirmation");
  complete_flip(rig, pending, 11, 1'016'666'666);

  gw::test::require(
      rig.presenter->suspend(rig.error) ==
              output::BackendStateResult::Complete &&
          !rig.kms.master &&
          rig.kms.atomic_commits.back().flags == 0 &&
          rig.kms.atomic_commits.back().properties.size() == 2 &&
          rig.kms.atomic_commits.back().properties.back().value == 0,
      "VT suspend disables VRR on the current framebuffer before master drop");

  const output::SoftwareFrameView committed{
      {1, 2, 2, 60'000}, pixels, {}, 3, 3, 3};
  gw::test::require(
      rig.presenter->resume(committed).disposition ==
              output::PresentDisposition::Complete &&
          rig.kms.master && !rig.presenter->vrr_capability(1)->session_active &&
          rig.kms.atomic_commits.back().properties.back().value == 0,
      "VT acquire re-modesets off but remains inactive before peer ack");
  gw::test::require(rig.presenter->activate_session(rig.error) &&
                        rig.presenter->vrr_capability(1)->session_active,
                    "peer acknowledgement activates the presentation session");

  const auto reevaluated = frame_set(pixels, 4, true);
  pending = rig.presenter->present(reevaluated.view());
  gw::test::require(pending.disposition == output::PresentDisposition::Pending &&
                        rig.kms.atomic_commits.back().properties.back().value ==
                            1,
                    "normal post-Active transaction reevaluates and enables VRR");
  complete_flip(rig, pending, 12, 1'033'333'332);

  gw::test::require(
      rig.presenter->shutdown(rig.error) ==
              output::BackendStateResult::Complete &&
          rig.kms.properties[{KmsObjectType::Crtc, 40}].back().value == 1,
      "shutdown restores the exact original VRR_ENABLED value");
  const auto report = contents(rig.report.path());
  gw::test::require(
      report.find("\"record\":\"vrr-capability\"") != std::string::npos &&
          report.find("\"record\":\"vrr-decision\"") !=
              std::string::npos &&
          report.find("\"record\":\"vrr-timing\"") != std::string::npos &&
          report.find("\"record\":\"vrr-summary\"") != std::string::npos &&
          report.find("\"record\":\"vrr-restore\"") != std::string::npos &&
          report.find("\"getty_restore\":true") != std::string::npos,
      "DRM report contains deterministic VRR capability through restore proof");
}

void initial_enable_waits_for_real_flip() {
  Rig rig;
  const std::array pixels{0xff101010U, 0xff202020U, 0xff303030U,
                          0xff404040U};
  const auto enabled = frame_set(pixels, 1, true);
  const auto pending = rig.presenter->present(enabled.view());
  const auto count = rig.kms.atomic_commits.size();
  gw::test::require(
      pending.disposition == output::PresentDisposition::Pending && count >= 2 &&
          rig.kms.atomic_commits[count - 2].flags == AtomicAllowModeset &&
          rig.kms.atomic_commits[count - 2].properties.back().property_id == 22 &&
          rig.kms.atomic_commits[count - 2].properties.back().value == 0 &&
          rig.kms.atomic_commits.back().flags ==
              (AtomicNonblock | AtomicPageFlipEvent) &&
          rig.kms.atomic_commits.back().properties.back().property_id == 22 &&
          rig.kms.atomic_commits.back().properties.back().value == 1,
      "initial enable remains pending until a real off-to-on page flip");
  complete_flip(rig, pending, 1, 1'000'000'000);
}

void zero_sequence_with_kernel_timestamp_is_valid() {
  Rig rig;
  const std::array pixels{0xff101010U, 0xff202020U, 0xff303030U,
                          0xff404040U};
  const auto off = frame_set(pixels, 1, false, output::vrr::Decision::Disabled);
  gw::test::require(rig.presenter->present(off.view()).disposition ==
                        output::PresentDisposition::Complete,
                    "zero-sequence fixture initializes");
  const auto enabled = frame_set(pixels, 2, true);
  const auto pending = rig.presenter->present(enabled.view());
  rig.drm.queue_page_flip(pending.token, 40, 0, 1'000'000'000, true);
  const auto event = rig.presenter->service(POLLIN);
  gw::test::require(
      event.kind == output::BackendEventKind::Complete &&
          event.vrr_feedback.contains(1) &&
          event.vrr_feedback.at(1).flip_sequence == 0 &&
          event.vrr_feedback.at(1).timestamp_available &&
          event.vrr_feedback.at(1).kernel_timestamp_nanoseconds ==
              1'000'000'000 &&
          rig.presenter->finalize_pending(pending.token, rig.error),
      event.error.empty() ? rig.error : event.error);
  const auto report = contents(rig.report.path());
  gw::test::require(
      report.find("\"record\":\"vrr-timing\"") != std::string::npos &&
          report.find("\"sequence\":0") != std::string::npos &&
          report.find("\"kernel_timestamp_nanoseconds\":1000000000") !=
              std::string::npos,
      "zero-sequence completion retains valid kernel timing evidence");
}

void historical_profile_does_not_probe_vrr() {
  Rig rig(DrmPresentationApi::Atomic, true, false, false);
  const auto property_reads = std::ranges::count(rig.kms.calls,
                                                 std::string{"properties:40"});
  const auto probe_commits = rig.kms.atomic_commits.size();
  const std::array pixels{0xff101010U, 0xff202020U, 0xff303030U,
                          0xff404040U};
  const output::SoftwareFrameView frame{{1, 2, 2, 60'000}, pixels, {}, 1, 1, 1};
  const auto presented = rig.presenter->present(frame);
  gw::test::require(
      !rig.presenter->vrr_capability(1) && property_reads == 1 &&
          probe_commits == 1 &&
          presented.disposition == output::PresentDisposition::Complete &&
          std::ranges::count(rig.kms.calls, std::string{"properties:40"}) ==
              property_reads &&
          rig.kms.atomic_commits.back().properties.back().property_id != 22,
      "historical presentation neither probes nor appends the optional VRR property");
}

void legacy_and_test_rejection() {
  const std::array pixels{0xff101010U, 0xff202020U, 0xff303030U,
                          0xff404040U};
  Rig legacy(DrmPresentationApi::Legacy, false);
  const auto legacy_capability = legacy.presenter->vrr_capability(1);
  const auto requested = frame_set(pixels, 1, true);
  const auto calls_before = legacy.kms.calls.size();
  gw::test::require(
      legacy_capability && legacy_capability->hardware_capable &&
          !legacy_capability->atomic_kms_available &&
          !legacy_capability->kms_controllable &&
          output::vrr::has_reason(
              legacy_capability->reason_flags,
              output::vrr::Reason::AtomicKmsUnavailable) &&
          legacy.presenter->present(requested.view()).disposition ==
              output::PresentDisposition::Rejected &&
          legacy.kms.calls.size() == calls_before,
      "legacy KMS diagnoses capability but rejects VRR without mutation");

  Rig rejected(DrmPresentationApi::Atomic, true, true);
  const auto rejected_capability = rejected.presenter->vrr_capability(1);
  const auto commits_before = rejected.kms.atomic_commits.size();
  gw::test::require(
      rejected_capability && rejected_capability->hardware_capable &&
          !rejected_capability->atomic_test_passed &&
          !rejected_capability->kms_controllable &&
          rejected.presenter->present(requested.view()).disposition ==
              output::PresentDisposition::Rejected &&
          rejected.kms.atomic_commits.size() == commits_before,
      "VRR TEST_ONLY rejection leaves ordinary atomic presentation available");
}

void readback_mismatch_is_fatal() {
  Rig rig;
  const std::array pixels{0xff101010U, 0xff202020U, 0xff303030U,
                          0xff404040U};
  const auto off = frame_set(pixels, 1, false, output::vrr::Decision::Disabled);
  gw::test::require(rig.presenter->present(off.view()).disposition ==
                        output::PresentDisposition::Complete,
                    "readback mismatch fixture initializes");
  const auto enabled = frame_set(pixels, 2, true);
  const auto pending = rig.presenter->present(enabled.view());
  const auto commits_before = rig.kms.atomic_commits.size();
  rig.kms.property_readback_overrides[{40, 22}] = 0;
  rig.drm.queue_page_flip(pending.token, 40, 20, 2'000'000'000, true);
  const auto event = rig.presenter->service(POLLIN);
  gw::test::require(
      event.kind == output::BackendEventKind::Fatal &&
          event.error.find("readback") != std::string::npos &&
          rig.kms.atomic_commits.size() == commits_before + 2 &&
          property_value(rig.kms.atomic_commits[commits_before], 22) == 0 &&
          property_value(rig.kms.atomic_commits.back(), 22) == 1,
      "readback divergence attempts VRR-off before saved-state restore");
}

void invalid_timing_restores_saved_state() {
  Rig rig;
  const std::array pixels{0xff101010U, 0xff202020U, 0xff303030U,
                          0xff404040U};
  const auto off = frame_set(pixels, 1, false, output::vrr::Decision::Disabled);
  gw::test::require(rig.presenter->present(off.view()).disposition ==
                        output::PresentDisposition::Complete,
                    "invalid timing fixture initializes");
  const auto enabled = frame_set(pixels, 2, true);
  const auto pending = rig.presenter->present(enabled.view());
  const auto commits_before = rig.kms.atomic_commits.size();
  rig.drm.queue_page_flip(pending.token, 40, 0, 0, false);
  const auto event = rig.presenter->service(POLLIN);
  gw::test::require(
      event.kind == output::BackendEventKind::Fatal &&
          event.error.find("timing") != std::string::npos &&
          event.error.find("sequence=0") != std::string::npos &&
          event.error.find("timestamp_available=false") != std::string::npos &&
          event.error.find("kernel_timestamp_nanoseconds=0") !=
              std::string::npos &&
          event.error.find("timestamp_monotonic=true") != std::string::npos &&
          rig.kms.atomic_commits.size() == commits_before + 2 &&
          property_value(rig.kms.atomic_commits[commits_before], 22) == 0 &&
          property_value(rig.kms.atomic_commits.back(), 22) == 1,
      "invalid page-flip timing disables VRR before saved-state restore");
}

void incapable_output_accepts_unavailable_timing() {
  Rig rig(DrmPresentationApi::Atomic, true, false, true, false);
  const auto capability = rig.presenter->vrr_capability(1);
  gw::test::require(
      capability && !capability->hardware_capable &&
          !capability->kms_controllable,
      "incapable timing fixture retains a non-controllable VRR contract");

  const std::array first_pixels{0xff101010U, 0xff202020U, 0xff303030U,
                                0xff404040U};
  const auto first =
      frame_set(first_pixels, 1, false, output::vrr::Decision::Disabled);
  gw::test::require(rig.presenter->present(first.view()).disposition ==
                        output::PresentDisposition::Complete,
                    "incapable timing fixture initializes fixed refresh");

  const std::array second_pixels{0xff111111U, 0xff222222U, 0xff333333U,
                                 0xff444444U};
  const auto second =
      frame_set(second_pixels, 2, false, output::vrr::Decision::Disabled);
  const auto pending = rig.presenter->present(second.view());
  gw::test::require(pending.disposition == output::PresentDisposition::Pending,
                    "incapable output still submits an ordinary page flip");
  rig.drm.queue_page_flip(pending.token, 40, 7, 0, false);
  const auto event = rig.presenter->service(POLLIN);
  gw::test::require(
      event.kind == output::BackendEventKind::Complete &&
          event.vrr_feedback.contains(1) &&
          !event.vrr_feedback.at(1).effective_enabled &&
          !event.vrr_feedback.at(1).timestamp_available &&
          rig.presenter->finalize_pending(pending.token, rig.error),
      event.error.empty() ? rig.error : event.error);
  const auto report = contents(rig.report.path());
  gw::test::require(
      report.find("\"record\":\"vrr-decision\"") != std::string::npos &&
          report.find("\"record\":\"vrr-timing\"") == std::string::npos,
      "incapable output reports its decision without claiming timing evidence");
}

} // namespace

int main() {
  atomic_transition_suspend_restore();
  initial_enable_waits_for_real_flip();
  zero_sequence_with_kernel_timestamp_is_valid();
  historical_profile_does_not_probe_vrr();
  legacy_and_test_rejection();
  readback_mismatch_is_fatal();
  invalid_timing_restores_saved_state();
  incapable_output_accepts_unavailable_timing();
  return 0;
}
