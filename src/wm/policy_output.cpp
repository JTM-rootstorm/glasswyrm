#include "wm/policy_engine_internal.hpp"

namespace glasswyrm::wm::detail {

void assign_outputs(const RawState& raw, PolicyState& policy) {
  for (auto& [id, state] : policy.windows) {
    (void)id;
    state.output_id = raw.context.output_id;
  }
}

}  // namespace glasswyrm::wm::detail
