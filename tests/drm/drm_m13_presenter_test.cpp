#include "backends/drm/fake_drm_api.hpp"
#include "backends/drm/fake_kms_api.hpp"
#include "backends/drm/presenter.hpp"
#include "tests/helpers/test_support.hpp"

#include <array>
#include <filesystem>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <poll.h>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

using namespace glasswyrm;
using namespace glasswyrm::drm;

std::filesystem::path temporary_directory() {
  std::string pattern = "/tmp/glasswyrm-drm-m13-XXXXXX";
  const auto result = ::mkdtemp(pattern.data());
  gw::test::require(result != nullptr, "create M13 DRM test directory");
  return result;
}

DeviceSnapshot snapshot() {
  DeviceSnapshot value;
  value.canonical_path = "/dev/dri/card0";
  value.device_major = 226;
  value.primary_node = true;
  value.dumb_buffer = true;
  value.universal_planes = true;
  value.atomic = true;
  value.driver.name = "virtio_gpu";
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
  connector.type = static_cast<std::uint32_t>(ConnectorType::Virtual);
  connector.type_id = 1;
  connector.status = ConnectionStatus::Connected;
  connector.modes.push_back(mode);
  connector.possible_crtc_mask = 1;
  connector.current_crtc_id = 40;
  value.connectors.push_back(connector);
  value.planes.push_back(
      {50, PlaneType::Primary, 1, {kFormatXrgb8888}, 40});
  return value;
}

std::vector<ObjectProperty> properties(
    const std::initializer_list<const char *> names, std::uint32_t first) {
  std::vector<ObjectProperty> result;
  for (const auto name : names)
    result.push_back({first++, name, 0, 64});
  return result;
}

void configure(FakeKmsApi &api) {
  api.master = true;
  api.dumb_allocation = {7, 8, 16};
  api.connector_crtcs[10] = 40;
  KmsMode mode{};
  mode.hdisplay = mode.vdisplay = 2;
  mode.name = "2x2";
  api.crtcs[40] = {40, 60, 0, 0, true, mode};
  api.properties[{KmsObjectType::Connector, 10}] =
      properties({"CRTC_ID"}, 10);
  api.properties[{KmsObjectType::Crtc, 40}] =
      properties({"MODE_ID", "ACTIVE"}, 20);
  api.planes[50] = {50, 60, 40, 0, 0, 2, 2, 0, 0,
                    2U << 16U, 2U << 16U};
  api.properties[{KmsObjectType::Plane, 50}] = properties(
      {"FB_ID", "CRTC_ID", "SRC_X", "SRC_Y", "SRC_W", "SRC_H",
       "CRTC_X", "CRTC_Y", "CRTC_W", "CRTC_H"},
      30);
}

output::SoftwareFrameSet frame_set(
    const std::span<const std::uint32_t> pixels, const std::uint64_t ordinal,
    const std::uint64_t layout_generation,
    const output::RationalScale scale,
    const output::OutputTransform transform) {
  output::SoftwareFrameSet result;
  output::OutputFrameResult item;
  std::string error;
  gw::test::require(item.frame.configure(1, 2, 2, error), error);
  std::ranges::copy(pixels, item.frame.pixels().begin());
  item.output = item.frame.spec(60'000);
  item.scale = scale;
  item.transform = transform;
  item.damage = {{0, 0, 2, 2}};
  gw::test::require(result.append(std::move(item), error), error);
  gw::test::require(result.finalize(layout_generation, 1, ordinal, ordinal,
                                   ordinal, error),
                    error);
  return result;
}

std::string contents(const std::filesystem::path &path) {
  std::ifstream input(path);
  return {std::istreambuf_iterator<char>(input), {}};
}

std::string hash_pair(const std::uint64_t hash) {
  std::ostringstream value;
  value << std::hex << std::nouppercase << std::setfill('0') << std::setw(16)
        << hash;
  const auto encoded = value.str();
  return "\"canonical_hash\":\"" + encoded +
         "\",\"scanout_hash\":\"" + encoded + "\"";
}

} // namespace

