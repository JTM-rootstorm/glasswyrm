#include "backends/drm/device.hpp"
#include "backends/drm/fake_drm_api.hpp"

#include "tests/helpers/test_support.hpp"

#include <fcntl.h>
#include <string>
#include <unistd.h>

namespace {

glasswyrm::drm::DeviceSnapshot valid_snapshot() {
  using namespace glasswyrm::drm;
  DeviceSnapshot snapshot;
  snapshot.canonical_path = "/dev/dri/card0";
  snapshot.device_major = 226;
  snapshot.device_minor = 0;
  snapshot.driver = {"virtio_gpu", "20240101", "Virtio GPU", "pci:0000:00:02.0",
                     1, 2, 3};
  snapshot.primary_node = true;
  snapshot.dumb_buffer = true;
  snapshot.universal_planes = true;
  snapshot.atomic = true;
  snapshot.crtcs.push_back({40, 0, {10}});
  Connector connector;
  connector.id = 10;
  connector.type = static_cast<std::uint32_t>(ConnectorType::Virtual);
  connector.type_id = 1;
  connector.status = ConnectionStatus::Connected;
  connector.modes.push_back({"1024x768", 1024, 768, 60'000, 65'000, true});
  connector.possible_crtc_mask = 1;
  connector.current_crtc_id = 40;
  snapshot.connectors.push_back(std::move(connector));
  snapshot.planes.push_back(
      {50, PlaneType::Primary, 1, {kFormatXrgb8888}, 40});
  return snapshot;
}

}  // namespace

int main() {
  using namespace glasswyrm::drm;
  DeviceDiscovery discovery;

  FakeDrmApi wrong_path({"/dev/dri/card0", DeviceOpenStatus::Success,
                         valid_snapshot(), {}});
  gw::test::require(
      !Device::open(wrong_path, "/dev/dri/card1", {}, discovery) &&
          discovery.status == DeviceOpenStatus::InvalidPath,
      "unconfigured DRM path is rejected");

  auto render_snapshot = valid_snapshot();
  render_snapshot.primary_node = false;
  FakeDrmApi render_node(
      {"/dev/dri/renderD128", DeviceOpenStatus::Success,
       std::move(render_snapshot), {}});
  gw::test::require(
      !Device::open(render_node, "/dev/dri/renderD128", {}, discovery) &&
          discovery.status == DeviceOpenStatus::NotPrimaryNode &&
          render_node.close_count() == 1,
      "render node is rejected and closed");

  auto no_dumb_snapshot = valid_snapshot();
  no_dumb_snapshot.dumb_buffer = false;
  FakeDrmApi no_dumb({"/dev/dri/card0", DeviceOpenStatus::Success,
                      std::move(no_dumb_snapshot), {}});
  gw::test::require(
      !Device::open(no_dumb, "/dev/dri/card0", {}, discovery) &&
          discovery.status == DeviceOpenStatus::MissingDumbBuffer &&
          no_dumb.close_count() == 1,
      "dumb-buffer capability is mandatory");

  auto no_resource_snapshot = valid_snapshot();
  no_resource_snapshot.connectors.clear();
  FakeDrmApi no_resources({"/dev/dri/card0", DeviceOpenStatus::Success,
                           std::move(no_resource_snapshot), {}});
  gw::test::require(
      !Device::open(no_resources, "/dev/dri/card0", {}, discovery) &&
          discovery.status == DeviceOpenStatus::MissingResources,
      "connector and CRTC resources are mandatory");

  FakeDrmApi api({"/dev/dri/card0", DeviceOpenStatus::Success,
                  valid_snapshot(), {}});
  DeviceOpenOptions legacy_options;
  legacy_options.request_universal_planes = false;
  legacy_options.request_atomic = false;
  {
    auto device = Device::open(api, "/dev/dri/card0", legacy_options,
                               discovery);
    gw::test::require(device.has_value() && device->valid() && api.open(),
                      "eligible primary DRM device opens");
    gw::test::require(
        discovery.status == DeviceOpenStatus::Success &&
            device->snapshot().driver.name == "virtio_gpu" &&
            device->snapshot().driver.bus_info == "pci:0000:00:02.0" &&
            device->snapshot().device_major == 226 &&
            !device->snapshot().universal_planes &&
            !device->snapshot().atomic,
        "device identity and requested client capabilities are retained");
    gw::test::require(!api.last_options().request_universal_planes &&
                          !api.last_options().request_atomic,
                      "forced legacy discovery does not enable client caps");

    std::string error;
    const int duplicate = device->duplicate_fd(error);
    gw::test::require(duplicate >= 0 && error.empty(),
                      "device FD duplicates");
    gw::test::require((::fcntl(duplicate, F_GETFD) & FD_CLOEXEC) != 0,
                      "duplicated device FD is close-on-exec");
    (void)::close(duplicate);

    Device moved = std::move(*device);
    gw::test::require(moved.valid() && !device->valid(),
                      "DRM device ownership moves uniquely");
  }
  gw::test::require(!api.open() && api.close_count() == 1,
                    "DRM device closes exactly once through RAII");
  return 0;
}
