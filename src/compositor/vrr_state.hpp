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
  using OutputStateMap =
      std::map<std::uint64_t, gwipc_output_vrr_state_upsert>;
  using TimingMap = std::map<std::uint64_t, gwipc_presentation_timing>;

  [[nodiscard]] bool promote(OutputStateMap states, TimingMap timings,
                             std::uint64_t commit_id,
                             std::uint64_t presented_generation,
                             std::string& error);
  void clear() noexcept;

  [[nodiscard]] const OutputStateMap& outputs() const noexcept {
    return outputs_;
  }
  [[nodiscard]] const TimingMap& timings() const noexcept { return timings_; }

private:
  OutputStateMap outputs_;
  TimingMap timings_;
};

[[nodiscard]] bool
valid_output_vrr_policy(const gwipc_output_vrr_policy_upsert& value) noexcept;
[[nodiscard]] bool
valid_surface_vrr_state(const gwipc_surface_vrr_state& value) noexcept;

} // namespace gw::compositor