int main() {
  FakeDrmApi drm(
      {"/dev/dri/card0", DeviceOpenStatus::Success, snapshot(), {}});
  FakeKmsApi kms;
  configure(kms);
  const int inherited = ::open("/dev/null", O_RDONLY | O_CLOEXEC);
  gw::test::require(inherited >= 0, "open inherited fake DRM descriptor");
  DeviceDiscovery discovery;
  auto device = Device::adopt(drm, inherited, {true, true}, discovery);
  (void)::close(inherited);
  gw::test::require(device.has_value(), "adopt fake external DRM device");

  const auto directory = temporary_directory();
  DrmReport report(directory / "drm-report.jsonl");
  DrmPresenter presenter(std::move(*device), kms, &report);
  DrmPresenterConfig config;
  config.output = {1, 2, 2, 60'000};
  config.connector = "Virtual-1";
  config.api = DrmPresentationApi::Atomic;
  config.damage_aware_copy = true;
  std::string error;
  gw::test::require(presenter.initialize(config, nullptr, error), error);

  const std::array initial_pixels{0xff101010U, 0xff202020U, 0xff303030U,
                                  0xff404040U};
  const auto initial = frame_set(initial_pixels, 1, 1, {1, 1},
                                 output::OutputTransform::Normal);
  const auto initial_result = presenter.present(initial.view());
  gw::test::require(
      initial_result.disposition == output::PresentDisposition::Complete &&
          initial_result.visible_hash == initial.aggregate_hash(),
      "fixed DRM mode accepts the initial native renderer frame");

  const std::array changed_pixels{0xff414243U, 0xff515253U, 0xff616263U,
                                  0xff717273U};
  const auto changed = frame_set(changed_pixels, 2, 2, {4, 3},
                                 output::OutputTransform::Rotate180);
  const auto pending = presenter.present(changed.view());
  gw::test::require(
      pending.disposition == output::PresentDisposition::Pending &&
          kms.atomic_commits.size() == 3 &&
          kms.atomic_commits.back().flags ==
              (AtomicNonblock | AtomicPageFlipEvent) &&
          kms.atomic_commits.back().properties.size() == 1,
      "scale and transform remain renderer-side and submit an ordinary native "
      "framebuffer flip");
  drm.queue_page_flip(pending.token, 40, 2);
  const auto completed = presenter.service(POLLIN);
  gw::test::require(
      completed.kind == output::BackendEventKind::Complete &&
          completed.visible_hash == changed.aggregate_hash() &&
          presenter.finalize_pending(pending.token, error),
      "configuration-change frame completes through the existing page-flip "
      "transaction");

  const auto report_contents = contents(report.path());
  const auto native_hash = changed.outputs().at(1).visible_hash;
  gw::test::require(
      report_contents.find(
          "\"full_copy_reason\":\"output-configuration-changed\"") !=
              std::string::npos &&
          report_contents.find(hash_pair(native_hash)) != std::string::npos,
      "configuration changes force a named full copy with canonical/scanout "
      "frame hash parity");

  output::SoftwareFrameSet wrong_size;
  output::OutputFrameResult wrong_item;
  const std::array wrong_pixels{0xff000000U};
  gw::test::require(wrong_item.frame.configure(1, 1, 1, error), error);
  std::ranges::copy(wrong_pixels, wrong_item.frame.pixels().begin());
  wrong_item.output = wrong_item.frame.spec(60'000);
  wrong_item.scale = {4, 3};
  wrong_item.transform = output::OutputTransform::Rotate180;
  wrong_item.damage = {{0, 0, 1, 1}};
  gw::test::require(wrong_size.append(std::move(wrong_item), error) &&
                        wrong_size.finalize(3, 1, 3, 3, 3, error),
                    error);
  const auto commits_before = kms.atomic_commits.size();
  gw::test::require(
      presenter.present(wrong_size.view()).disposition ==
              output::PresentDisposition::Rejected &&
          kms.atomic_commits.size() == commits_before,
      "DRM rejects a scaled logical-size frame before KMS; scanout always uses "
      "the fixed native mode");

  gw::test::require(
      presenter.shutdown(error) == output::BackendStateResult::Complete,
      error);
  std::error_code ignored;
  std::filesystem::remove_all(directory, ignored);
  return 0;
}
