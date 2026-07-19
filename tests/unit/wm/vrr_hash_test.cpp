#include "wm/vrr_hash.hpp"

#include "tests/helpers/test_support.hpp"
#include "tests/unit/wm/vrr_test_fixture.hpp"

namespace {

using namespace glasswyrm::wm;
using glasswyrm::wm::test::fixture;
using gw::test::require;

void deterministic_v4_hash() {
  auto first = fixture();
  first.inputs.outputs.at(10).mode = VrrPolicyMode::Focused;
  first.inputs.windows.at(1001).output_membership = {20, 10};
  auto second = first;
  second.inputs.windows.at(1001).output_membership = {10, 20};
  const auto first_result =
      evaluate_vrr_policy(first.raw, first.base, first.inputs);
  const auto second_result =
      evaluate_vrr_policy(second.raw, second.base, second.inputs);
  require(first_result && second_result &&
              first_result.policy.hash == second_result.policy.hash,
          "v4 sorts auxiliary membership before hashing");
  require(first_result.policy.hash == UINT64_C(0x8a39efc1977a4efd),
          "canonical v4 policy hash matches its frozen vector");
}

void every_auxiliary_field_participates() {
  auto baseline = fixture();
  baseline.inputs.outputs.at(10).mode = VrrPolicyMode::Focused;
  const auto expected =
      evaluate_vrr_policy(baseline.raw, baseline.base, baseline.inputs);
  require(static_cast<bool>(expected), "canonical hash fixture evaluates");

  auto changed = baseline;
  changed.inputs.outputs.at(10).hardware_capable = false;
  require(evaluate_vrr_policy(changed.raw, changed.base, changed.inputs)
              .policy.hash != expected.policy.hash,
          "output capability inputs participate in v4");
  changed = baseline;
  changed.inputs.windows.at(1001).preference = VrrWindowPreference::Prefer;
  require(evaluate_vrr_policy(changed.raw, changed.base, changed.inputs)
              .policy.hash != expected.policy.hash,
          "window preferences participate in v4");
  changed = baseline;
  changed.base.windows.at(1001).focused = false;
  require(evaluate_vrr_policy(changed.raw, changed.base, changed.inputs)
              .policy.hash != expected.policy.hash,
          "window VRR result facts participate in v4");
  changed = baseline;
  changed.base.windows.at(1001).output_id = 20;
  changed.inputs.windows.at(1001).output_membership = {20};
  require(evaluate_vrr_policy(changed.raw, changed.base, changed.inputs)
              .policy.hash != expected.policy.hash,
          "selected output candidates participate in v4");
}

void historical_hashes_are_immutable_inputs() {
  auto value = fixture();
  constexpr auto v3 = UINT64_C(0x27dd02889862e36c);
  require(value.base.hash == v3, "fixture starts at the frozen v3 hash");
  const auto evaluated =
      evaluate_vrr_policy(value.raw, value.base, value.inputs);
  require(evaluated && value.base.hash == v3 &&
              evaluated.policy.base_policy_hash == v3,
          "v4 evaluation neither mutates nor replaces the v3 hash");

  PolicyState historical_v1;
  historical_v1.hash = UINT64_C(0x79ddf2e26c5784d8);
  VrrPolicyState empty_v4;
  empty_v4.base_policy_hash = historical_v1.hash;
  VrrInputs empty_inputs;
  const auto v4 = vrr_policy_hash(historical_v1, empty_inputs, empty_v4);
  require(historical_v1.hash == UINT64_C(0x79ddf2e26c5784d8) && v4 != 0 &&
              v4 != historical_v1.hash,
          "the separate v4 envelope leaves the frozen v1 hash unchanged");
}

}  // namespace

int main() {
  deterministic_v4_hash();
  every_auxiliary_field_participates();
  historical_hashes_are_immutable_inputs();
  return 0;
}
