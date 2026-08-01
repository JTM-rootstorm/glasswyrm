#include "backends/drm/fake_drm_api.hpp"
#include "backends/drm/vrr_timing.hpp"
#include "tests/helpers/test_support.hpp"

#include <array>
#include <limits>
#include <memory>
#include <poll.h>
#include <string>

int main() {
  using namespace glasswyrm::drm;
  const std::array crtc_bindings{
      CrtcIndexBinding{40, 0},
      CrtcIndexBinding{900, 1},
      CrtcIndexBinding{12, 31},
  };
  gw::test::require(
      find_crtc_index(crtc_bindings, 40) == 0 &&
          find_crtc_index(crtc_bindings, 900) == 1 &&
          find_crtc_index(crtc_bindings, 12) == 31 &&
          !find_crtc_index(crtc_bindings, 1),
      "legacy vblank lookup maps object IDs to resource indices");
  gw::test::require(
      legacy_vblank_crtc_selector(0) == 0 &&
          legacy_vblank_crtc_selector(1) == 2 &&
          legacy_vblank_crtc_selector(31) == 62 &&
          !legacy_vblank_crtc_selector(32),
      "legacy vblank selector encodes only supported CRTC indices");

  auto extended =
      extend_legacy_vblank_counter(UINT32_C(0xfffffffe));
  gw::test::require(
      extended.status == LegacyVBlankCounterStatus::Success &&
          extended.state.raw_sequence == UINT32_C(0xfffffffe) &&
          extended.state.extended_sequence == UINT64_C(0xfffffffe),
      "legacy vblank counter initializes without inventing an epoch");
  extended =
      extend_legacy_vblank_counter(UINT32_C(0xffffffff), extended.state);
  gw::test::require(
      extended.status == LegacyVBlankCounterStatus::Success &&
          extended.state.extended_sequence == UINT64_C(0xffffffff),
      "legacy vblank counter advances before wrap");
  extended = extend_legacy_vblank_counter(0, extended.state);
  gw::test::require(
      extended.status == LegacyVBlankCounterStatus::Success &&
          extended.state.extended_sequence == UINT64_C(0x100000000),
      "legacy vblank counter extends a 32-bit wrap");
  extended = extend_legacy_vblank_counter(1, extended.state);
  gw::test::require(
      extended.status == LegacyVBlankCounterStatus::Success &&
          extended.state.extended_sequence == UINT64_C(0x100000001),
      "legacy vblank counter advances after wrap");
  const auto duplicate =
      extend_legacy_vblank_counter(1, extended.state);
  gw::test::require(
      duplicate.status == LegacyVBlankCounterStatus::Duplicate &&
          duplicate.state.extended_sequence ==
              extended.state.extended_sequence,
      "duplicate legacy vblank counter fails closed");
  const auto regression = extend_legacy_vblank_counter(
      0, LegacyVBlankCounterState{1, UINT64_C(0x100000001)});
  gw::test::require(
      regression.status == LegacyVBlankCounterStatus::Regression,
      "backward legacy vblank counter fails closed");
  const auto ambiguous = extend_legacy_vblank_counter(
      UINT32_C(0x80000000), LegacyVBlankCounterState{0, 10});
  gw::test::require(
      ambiguous.status == LegacyVBlankCounterStatus::Regression,
      "half-range legacy vblank counter change is rejected");
  const auto counter_overflow = extend_legacy_vblank_counter(
      1, LegacyVBlankCounterState{
             0, std::numeric_limits<std::uint64_t>::max()});
  gw::test::require(
      counter_overflow.status ==
          LegacyVBlankCounterStatus::ArithmeticOverflow,
      "legacy vblank counter arithmetic overflow fails closed");
  const auto reset_counter = extend_legacy_vblank_counter(7);
  gw::test::require(
      reset_counter.status == LegacyVBlankCounterStatus::Success &&
          reset_counter.state.extended_sequence == 7,
      "clearing the legacy baseline starts a new scanout period");

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
  gw::test::require(
      page_flip_timestamp_advances(100, std::nullopt) &&
          !page_flip_timestamp_advances(90, 100) &&
          !page_flip_timestamp_advances(95, 100) &&
          !page_flip_timestamp_advances(100, 100) &&
          page_flip_timestamp_advances(101, 100),
      "page-flip timestamp high-water never regresses after a bad sample");

  const auto query_failed =
      assess_crtc_sequence_sample(2'000'000'000ULL, true, false, true, 0, 0);
  gw::test::require(
      query_failed.source == VrrTimingSource::CrtcSequenceQuery &&
          query_failed.correlation == CrtcSequenceCorrelation::QueryFailed &&
          !query_failed.cadence_eligible,
      "failed CRTC query remains separately tagged and ineligible");
  const auto legacy_query_failed = assess_crtc_sequence_sample(
      2'000'000'000ULL, true, false, true, 0, 0, std::nullopt,
      VrrTimingSource::LegacyVBlankQuery);
  gw::test::require(
      legacy_query_failed.source == VrrTimingSource::LegacyVBlankQuery &&
          legacy_query_failed.correlation ==
              CrtcSequenceCorrelation::QueryFailed &&
          !legacy_query_failed.cadence_eligible,
      "failed legacy vblank query remains separately tagged and ineligible");
  const auto invalid_query = assess_crtc_sequence_sample(
      2'000'000'000ULL, true, true, true, 0, 2'000'000'000ULL);
  gw::test::require(invalid_query.correlation ==
                            CrtcSequenceCorrelation::Invalid &&
                        !invalid_query.cadence_eligible,
                    "zero CRTC sequence cannot become cadence evidence");
  const auto nonmonotonic_clock = assess_crtc_sequence_sample(
      2'000'000'000ULL, true, true, false, 9, 2'000'000'000ULL);
  gw::test::require(nonmonotonic_clock.correlation ==
                            CrtcSequenceCorrelation::Invalid &&
                        !nonmonotonic_clock.cadence_eligible,
                    "a non-monotonic timestamp clock fails closed");
  const auto late_query = assess_crtc_sequence_sample(
      2'000'000'000ULL, true, true, true, 9, 2'000'001'000ULL);
  gw::test::require(late_query.correlation ==
                            CrtcSequenceCorrelation::Uncorrelated &&
                        !late_query.cadence_eligible,
                    "a query from a later vblank cannot replace event timing");
  const auto correlated_query = assess_crtc_sequence_sample(
      2'000'000'000ULL, true, true, true, 0x1'0000'0001ULL, 2'000'000'999ULL);
  gw::test::require(
      correlated_query.correlation == CrtcSequenceCorrelation::Correlated &&
          correlated_query.sequence == 0x1'0000'0001ULL &&
          correlated_query.timestamp_nanoseconds == 2'000'000'999ULL &&
          correlated_query.cadence_eligible,
      "a tightly correlated query retains its full 64-bit CRTC sequence");
  const auto nonmonotonic_query = assess_crtc_sequence_sample(
      2'100'000'000ULL, true, true, true, 0x1'0000'0001ULL, 2'100'000'000ULL,
      CrtcSequencePoint{0x1'0000'0001ULL, 2'000'000'999ULL});
  gw::test::require(nonmonotonic_query.correlation ==
                            CrtcSequenceCorrelation::NonMonotonic &&
                        !nonmonotonic_query.cadence_eligible,
                    "non-increasing CRTC sequence fails closed");
  const auto next_query = assess_crtc_sequence_sample(
      2'200'000'000ULL, true, true, true, 0x1'0000'0002ULL, 2'200'000'000ULL,
      CrtcSequencePoint{0x1'0000'0001ULL, 2'100'000'000ULL});
  gw::test::require(
      next_query.correlation == CrtcSequenceCorrelation::Correlated &&
          next_query.cadence_eligible,
      "increasing correlated CRTC sequence remains cadence eligible");
  const auto legacy_query = assess_crtc_sequence_sample(
      2'300'000'000ULL, true, true, true, 0x1'0000'0003ULL, 2'300'000'000ULL,
      CrtcSequencePoint{0x1'0000'0002ULL, 2'200'000'000ULL},
      VrrTimingSource::LegacyVBlankQuery);
  gw::test::require(
      legacy_query.source == VrrTimingSource::LegacyVBlankQuery &&
          legacy_query.correlation ==
              CrtcSequenceCorrelation::Correlated &&
          legacy_query.cadence_eligible,
      "legacy vblank query remains separately source-tagged");

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
  const auto regressed_event = api.service_events(opened.handle, POLLIN);
  gw::test::require(regressed_event.kind == DrmEventKind::PageFlip &&
                        regressed_event.token == 2 &&
                        !regressed_event.timestamp_available &&
                        regressed->completed && regressed->timestamp_invalid,
                    "timestamp regression preserves completed page-flip truth");
  auto still_regressed = std::make_shared<PageFlipCookie>(3);
  gw::test::require(api.arm_page_flip(opened.handle, still_regressed, error),
                    "third timed fake page flip arms");
  api.queue_page_flip(11, 40, 9, 1'999'999'999ULL, true);
  const auto still_regressed_event = api.service_events(opened.handle, POLLIN);
  gw::test::require(
      still_regressed_event.kind == DrmEventKind::PageFlip &&
          still_regressed_event.token == 3 &&
          !still_regressed_event.timestamp_available &&
          still_regressed->completed && still_regressed->timestamp_invalid,
      "a bad sample cannot lower the fake page-flip timestamp high-water");
  auto recovered = std::make_shared<PageFlipCookie>(4);
  gw::test::require(api.arm_page_flip(opened.handle, recovered, error),
                    "recovered timed fake page flip arms");
  api.queue_page_flip(12, 40, 10, 2'000'000'001ULL, true);
  const auto recovered_event = api.service_events(opened.handle, POLLIN);
  gw::test::require(recovered_event.kind == DrmEventKind::PageFlip &&
                        recovered_event.token == 4 &&
                        recovered_event.timestamp_available &&
                        recovered->timestamp_available,
                    "a timestamp above the valid high-water recovers timing");
  auto abandoned = std::make_shared<PageFlipCookie>(5);
  gw::test::require(api.arm_page_flip(opened.handle, abandoned, error),
                    "abandoned timed fake page flip arms");
  api.abandon_page_flip(opened.handle, abandoned);
  api.queue_page_flip(13, 40, 11, 1'999'999'998ULL, true);
  gw::test::require(api.service_events(opened.handle, POLLIN).kind ==
                        DrmEventKind::None,
                    "late abandoned timestamp regression is consumed");
  api.close_device(opened.handle);
  return 0;
}
