#include "backends/drm/fake_drm_api.hpp"
#include "backends/drm/fake_kms_api.hpp"
#include "backends/drm/presenter.hpp"
#include "tests/helpers/fake_kms.hpp"
#include "tests/helpers/test_support.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <poll.h>
#include <span>
#include <string>
#include <string_view>
#include <unistd.h>
#include <vector>

namespace {

using namespace glasswyrm;
using namespace glasswyrm::drm;

constexpr std::uint32_t kWidth = 2560;
constexpr std::uint32_t kHeight = 1440;
constexpr std::uint32_t kRefreshMillihertz = 120'000;
constexpr std::uint64_t kFullFrameBytes = std::uint64_t{kWidth} * kHeight * 4U;
constexpr std::uint64_t kMaximumSteadyCopyBytes = 256U * 1024U;
constexpr std::uint64_t kSteadyFrames = 180;

std::filesystem::path temporary_directory() {
  std::string pattern = "/tmp/glasswyrm-m14-bounded-damage-XXXXXX";
  gw::test::require(::mkdtemp(pattern.data()) != nullptr,
                    "create M14 bounded-damage test directory");
  return pattern;
}

DeviceSnapshot snapshot() {
  DeviceSnapshot value;
  value.canonical_path = "/dev/dri/card0";
  value.device_major = 226;
  value.primary_node = value.dumb_buffer = value.universal_planes = true;
  value.atomic = true;
  value.driver.name = "virtio_gpu";
  value.crtcs.push_back({40, 0, {10}});
  Mode mode{"2560x1440", kWidth, kHeight, kRefreshMillihertz, 650'000, true};
  mode.hsync_start = kWidth + 1U;
  mode.hsync_end = kWidth + 2U;
  mode.htotal = kWidth + 3U;
  mode.vsync_start = kHeight + 1U;
  mode.vsync_end = kHeight + 2U;
  mode.vtotal = kHeight + 3U;
  mode.vrefresh_hz = 120;
  Connector connector;
  connector.id = 10;
  connector.type = static_cast<std::uint32_t>(ConnectorType::Virtual);
  connector.type_id = 1;
  connector.status = ConnectionStatus::Connected;
  connector.modes.push_back(mode);
  connector.possible_crtc_mask = 1;
  connector.current_crtc_id = 40;
  value.connectors.push_back(connector);
  value.planes.push_back({50, PlaneType::Primary, 1, {kFormatXrgb8888}, 40});
  return value;
}

void configure(FakeKmsApi &api) {
  api.master = true;
  api.dumb_allocation = {7, kWidth * 4U, kFullFrameBytes};
  api.connector_crtcs[10] = 40;
  KmsMode mode{};
  mode.hdisplay = kWidth;
  mode.vdisplay = kHeight;
  mode.name = "2560x1440";
  api.crtcs[40] = {40, 60, 0, 0, true, mode};
  api.properties[{KmsObjectType::Connector, 10}] =
      gw::test::kms_properties({"CRTC_ID"}, 10);
  api.properties[{KmsObjectType::Crtc, 40}] =
      gw::test::kms_properties({"MODE_ID", "ACTIVE"}, 20);
  api.planes[50] = {
      50, 60, 40, 0, 0, kWidth, kHeight, 0, 0, kWidth << 16U, kHeight << 16U};
  api.properties[{KmsObjectType::Plane, 50}] = gw::test::kms_properties(
      {"FB_ID", "CRTC_ID", "SRC_X", "SRC_Y", "SRC_W", "SRC_H",
       "CRTC_X", "CRTC_Y", "CRTC_W", "CRTC_H"},
      30);
}

output::SoftwareFrameSet
frame_set(const std::span<const std::uint32_t> pixels,
          const std::span<const gw::compositor::Rectangle> damage,
          const std::uint64_t generation) {
  output::SoftwareFrameSet result;
  output::OutputFrameResult item;
  std::string error;
  gw::test::require(item.frame.configure(1, kWidth, kHeight, error), error);
  std::ranges::copy(pixels, item.frame.pixels().begin());
  item.output = item.frame.spec(kRefreshMillihertz);
  item.logical = {0, 0, kWidth, kHeight};
  item.scale = {1, 1};
  item.transform = output::OutputTransform::Normal;
  item.damage.assign(damage.begin(), damage.end());
  gw::test::require(result.append(std::move(item), error), error);
  gw::test::require(
      result.finalize(1, 1, generation, generation, generation, error), error);
  return result;
}

std::uint64_t unsigned_field(const std::string_view line,
                             const std::string_view key) {
  const auto field = line.find(key);
  gw::test::require(field != std::string_view::npos,
                    "damage report contains required numeric field");
  const auto first = field + key.size();
  const auto last = line.find_first_not_of("0123456789", first);
  return std::stoull(std::string(line.substr(first, last - first)));
}

std::vector<std::string> damage_reports(const std::filesystem::path &path) {
  std::ifstream input(path);
  std::vector<std::string> result;
  for (std::string line; std::getline(input, line);)
    if (line.find("\"record\":\"damage-copy\"") != std::string::npos)
      result.push_back(std::move(line));
  return result;
}

gw::compositor::Rectangle update_rectangle(const std::uint64_t generation) {
  const auto x = static_cast<std::int32_t>((generation * 73U) % (kWidth - 64U));
  const auto y =
      static_cast<std::int32_t>((generation * 41U) % (kHeight - 64U));
  return {x, y, 64, 64};
}

void paint(std::span<std::uint32_t> pixels,
           const gw::compositor::Rectangle rectangle,
           const std::uint32_t color) {
  for (std::uint32_t y = 0; y < rectangle.height; ++y)
    std::fill_n(pixels.begin() +
                    static_cast<std::size_t>(rectangle.y + y) * kWidth +
                    rectangle.x,
                rectangle.width, color);
}

} // namespace

int main() {
  FakeDrmApi drm({"/dev/dri/card0", DeviceOpenStatus::Success, snapshot(), {}});
  FakeKmsApi kms;
  configure(kms);
  const int inherited = ::open("/dev/null", O_RDONLY | O_CLOEXEC);
  gw::test::require(inherited >= 0, "open inherited fake DRM descriptor");
  DeviceDiscovery discovery;
  auto device = Device::adopt(drm, inherited, {true, true}, discovery);
  (void)::close(inherited);
  gw::test::require(device.has_value(), "open bounded fake DRM device");

  const auto directory = temporary_directory();
  DrmReport report(directory / "drm-report.jsonl");
  DrmPresenter presenter(std::move(*device), kms, &report);
  DrmPresenterConfig config;
  config.output = {1, kWidth, kHeight, kRefreshMillihertz};
  config.connector = "Virtual-1";
  config.api = DrmPresentationApi::Atomic;
  config.damage_aware_copy = true;
  std::string error;
  gw::test::require(presenter.initialize(config, nullptr, error),
                    "initialize bounded fake DRM presenter: " + error);

  std::vector<std::uint32_t> pixels(static_cast<std::size_t>(kWidth) * kHeight,
                                    0xff000000U);
  const std::array full_damage{
      gw::compositor::Rectangle{0, 0, kWidth, kHeight}};
  const auto initial = frame_set(pixels, full_damage, 1);
  const auto initial_result = presenter.present(initial.view());
  gw::test::require(initial_result.disposition ==
                            output::PresentDisposition::Complete &&
                        initial_result.visible_hash == initial.aggregate_hash(),
                    "first scanout buffer seeds with canonical pixel parity");

  for (std::uint64_t generation = 2; generation <= kSteadyFrames + 1U;
       ++generation) {
    const auto rectangle = update_rectangle(generation);
    paint(pixels, rectangle,
          0xff000000U | static_cast<std::uint32_t>(generation));
    const std::array damage{rectangle};
    const auto frame = frame_set(pixels, damage, generation);
    const auto pending = presenter.present(frame.view());
    gw::test::require(pending.disposition ==
                          output::PresentDisposition::Pending,
                      "bounded fake DRM update stages one page flip");
    drm.queue_page_flip(pending.token, 40, generation);
    const auto completed = presenter.service(POLLIN);
    gw::test::require(
        completed.kind == output::BackendEventKind::Complete &&
            completed.visible_hash == frame.aggregate_hash() &&
            presenter.finalize_pending(pending.token, error),
        "bounded fake DRM update completes with canonical pixel parity");
  }

  const auto reports = damage_reports(report.path());
  gw::test::require(reports.size() == kSteadyFrames + 1U,
                    "every fake DRM generation reports its copy plan");
  std::uint64_t steady_copied = 0;
  std::uint64_t steady_baseline = 0;
  bool history_union_observed = false;
  for (const auto &line : reports) {
    const auto generation = unsigned_field(line, "\"generation\":");
    if (generation < 3)
      continue;
    const auto copied = unsigned_field(line, "\"copied_bytes\":");
    const auto actual_copied =
        unsigned_field(line, "\"drm_copied_bytes\":");
    const auto parity =
        unsigned_field(line, "\"parity_verified_bytes\":");
    const auto scanout_readback =
        unsigned_field(line, "\"scanout_readback_bytes\":");
    gw::test::require(
        line.find("\"full_copy_reason\":\"none\"") != std::string::npos &&
            copied <= kMaximumSteadyCopyBytes && actual_copied == copied &&
            parity == kFullFrameBytes && scanout_readback == 0 &&
            line.find("\"copy_nanoseconds\":") != std::string::npos &&
            line.find("\"parity_nanoseconds\":") != std::string::npos,
        "steady fake DRM damage avoids full copy and mapped readback");
    steady_copied += copied;
    steady_baseline += kFullFrameBytes;
    history_union_observed |= unsigned_field(line, "\"history_span\":") == 2 &&
                              copied > 64U * 64U * 4U;
  }
  gw::test::require(
      steady_copied * 10U < steady_baseline && history_union_observed,
      "steady copies stay below ten percent and union skipped generations");

  const auto advertised = update_rectangle(kSteadyFrames + 2U);
  paint(pixels, advertised, 0xff334455U);
  pixels[0] = 0xffabcdefU;
  const std::array incomplete_damage{advertised};
  const auto mismatch =
      frame_set(pixels, incomplete_damage, kSteadyFrames + 2U);
  const auto pending = presenter.present(mismatch.view());
  gw::test::require(pending.disposition == output::PresentDisposition::Pending,
                    "out-of-damage mutation stages recovery frame");
  drm.queue_page_flip(pending.token, 40, kSteadyFrames + 2U);
  const auto completed = presenter.service(POLLIN);
  gw::test::require(completed.kind == output::BackendEventKind::Complete &&
                        completed.visible_hash == mismatch.aggregate_hash() &&
                        presenter.finalize_pending(pending.token, error),
                    "out-of-damage mutation recovers before scanout");
  const auto recovered = damage_reports(report.path());
  gw::test::require(
      recovered.back().find("\"full_copy_reason\":\"canonical-mismatch\"") !=
              std::string::npos &&
          unsigned_field(recovered.back(), "\"drm_copied_bytes\":") >
              kFullFrameBytes &&
          unsigned_field(recovered.back(), "\"parity_verified_bytes\":") >=
              kFullFrameBytes &&
          unsigned_field(recovered.back(), "\"scanout_readback_bytes\":") >=
              kFullFrameBytes,
      "parity mismatch records a conservative canonical recovery");

  gw::test::require(presenter.shutdown(error) ==
                        output::BackendStateResult::Complete,
                    "shutdown bounded fake DRM presenter: " + error);
  std::error_code ignored;
  std::filesystem::remove_all(directory, ignored);
  return 0;
}
