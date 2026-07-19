#include "backends/drm/fake_drm_api.hpp"
#include "backends/drm/vrr_timing.hpp"
#include "tests/helpers/test_support.hpp"

#include <limits>
#include <memory>
#include <poll.h>
#include <string>

int main() {
  using namespace glasswyrm::drm;
  const auto exact = convert_page_flip_timestamp(12, 345'678);
  gw::test::require(exact.status == VrrTimestampStatus::Success &&
                        exact.nanoseconds == 12'345'678'000ULL,
                    "kernel seconds and microseconds convert exactly to ns");
  gw::test::require(convert_page_flip_timestamp(0, 1'000'000).status ==
                        VrrTimestampStatus::InvalidMicroseconds,
                    "invalid kernel microseconds are rejected");
  gw::test::require(
      convert_page_flip_timestamp(std::numeric_limits<std::uint64_t>::max(), 0)
              .status == VrrTimestampStatus::ArithmeticOverflow,
      "timestamp multiplication overflow is rejected");
  gw::test::require(
      convert_page_flip_timestamp(1, 0, 1'000'000'001ULL).status ==
          VrrTimestampStatus::Regression,
      "timestamp regression is rejected");

  DeviceSnapshot snapshot;
  snapshot.primary_node = true;
  snapshot.dumb_buffer = true;
  snapshot.timestamp_monotonic = true;
  snapshot.connectors.resize(3);
  snapshot.connectors[1].vrr_property_present = true;
  snapshot.connectors[2].vrr_property_present = true;
  snapshot.connectors[2].vrr_capable = true;
  FakeDrmApi api({"/dev/dri/card0", DeviceOpenStatus::Success, snapshot, {}});
  const auto opened = api.open_device("/dev/dri/card0", {});
  gw::test::require(opened.status == DeviceOpenStatus::Success &&
                        opened.snapshot.timestamp_monotonic,
                    "fake device retains monotonic timestamp capability");
  gw::test::require(
      !opened.snapshot.connectors[0].vrr_property_present &&
          opened.snapshot.connectors[1].vrr_property_present &&
          !opened.snapshot.connectors[1].vrr_capable &&
          opened.snapshot.connectors[2].vrr_property_present &&
          opened.snapshot.connectors[2].vrr_capable,
      "fake device models absent, false, and true connector capability");
  auto first = std::make_shared<PageFlipCookie>(1);
  std::string error;
  gw::test::require(api.arm_page_flip(opened.handle, first, error),
                    "timed fake page flip arms");
  api.queue_page_flip(9, 40, 7, 2'000'000'000ULL, true);
  const auto event = api.service_events(opened.handle, POLLIN);
  gw::test::require(event.kind == DrmEventKind::PageFlip &&
                        event.kernel_timestamp_nanoseconds ==
                            2'000'000'000ULL &&
                        event.timestamp_available &&
                        first->kernel_timestamp_nanoseconds ==
                            event.kernel_timestamp_nanoseconds &&
                        first->timestamp_available,
                    "timed fake completion populates event and cookie");

  auto regressed = std::make_shared<PageFlipCookie>(2);
  gw::test::require(api.arm_page_flip(opened.handle, regressed, error),
                    "second timed fake page flip arms");
  api.queue_page_flip(10, 40, 8, 1'999'999'999ULL, true);
  gw::test::require(api.service_events(opened.handle, POLLIN).kind ==
                        DrmEventKind::Error,
                    "fake DRM rejects regressing timed events");
  auto abandoned = std::make_shared<PageFlipCookie>(3);
  gw::test::require(api.arm_page_flip(opened.handle, abandoned, error),
                    "abandoned timed fake page flip arms");
  api.abandon_page_flip(opened.handle, abandoned);
  api.queue_page_flip(11, 40, 9, 1'999'999'998ULL, true);
  gw::test::require(api.service_events(opened.handle, POLLIN).kind ==
                        DrmEventKind::None,
                    "late abandoned timestamp regression is consumed");
  api.close_device(opened.handle);
  return 0;
}
