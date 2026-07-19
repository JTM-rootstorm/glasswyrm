#ifndef GLASSWYRM_TESTS_UNIT_WM_VRR_TEST_FIXTURE_HPP
#define GLASSWYRM_TESTS_UNIT_WM_VRR_TEST_FIXTURE_HPP

#include "wm/vrr_policy.hpp"

namespace glasswyrm::wm::test {

struct VrrFixture {
  RawState raw;
  PolicyState base;
  VrrInputs inputs;
};

inline OutputContext output(const std::uint64_t id, const std::int32_t x,
                            const bool primary) {
  OutputContext value;
  value.output_id = id;
  value.logical = {x, 0, 800, 600};
  value.work = {x, 0, 800, primary ? 560U : 600U};
  value.enabled = true;
  value.primary = primary;
  return value;
}

inline RawWindow raw_window(const std::uint32_t id) {
  RawWindow value;
  value.window_id = id;
  value.parent_window_id = 1;
  value.requested_width = 800;
  value.requested_height = 600;
  value.window_type = WindowType::Normal;
  value.wants_map = true;
  value.decoration_preference = DecorationPreference::False;
  value.creation_serial = id;
  value.map_serial = id;
  return value;
}

inline WindowState policy_window(const std::uint32_t id,
                                 const std::uint64_t output_id) {
  WindowState value;
  value.window_id = id;
  value.output_id = output_id;
  value.final_x = output_id == 10 ? 0 : 800;
  value.final_width = 800;
  value.final_height = 600;
  value.window_type = WindowType::Normal;
  value.applied_state = AppliedState::Normal;
  value.visible = true;
  value.focused = true;
  value.managed = true;
  return value;
}

inline VrrFixture fixture() {
  VrrFixture value;
  value.raw.complete = true;
  value.raw.has_context = true;
  value.raw.context = {1, 1, 10, 0, 0, 1600, 600, 0};
  value.raw.outputs.emplace(10, output(10, 0, true));
  value.raw.outputs.emplace(20, output(20, 800, false));
  value.raw.windows.emplace(1001, raw_window(1001));

  value.base.generation = 42;
  value.base.hash = UINT64_C(0x27dd02889862e36c);
  value.base.context = value.raw.context;
  value.base.outputs = value.raw.outputs;
  value.base.windows.emplace(1001, policy_window(1001, 10));
  value.base.output_order.push_back(1001);

  value.inputs.complete = true;
  value.inputs.outputs.emplace(
      10, VrrOutputInput{10, VrrPolicyMode::Off, true, true, 0});
  value.inputs.outputs.emplace(
      20, VrrOutputInput{20, VrrPolicyMode::Off, true, true, 0});
  value.inputs.windows.emplace(
      1001, VrrWindowInput{1001, VrrWindowPreference::Default, {10}, 0});
  return value;
}

}  // namespace glasswyrm::wm::test

#endif
