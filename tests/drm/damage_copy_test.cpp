#include "backends/drm/damage_copy.hpp"
#include "tests/helpers/test_support.hpp"

#include <array>

int main() {
  using namespace glasswyrm::drm;
  using gw::compositor::Rectangle;

  DamageCopyHistory history(100, 80);
  const std::array first_damage{Rectangle{1, 2, 3, 4}};
  const auto first = history.plan(false, 0, 1, first_damage);
  gw::test::require(first.full_copy() &&
                        first.full_copy_reason == FullCopyReason::FirstUse &&
                        first.copied_bytes == 32'000 &&
                        first.rectangles ==
                            std::vector<Rectangle>{{0, 0, 100, 80}},
                    "first buffer use copies the complete frame");
  history.record_completed(first, 1);

  const std::array second_damage{Rectangle{10, 10, 5, 5}};
  const auto second = history.plan(false, 0, 2, second_damage);
  gw::test::require(second.full_copy() &&
                        second.full_copy_reason == FullCopyReason::FirstUse,
                    "second dumb buffer first use is also a full copy");
  history.record_completed(second, 2);

  const std::array third_damage{Rectangle{20, 20, 4, 4}};
  const auto third = history.plan(true, 1, 3, third_damage);
  gw::test::require(!third.full_copy() && third.history_span == 2 &&
                        third.copied_bytes == 164 &&
                        third.rectangles ==
                            std::vector<Rectangle>{{10, 10, 5, 5},
                                                   {20, 20, 4, 4}},
                    "alternating reuse unions all damage missing from buffer");
  history.record_completed(third, 3);

  const std::array clipped_damage{Rectangle{-2, -2, 4, 4}};
  const auto clipped = history.plan(true, 2, 4, clipped_damage);
  gw::test::require(!clipped.full_copy() && clipped.history_span == 2 &&
                        clipped.copied_bytes == 80 &&
                        clipped.rectangles ==
                            std::vector<Rectangle>{{0, 0, 2, 2},
                                                   {20, 20, 4, 4}},
                    "damage union clips and normalizes to output bounds");

  const auto unavailable = history.plan(true, 3, 4, {});
  gw::test::require(unavailable.full_copy() &&
                        unavailable.full_copy_reason ==
                            FullCopyReason::DamageUnavailable,
                    "missing current damage falls back to a complete copy");

  history.clear();
  const auto miss = history.plan(true, 1, 5, third_damage);
  gw::test::require(miss.full_copy() &&
                        miss.full_copy_reason == FullCopyReason::HistoryMiss,
                    "history uncertainty falls back to a complete copy");
  const auto resume = history.plan(
      true, 4, 5, third_damage, FullCopyReason::VirtualTerminalResume);
  gw::test::require(resume.full_copy() &&
                        resume.full_copy_reason ==
                            FullCopyReason::VirtualTerminalResume,
                    "VT resume explicitly forces a complete copy");
  return 0;
}
