#pragma once

#include <glasswyrm/ipc.h>

#include <cstdint>
#include <map>
#include <string>

namespace gw::compositor {

struct SceneVrrState {
  std::map<std::uint64_t, gwipc_output_vrr_policy_upsert> output_policies;
  std::map<std::uint64_t, gwipc_surface_vrr_state> surfaces;
  std::uint64_t policy_generation{};
};

// VRR-visible state is promoted only after presentation succeeds. Keeping the
// effective and timing records together makes a rejected presenter operation
// unable to advance one half of the externally visible result.
class CommittedVrrState final {
public:
  struct Output {
    gwipc_output_vrr_state_upsert state;
    gwipc_presentation_timing timing;
  };
  using OutputMap = std::map<std::uint64_t, Output>;

  [[nodiscard]] bool promote(OutputMap outputs,
                             std::uint64_t commit_id,
                             std::uint64_t presented_generation,
                             std::string& error);
  void clear() noexcept;

  [[nodiscard]] const OutputMap& outputs() const noexcept {
    return outputs_;
  }

private:
  OutputMap outputs_;
};

[[nodiscard]] bool
valid_output_vrr_policy(const gwipc_output_vrr_policy_upsert& value) noexcept;
[[nodiscard]] bool
valid_surface_vrr_state(const gwipc_surface_vrr_state& value) noexcept;

} // namespace gw::compositor
