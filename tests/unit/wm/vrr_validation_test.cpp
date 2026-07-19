#include "wm/vrr_validation.hpp"

#include "tests/helpers/test_support.hpp"
#include "tests/unit/wm/vrr_test_fixture.hpp"

namespace {

using namespace glasswyrm::wm;
using glasswyrm::wm::test::fixture;
using gw::test::require;

void complete_snapshot_rules() {
  auto value = fixture();
  require(validate_vrr_inputs(value.raw, value.base, value.inputs) ==
              VrrEvaluationError::None,
          "canonical complete inputs validate");
  value.inputs.complete = false;
  require(validate_vrr_inputs(value.raw, value.base, value.inputs) ==
              VrrEvaluationError::IncompleteSnapshot,
          "an incomplete auxiliary snapshot is rejected");

  value = fixture();
  value.inputs.outputs.erase(20);
  require(validate_vrr_inputs(value.raw, value.base, value.inputs) ==
              VrrEvaluationError::InvalidOutput,
          "one output input is required per base output");
  value = fixture();
  value.inputs.windows.clear();
  require(validate_vrr_inputs(value.raw, value.base, value.inputs) ==
              VrrEvaluationError::InvalidWindow,
          "one preference input is required per managed window");
}

void exact_identifiers_and_enums() {
  auto value = fixture();
  value.inputs.outputs.at(10).output_id = 20;
  require(validate_vrr_inputs(value.raw, value.base, value.inputs) ==
              VrrEvaluationError::InvalidOutput,
          "output map keys and record IDs must agree");
  value = fixture();
  value.inputs.outputs.at(10).mode = static_cast<VrrPolicyMode>(0);
  require(validate_vrr_inputs(value.raw, value.base, value.inputs) ==
              VrrEvaluationError::InvalidOutput,
          "unknown output policy modes are rejected");
  value = fixture();
  value.inputs.windows.at(1001).preference =
      static_cast<VrrWindowPreference>(4);
  require(validate_vrr_inputs(value.raw, value.base, value.inputs) ==
              VrrEvaluationError::InvalidWindow,
          "unknown window preferences are rejected");
  value = fixture();
  value.inputs.windows.at(1001).flags = 1;
  require(validate_vrr_inputs(value.raw, value.base, value.inputs) ==
              VrrEvaluationError::InvalidWindow,
          "window input flags remain reserved");
}

void membership_rules() {
  auto value = fixture();
  value.inputs.windows.at(1001).output_membership = {10, 10};
  require(validate_vrr_inputs(value.raw, value.base, value.inputs) ==
              VrrEvaluationError::InvalidWindow,
          "duplicate membership IDs are rejected");
  value = fixture();
  value.inputs.windows.at(1001).output_membership = {99};
  require(validate_vrr_inputs(value.raw, value.base, value.inputs) ==
              VrrEvaluationError::UnknownReference,
          "unknown membership outputs are rejected");
  value = fixture();
  value.base.outputs.at(20).enabled = false;
  value.raw.outputs.at(20).enabled = false;
  value.inputs.windows.at(1001).output_membership = {20};
  require(validate_vrr_inputs(value.raw, value.base, value.inputs) ==
              VrrEvaluationError::UnknownReference,
          "disabled outputs cannot appear in surface membership");
  value = fixture();
  value.inputs.windows.at(1001).output_membership = {20, 10};
  require(validate_vrr_inputs(value.raw, value.base, value.inputs) ==
              VrrEvaluationError::None,
          "unique membership order is not a validation constraint");
}

void base_policy_alignment() {
  auto value = fixture();
  value.base.generation = 0;
  require(validate_vrr_inputs(value.raw, value.base, value.inputs) ==
              VrrEvaluationError::BasePolicyMismatch,
          "VRR policy must follow a committed base generation");
  value = fixture();
  value.base.windows.at(1001).output_id = 99;
  require(validate_vrr_inputs(value.raw, value.base, value.inputs) ==
              VrrEvaluationError::BasePolicyMismatch,
          "assigned outputs must exist in both base and VRR inputs");
  value = fixture();
  auto second = test::raw_window(1002);
  value.raw.windows.emplace(1002, second);
  auto second_policy = test::policy_window(1002, 20);
  second_policy.focused = true;
  value.base.windows.emplace(1002, second_policy);
  value.inputs.windows.emplace(
      1002, VrrWindowInput{1002, VrrWindowPreference::Default, {20}, 0});
  require(validate_vrr_inputs(value.raw, value.base, value.inputs) ==
              VrrEvaluationError::BasePolicyMismatch,
          "multiple focused windows cannot enter deterministic selection");
  value = fixture();
  value.raw.windows.at(1001).override_redirect = true;
  value.base.windows.at(1001).managed = false;
  value.base.windows.at(1001).override_redirect = true;
  require(validate_vrr_inputs(value.raw, value.base, value.inputs) ==
              VrrEvaluationError::InvalidWindow,
          "override-redirect windows have no VRR preference input");
}

}  // namespace

int main() {
  complete_snapshot_rules();
  exact_identifiers_and_enums();
  membership_rules();
  base_policy_alignment();
  return 0;
}
