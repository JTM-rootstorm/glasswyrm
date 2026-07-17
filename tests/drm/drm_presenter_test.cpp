#include "backends/drm/fake_drm_api.hpp"
#include "backends/drm/fake_kms_api.hpp"
#include "backends/drm/presenter.hpp"
#include "tests/helpers/test_support.hpp"

#include <algorithm>
#include <array>
#include <csignal>
#include <filesystem>
#include <fcntl.h>
#include <fstream>
#include <poll.h>
#include <string>
#include <unistd.h>
#include <vector>

namespace {
using namespace glasswyrm;
using namespace glasswyrm::drm;

std::filesystem::path temporary_directory() {
  std::string pattern = "/tmp/glasswyrm-drm-presenter-XXXXXX";
  const auto result = ::mkdtemp(pattern.data());
  gw::test::require(result != nullptr, "create presenter test directory");
  return result;
}

DeviceSnapshot snapshot(const bool atomic = true, const bool planes = true) {
  DeviceSnapshot value;
  value.canonical_path = "/dev/dri/card0";
  value.device_major = 226;
  value.primary_node = value.dumb_buffer = value.universal_planes = true;
  value.atomic = atomic;
  value.driver.name = "virtio_gpu";
  value.crtcs.push_back({40, 0, {10}});
  Mode mode{"2x2", 2, 2, 60'000, 25'000, true};
  mode.hsync_start = 3; mode.hsync_end = 4; mode.htotal = 5;
  mode.vsync_start = 3; mode.vsync_end = 4; mode.vtotal = 5;
  mode.vrefresh_hz = 60;
  Connector connector;
  connector.id = 10;
  connector.type = static_cast<std::uint32_t>(ConnectorType::Virtual);
  connector.type_id = 1;
  connector.status = ConnectionStatus::Connected;
  connector.modes.push_back(mode);
  connector.possible_crtc_mask = 1;
  connector.current_crtc_id = 40;
  value.connectors.push_back(connector);
  if (planes)
    value.planes.push_back({50, PlaneType::Primary, 1,
                            {kFormatXrgb8888}, 40});
  return value;
}

std::vector<ObjectProperty> properties(
    const std::initializer_list<const char*> names, std::uint32_t first) {
  std::vector<ObjectProperty> value;
  for (const auto name : names) value.push_back({first++, name, 0, 64});
  return value;
}

void configure(FakeKmsApi& api, const bool planes = true) {
  api.dumb_allocation = {7, 8, 16};
  api.connector_crtcs[10] = 40;
  KmsMode mode{}; mode.hdisplay = mode.vdisplay = 2; mode.name = "2x2";
  api.crtcs[40] = {40, 60, 0, 0, true, mode};
  api.properties[{KmsObjectType::Connector, 10}] =
      properties({"CRTC_ID"}, 10);
  api.properties[{KmsObjectType::Crtc, 40}] =
      properties({"MODE_ID", "ACTIVE"}, 20);
  if (planes) {
    api.planes[50] = {50, 60, 40, 0, 0, 2, 2, 0, 0, 2U << 16U, 2U << 16U};
    api.properties[{KmsObjectType::Plane, 50}] = properties(
        {"FB_ID", "CRTC_ID", "SRC_X", "SRC_Y", "SRC_W", "SRC_H",
         "CRTC_X", "CRTC_Y", "CRTC_W", "CRTC_H"}, 30);
  }
}

class FakeVtApi final : public session::VirtualTerminalApi {
 public:
  int open_terminal(std::string_view) override { log.push_back("open_vt"); return 9; }
  bool identify(int, session::DeviceIdentity& value) override { value = {4, 2, true}; return true; }
  bool get_state(int, session::VirtualTerminalState& value) override { value.active = 1; return true; }
  bool get_mode(int, session::VirtualTerminalMode& value) override { value = {}; return true; }
  bool get_kd_mode(int, int& value) override { value = 0; return true; }
  bool activate(int, unsigned number) override { log.push_back("activate:" + std::to_string(number)); return true; }
  bool wait_until_active(int, unsigned) override { return true; }
  bool set_process_mode(int, int, int) override { log.push_back("vt_process"); return true; }
  bool set_mode(int, const session::VirtualTerminalMode&) override { log.push_back("restore_vt"); return true; }
  bool set_graphics_mode(int) override { log.push_back("graphics"); return true; }
  bool set_kd_mode(int, int) override { log.push_back("restore_kd"); return true; }
  bool acknowledge_release(int) override { log.push_back("release"); return true; }
  bool acknowledge_acquire(int) override { log.push_back("acquire"); return true; }
  void close_terminal(int) noexcept override { log.push_back("close_vt"); }
  std::string last_error() const override { return "fake VT failure"; }
  std::vector<std::string> log;
};

struct Rig {
  std::filesystem::path directory{temporary_directory()};
  FakeDrmApi drm;
  FakeKmsApi kms;
  DrmReport report{directory / "report.jsonl"};
  headless::FrameDumper mirror{directory / "mirror"};
  FakeVtApi vt;
  std::unique_ptr<DrmPresenter> presenter;
  std::string error;
  bool initialized{};

