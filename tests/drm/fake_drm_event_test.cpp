#include "backends/drm/device.hpp"
#include "backends/drm/fake_drm_api.hpp"

#include "tests/helpers/test_support.hpp"

#include <poll.h>
#include <string>

namespace {

glasswyrm::drm::DeviceSnapshot valid_snapshot() {
  using namespace glasswyrm::drm;
  DeviceSnapshot snapshot;
  snapshot.canonical_path = "/dev/dri/card0";
  snapshot.primary_node = true;
  snapshot.dumb_buffer = true;
  snapshot.crtcs.push_back({40, 0, {10}});
  snapshot.connectors.push_back(
      {10,
       static_cast<std::uint32_t>(ConnectorType::Virtual),
       1,
       ConnectionStatus::Connected,
       {},
       false,
       1,
       40,
       0,
       0,
       {}});
  return snapshot;
}

} // namespace

int main() {
  using namespace glasswyrm::drm;
  FakeDrmApi api(
      {"/dev/dri/card0", DeviceOpenStatus::Success, valid_snapshot(), {}});
  DeviceDiscovery discovery;
  auto device = Device::open(api, "/dev/dri/card0", {}, discovery);
  gw::test::require(device.has_value(), "fake DRM device opens");

  pollfd descriptor{device->poll_fd(), POLLIN, 0};
  gw::test::require(::poll(&descriptor, 1, 0) == 0,
                    "fake event FD begins idle");
  auto cookie = std::make_shared<PageFlipCookie>(77);
  std::string error;
  gw::test::require(device->arm_page_flip(cookie, error),
                    "stable page-flip cookie arms");
  auto competing = std::make_shared<PageFlipCookie>(88);
  gw::test::require(!device->arm_page_flip(competing, error),
                    "one page flip may be armed at a time");
  api.queue_page_flip(999, 40, 12);
  descriptor.revents = 0;
  gw::test::require(::poll(&descriptor, 1, 0) == 1 &&
                        (descriptor.revents & POLLIN) != 0,
                    "queued fake page flip wakes the poll path");
  const auto flip = device->service_events(descriptor.revents);
  gw::test::require(flip.kind == DrmEventKind::PageFlip && flip.token == 77 &&
                        flip.crtc_id == 40 && flip.sequence == 12 &&
                        cookie->completed && cookie->completed_crtc_id == 40 &&
                        cookie->completed_sequence == 12,
                    "fake page-flip identity survives event service");

  auto abandoned = std::make_shared<PageFlipCookie>(78);
  std::weak_ptr<PageFlipCookie> abandoned_lifetime = abandoned;
  gw::test::require(device->arm_page_flip(abandoned, error),
                    "submitted page-flip callback record arms");
  device->abandon_page_flip(abandoned);
  abandoned.reset();
  gw::test::require(!abandoned_lifetime.expired(),
                    "abandoned callback storage remains alive");
  api.queue_page_flip(78, 40, 13);
  gw::test::require(
      device->service_events(POLLIN).kind == DrmEventKind::None &&
          abandoned_lifetime.expired(),
      "late abandoned completion is consumed without publication");

  api.queue_page_flip(79, 40, 14);
  api.queue_error("injected event failure");
  const auto second = device->service_events(POLLIN);
  const auto failure = device->service_events(POLLIN);
  gw::test::require(second.kind == DrmEventKind::PageFlip &&
                        second.token == 79 &&
                        failure.kind == DrmEventKind::Error &&
                        failure.error == "injected event failure",
                    "fake event queue preserves deterministic ordering");
  gw::test::require(device->service_events(0).kind == DrmEventKind::None,
                    "servicing without readiness is a no-op");
  gw::test::require(device->service_events(POLLHUP).kind == DrmEventKind::Error,
                    "event FD hangup is fatal");

  auto close_abandoned = std::make_shared<PageFlipCookie>(80);
  std::weak_ptr<PageFlipCookie> close_lifetime = close_abandoned;
  gw::test::require(device->arm_page_flip(close_abandoned, error),
                    "close-path callback record arms");
  device->abandon_page_flip(close_abandoned);
  close_abandoned.reset();
  device->reset();
  gw::test::require(close_lifetime.expired() && device->poll_fd() == -1 &&
                        device->service_events(POLLIN).kind ==
                            DrmEventKind::Error,
                    "device close releases abandoned callback storage");
  return 0;
}