  Rig(DrmPresentationApi policy, bool atomic = true, bool planes = true,
      bool fail_atomic = false, bool with_report = true,
      bool damage_aware_copy = false)
      : drm({"/dev/dri/card0", DeviceOpenStatus::Success,
             snapshot(atomic, planes), {}}) {
    configure(kms, planes);
    if (fail_atomic) kms.fail_next(KmsOperation::AtomicCommit);
    DeviceDiscovery discovery;
    auto device = Device::open(drm, "/dev/dri/card0", {}, discovery);
    gw::test::require(device.has_value(), "open fake presenter device");
    presenter = std::make_unique<DrmPresenter>(
        std::move(*device), kms, with_report ? &report : nullptr, &mirror);
    DrmPresenterConfig config;
    config.output = {1, 2, 2, 60'000};
    config.connector = "Virtual-1";
    config.api = policy;
    config.tty_path = "/dev/tty2";
    config.vt_signals = {SIGUSR1, SIGUSR2};
    config.damage_aware_copy = damage_aware_copy;
    initialized = presenter->initialize(config, &vt, error);
  }
  ~Rig() {
    presenter.reset();
    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
  }
};

output::SoftwareFrameView frame(std::span<const std::uint32_t> pixels,
                                std::uint64_t ordinal,
                                std::uint32_t refresh = 60'000,
                                std::span<const gw::compositor::Rectangle>
                                    damage = {}) {
  return {{1, 2, 2, refresh}, pixels, damage, ordinal, ordinal, ordinal};
}

std::string contents(const std::filesystem::path& path) {
  std::ifstream input(path);
  return {std::istreambuf_iterator<char>(input), {}};
}

void replace_report_target(void* context) {
  const auto& path = *static_cast<std::filesystem::path*>(context);
  const auto replacement = path.string() + ".foreign";
  std::ofstream(replacement) << "foreign\n";
  std::filesystem::rename(replacement, path);
}

void atomic_lifecycle_and_delayed_evidence() {
  Rig rig(DrmPresentationApi::Atomic);
  gw::test::require(rig.initialized &&
                        rig.presenter->selected_api() == ReportApiPath::Atomic,
                    "forced atomic presenter initializes");
  gw::test::require(rig.kms.atomic_commits.size() == 1 &&
                        rig.kms.atomic_commits[0].flags ==
                            (AtomicTestOnly | AtomicAllowModeset) &&
                        rig.kms.atomic_commits[0].properties.size() == 13 &&
                        rig.kms.atomic_commits[0].properties[3].value == 70,
                    "initialization performs exact atomic TEST_ONLY");
  const std::array first{0xff101010U, 0xff202020U, 0xff303030U, 0xff404040U};
  const auto initial = rig.presenter->present(frame(first, 1));
  gw::test::require(initial.disposition == output::PresentDisposition::Complete &&
                        rig.kms.atomic_commits.back().flags == AtomicAllowModeset,
                    "first presentation is a blocking atomic modeset");
  const auto first_fb = rig.presenter->front_framebuffer();
  const std::array second{0xff111111U, 0xff222222U, 0xff333333U, 0xff444444U};
  const auto pending = rig.presenter->present(frame(second, 2));
  gw::test::require(pending.disposition == output::PresentDisposition::Pending &&
                        rig.kms.atomic_commits.back().flags ==
                            (AtomicNonblock | AtomicPageFlipEvent) &&
                        rig.kms.atomic_commits.back().properties.size() == 1 &&
                        rig.kms.atomic_commits.back().properties[0].value == 71 &&
                        rig.presenter->front_framebuffer() == first_fb &&
                        contents(rig.report.path()).find("\"record\":\"flip\"") ==
                            std::string::npos,
                    "flip submission delays evidence and front-buffer swap");
  rig.drm.queue_page_flip(pending.token, 40, 77);
  const auto event = rig.presenter->service(POLLIN);
  gw::test::require(event.kind == output::BackendEventKind::Complete &&
                        rig.presenter->front_framebuffer() == first_fb &&
                        contents(rig.report.path()).find("\"record\":\"flip\"") ==
                            std::string::npos,
                    "verified event remains staged until coordinator finalizes");
  gw::test::require(rig.presenter->finalize_pending(pending.token, rig.error) &&
                        rig.presenter->front_framebuffer() != first_fb &&
                        contents(rig.report.path()).find("\"record\":\"flip\"") !=
                            std::string::npos &&
                        std::filesystem::exists(rig.directory / "mirror" /
                            "frame-000002-output-0000000000000001.ppm"),
                    "finalize publishes report, mirror, and swaps buffers");
}

void presentation_refresh_tolerance() {
  Rig rig(DrmPresentationApi::Atomic);
  const std::array pixels{0xff101010U, 0xff202020U, 0xff303030U, 0xff404040U};
  gw::test::require(
      rig.presenter->present(frame(pixels, 1, 60'003)).disposition ==
          output::PresentDisposition::Complete,
      "presentation accepts the selected mode refresh tolerance");
  gw::test::require(
      rig.presenter->present(frame(pixels, 2, 61'001)).disposition ==
          output::PresentDisposition::Rejected,
      "presentation rejects refresh outside the selected mode tolerance");
}

void accumulated_damage_copy_and_vt_fallback() {
  Rig rig(DrmPresentationApi::Atomic, true, true, false, true, true);
  const std::array full_damage{gw::compositor::Rectangle{0, 0, 2, 2}};
  const std::array first{0xff000000U, 0xff000000U, 0xff000000U,
                         0xff000000U};
  gw::test::require(
      rig.presenter->present(frame(first, 1, 60'000, full_damage))
              .disposition == output::PresentDisposition::Complete,
      "damage-aware first frame completes");

  const std::array top_left{gw::compositor::Rectangle{0, 0, 1, 1}};
  const std::array second{0xffff0000U, 0xff000000U, 0xff000000U,
                          0xff000000U};
  auto pending = rig.presenter->present(frame(second, 2, 60'000, top_left));
  rig.drm.queue_page_flip(pending.token, 40, 2);
  gw::test::require(
      pending.disposition == output::PresentDisposition::Pending &&
          rig.presenter->service(POLLIN).kind ==
              output::BackendEventKind::Complete &&
          rig.presenter->finalize_pending(pending.token, rig.error),
      "damage-aware second buffer first use completes");

  const std::array bottom_right{gw::compositor::Rectangle{1, 1, 1, 1}};
  const std::array third{0xffff0000U, 0xff000000U, 0xff000000U,
                         0xff00ff00U};
  pending = rig.presenter->present(frame(third, 3, 60'000, bottom_right));
  rig.drm.queue_page_flip(pending.token, 40, 3);
  gw::test::require(
      pending.disposition == output::PresentDisposition::Pending &&
          rig.presenter->service(POLLIN).kind ==
              output::BackendEventKind::Complete &&
          rig.presenter->finalize_pending(pending.token, rig.error),
      "alternating buffer reuses bounded damage history");
  const auto report = contents(rig.report.path());
  gw::test::require(
      report.find("\"record\":\"damage-copy\",\"generation\":1") !=
              std::string::npos &&
          report.find("\"generation\":3,\"buffer\":0") !=
              std::string::npos &&
          report.find("\"copied_bytes\":8") != std::string::npos &&
          report.find("\"history_span\":2") != std::string::npos,
      "damage report proves two-generation eight-byte copy");

  gw::test::require(
      rig.presenter->suspend(rig.error) ==
              output::BackendStateResult::Complete &&
          rig.presenter->resume(frame(third, 3, 60'000, bottom_right))
                  .disposition == output::PresentDisposition::Complete &&
          contents(rig.report.path()).find(
              "\"full_copy_reason\":\"vt-resume\"") != std::string::npos,
      "VT resume forces and reports one complete scanout copy");
}

void incomplete_damage_recovers_with_full_copy() {
  Rig rig(DrmPresentationApi::Atomic, true, true, false, true, true);
  const std::array wrong_damage{gw::compositor::Rectangle{1, 1, 1, 1}};
  const std::array black{0xff000000U, 0xff000000U, 0xff000000U,
                         0xff000000U};
  gw::test::require(
      rig.presenter->present(frame(black, 1, 60'000, wrong_damage))
              .disposition == output::PresentDisposition::Complete,
      "incomplete-damage fixture initializes the first buffer");
  auto pending = rig.presenter->present(frame(black, 2, 60'000, wrong_damage));
  rig.drm.queue_page_flip(pending.token, 40, 2);
  gw::test::require(
      pending.disposition == output::PresentDisposition::Pending &&
          rig.presenter->service(POLLIN).kind ==
              output::BackendEventKind::Complete &&
          rig.presenter->finalize_pending(pending.token, rig.error),
      "incomplete-damage fixture initializes the second buffer");

  const std::array changed{0xffff0000U, 0xff000000U, 0xff000000U,
                           0xff000000U};
  pending = rig.presenter->present(frame(changed, 3, 60'000, wrong_damage));
  rig.drm.queue_page_flip(pending.token, 40, 3);
  gw::test::require(
      pending.disposition == output::PresentDisposition::Pending &&
          rig.presenter->service(POLLIN).kind ==
              output::BackendEventKind::Complete &&
          rig.presenter->finalize_pending(pending.token, rig.error) &&
          contents(rig.report.path()).find(
              "\"full_copy_reason\":\"canonical-mismatch\"") !=
              std::string::npos,
      "canonical mismatch retries a complete copy before KMS submission");
}

void zero_sequence_page_flip_completion() {
  Rig rig(DrmPresentationApi::Atomic);
  const std::array pixels{0xff111111U, 0xff222222U, 0xff333333U, 0xff444444U};
  gw::test::require(
      rig.presenter->present(frame(pixels, 1)).disposition ==
          output::PresentDisposition::Complete,
      "zero-sequence fixture initial frame");
  const auto pending = rig.presenter->present(frame(pixels, 2));
  gw::test::require(pending.disposition == output::PresentDisposition::Pending,
                    "zero-sequence fixture submits page flip");
  rig.drm.queue_page_flip(pending.token, 40, 0);
  const auto event = rig.presenter->service(POLLIN);
  gw::test::require(
      event.kind == output::BackendEventKind::Complete &&
          event.token == pending.token &&
          rig.presenter->finalize_pending(pending.token, rig.error) &&
          contents(rig.report.path()).find("\"page_flip_sequence\":0") !=
              std::string::npos,
      "valid page flip without vblank accounting completes and reports zero");
}

void policy_and_legacy_requests() {
  Rig fallback(DrmPresentationApi::Auto, true, true, true);
  gw::test::require(fallback.initialized &&
                        fallback.presenter->selected_api() == ReportApiPath::Legacy &&
                        !fallback.presenter->fallback_reason().empty() &&
                        fallback.presenter->fallback_reason().size() <=
                            DrmPresenter::kMaximumDiagnosticBytes,
                    "auto policy falls back after atomic TEST_ONLY failure");
  const std::array pixels{0xff000001U, 0xff000002U, 0xff000003U, 0xff000004U};
  gw::test::require(fallback.presenter->present(frame(pixels, 1)).disposition ==
                        output::PresentDisposition::Complete &&
                        fallback.kms.calls.back().starts_with("setcrtc:40:70:1"),
                    "legacy initial request identifies CRTC, FB, and connector");
  const auto pending = fallback.presenter->present(frame(pixels, 2));
  gw::test::require(pending.disposition == output::PresentDisposition::Pending &&
                        fallback.kms.calls.back().starts_with("pageflip:40:71:"),
                    "legacy flip request identifies CRTC, back FB, and cookie");

  Rig forced(DrmPresentationApi::Atomic, true, true, true);
  gw::test::require(!forced.initialized &&
                        forced.error.find("forced atomic") != std::string::npos,
                    "forced atomic policy does not fall back");
  Rig plane_less(DrmPresentationApi::Auto, true, false, false, true);
  gw::test::require(plane_less.initialized &&
                        plane_less.presenter->selected_api() == ReportApiPath::Legacy &&
                        plane_less.presenter->pipeline().primary_plane == 0,
                    "auto policy permits legacy implicit primary plane");
}

void mismatch_resume_and_shutdown_order() {
  Rig mismatch(DrmPresentationApi::Atomic);
  const std::array pixels{0xffabcdefU, 0xffabcdefU, 0xffabcdefU, 0xffabcdefU};
  gw::test::require(mismatch.presenter->present(frame(pixels, 1)).disposition ==
                        output::PresentDisposition::Complete,
                    "mismatch fixture initial frame");
  const auto pending = mismatch.presenter->present(frame(pixels, 2));
  mismatch.drm.queue_page_flip(pending.token, 99, 1);
  gw::test::require(mismatch.presenter->service(POLLIN).kind ==
                        output::BackendEventKind::Fatal,
                    "wrong CRTC completion is fatal");

  Rig session(DrmPresentationApi::Atomic);
  gw::test::require(session.presenter->present(frame(pixels, 1)).disposition ==
                        output::PresentDisposition::Complete &&
                        session.presenter->suspend(session.error) ==
                            output::BackendStateResult::Complete &&
                        session.presenter->resume(frame(pixels, 1)).disposition ==
                            output::PresentDisposition::Complete,
                    "VT release/acquire re-modesets committed pixels");
  gw::test::require(session.presenter->shutdown(session.error) ==
                        output::BackendStateResult::Complete,
                    "clean DRM shutdown is observable");
  const auto drop = std::ranges::find(session.kms.calls.rbegin(),
                                      session.kms.calls.rend(), "drop_master");
  const auto remove = std::ranges::find_if(session.kms.calls,
      [](const std::string& call) { return call.starts_with("rmfb:"); });
  gw::test::require(drop != session.kms.calls.rend() &&
                        remove != session.kms.calls.end() && remove < drop.base() &&
                        std::ranges::find(session.vt.log, "restore_kd") !=
                            session.vt.log.end(),
                    "shutdown restores KMS, releases scanout, then drops master/restores VT");
}

void fatal_abort_and_shutdown_failure_are_observable() {
  Rig abort(DrmPresentationApi::Atomic);
  const std::array pixels{0xff515151U, 0xff525252U, 0xff535353U, 0xff545454U};
  gw::test::require(abort.presenter->present(frame(pixels, 1)).disposition ==
                        output::PresentDisposition::Complete,
                    "abort fixture initial frame");
  const auto pending = abort.presenter->present(frame(pixels, 2));
  const auto front = abort.presenter->front_framebuffer();
  abort.presenter->abort_pending(pending.token, "page flip timeout");
  gw::test::require(abort.presenter->front_framebuffer() == front &&
                        contents(abort.report.path()).find("page flip timeout") !=
                            std::string::npos &&
                        !std::filesystem::exists(abort.directory / "mirror" /
                            "frame-000002-output-0000000000000001.ppm"),
                    "fatal abort reports reason and discards staged evidence");

  Rig failed(DrmPresentationApi::Atomic);
  gw::test::require(failed.presenter->present(frame(pixels, 1)).disposition ==
                        output::PresentDisposition::Complete,
                    "shutdown failure fixture initial frame");
  failed.kms.fail_next(KmsOperation::DropMaster);
  gw::test::require(failed.presenter->shutdown(failed.error) ==
                        output::BackendStateResult::Fatal &&
                        failed.error.find("injected KMS") != std::string::npos,
                    "master-drop failure makes shutdown observably fatal");
}

void report_failure_cannot_publish_mirror() {
  Rig rig(DrmPresentationApi::Atomic);
  const std::array pixels{0xff616161U, 0xff626262U, 0xff636363U, 0xff646464U};
  gw::test::require(rig.presenter->present(frame(pixels, 1)).disposition ==
                        output::PresentDisposition::Complete,
                    "evidence failure fixture initial frame");
  const auto pending = rig.presenter->present(frame(pixels, 2));
  const auto front = rig.presenter->front_framebuffer();
  rig.drm.queue_page_flip(pending.token, 40, 88);
  gw::test::require(rig.presenter->service(POLLIN).kind ==
                        output::BackendEventKind::Complete,
                    "evidence failure fixture completes flip");
  auto report_path = rig.report.path();
  rig.report.set_before_publish_hook_for_testing(replace_report_target,
                                                  &report_path);
  gw::test::require(!rig.presenter->finalize_pending(pending.token, rig.error) &&
                        rig.presenter->front_framebuffer() == front &&
                        !std::filesystem::exists(rig.directory / "mirror" /
                            "frame-000002-output-0000000000000001.ppm"),
                    "report publication failure leaves mirror unpublished and front unchanged");
}

void external_session_never_manages_master() {
  FakeDrmApi drm({"/dev/dri/card0", DeviceOpenStatus::Success, snapshot(), {}});
  FakeKmsApi kms;
  configure(kms);
  kms.master = true;
  const int inherited = ::open("/dev/null", O_RDONLY | O_CLOEXEC);
  DeviceDiscovery discovery;
  auto device = Device::adopt(drm, inherited, {}, discovery);
  gw::test::require(device.has_value(), "adopt external presenter device");
  DrmPresenter presenter(std::move(*device), kms);
  DrmPresenterConfig config;
  config.output = {1, 2, 2, 60'000};
  config.connector = "Virtual-1";
  config.api = DrmPresentationApi::Legacy;
  std::string error;
  gw::test::require(presenter.initialize(config, nullptr, error),
                    "external presenter initializes without VT or report");
  const std::array pixels{0xff010101U, 0xff020202U, 0xff030303U, 0xff040404U};
  gw::test::require(presenter.present(frame(pixels, 1)).disposition ==
                        output::PresentDisposition::Complete,
                    "external presenter performs initial modeset");
  gw::test::require(presenter.shutdown(error) ==
                        output::BackendStateResult::Complete,
                    "external shutdown completes without master ownership");
  gw::test::require(std::ranges::find(kms.calls, "set_master") == kms.calls.end() &&
                        std::ranges::find(kms.calls, "drop_master") == kms.calls.end() &&
                        std::ranges::count_if(kms.calls, [](const std::string& call) {
                          return call.starts_with("create_dumb:");
                        }) == 2,
                    "external session never manages master and allocates exactly two buffers");
  (void)::close(inherited);
}
}  // namespace

int main() {
  atomic_lifecycle_and_delayed_evidence();
  presentation_refresh_tolerance();
  accumulated_damage_copy_and_vt_fallback();
  incomplete_damage_recovers_with_full_copy();
  zero_sequence_page_flip_completion();
  policy_and_legacy_requests();
  mismatch_resume_and_shutdown_order();
  fatal_abort_and_shutdown_failure_are_observable();
  report_failure_cannot_publish_mirror();
  external_session_never_manages_master();
  return 0;
}
