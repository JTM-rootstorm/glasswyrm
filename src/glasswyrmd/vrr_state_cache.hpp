#pragma once

#include <glasswyrm/ipc.h>

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <vector>

namespace glasswyrm::server {

struct ServerVrrOutputState {
  gwipc_output_vrr_capability_upsert capability{};
  gwipc_output_vrr_policy_upsert policy{};
  std::optional<gwipc_policy_output_vrr_state> policy_result;
  std::optional<gwipc_output_vrr_state_upsert> compositor_state;
  std::optional<gwipc_presentation_timing> timing;
};

struct ServerVrrWindowState {
  gwipc_vrr_window_preference preference{GWIPC_VRR_PREFERENCE_DEFAULT};
  bool policy_candidate{true};
  std::optional<gwipc_policy_window_vrr_state> policy_result;
  std::optional<gwipc_surface_vrr_state> compositor_state;
};

struct VrrResponseExpectation {
  std::uint64_t commit_id{};
  std::uint64_t presented_generation{};
  std::set<std::uint64_t> output_ids;
  std::set<std::uint64_t> release_buffer_ids;
};

struct VrrResponseBatch {
  std::vector<gwipc_output_vrr_state_upsert> output_states;
  std::vector<gwipc_presentation_timing> timings;
  std::vector<std::uint64_t> released_buffer_ids;
  std::optional<gwipc_frame_acknowledged> acknowledgement;
};

enum class VrrResponseStatus {
  Accepted,
  NoExpectation,
  InvalidAcknowledgement,
  OutputCountMismatch,
  DuplicateOutput,
  UnknownOutput,
  InvalidOutputState,
  TimingCountMismatch,
  DuplicateTiming,
  InvalidTiming,
  ReleaseMismatch,
};

class VrrStateCache final {
 public:
  [[nodiscard]] std::uint64_t generation() const noexcept {
    return generation_;
  }
  [[nodiscard]] const std::map<std::uint64_t, ServerVrrOutputState>& outputs()
      const noexcept {
    return outputs_;
  }
  [[nodiscard]] const std::map<std::uint32_t, ServerVrrWindowState>& windows()
      const noexcept {
    return windows_;
  }
  [[nodiscard]] const VrrResponseExpectation* expectation() const noexcept {
    return expectation_ ? &*expectation_ : nullptr;
  }

  [[nodiscard]] bool replace_inventory(
      std::vector<gwipc_output_vrr_capability_upsert> capabilities,
      std::vector<gwipc_output_vrr_policy_upsert> policies);
  [[nodiscard]] bool set_policy(std::uint64_t output_id,
                                gwipc_vrr_policy_mode mode) noexcept;
  void set_window_preference(std::uint32_t window_id,
                             gwipc_vrr_window_preference preference);
  void set_window_policy_candidate(std::uint32_t window_id,
                                   bool policy_candidate);
  void erase_window(std::uint32_t window_id) noexcept;

  [[nodiscard]] bool stage_policy_result(
      std::uint64_t generation,
      const std::vector<gwipc_policy_output_vrr_state>& outputs,
      const std::vector<gwipc_policy_window_vrr_state>& windows);
  [[nodiscard]] bool stage_surface_states(
      const std::vector<gwipc_surface_vrr_state>& states);
  [[nodiscard]] bool seed_compositor_state(
      const std::vector<gwipc_output_vrr_state_upsert>& states,
      const std::vector<gwipc_presentation_timing>& timings);

  [[nodiscard]] bool expect_response(VrrResponseExpectation expectation);
  void cancel_expectation() noexcept { expectation_.reset(); }
  [[nodiscard]] VrrResponseStatus preflight(
      const VrrResponseBatch& batch) const noexcept;
  [[nodiscard]] VrrResponseStatus promote(const VrrResponseBatch& batch);

 private:
  std::uint64_t generation_{1};
  std::map<std::uint64_t, ServerVrrOutputState> outputs_;
  std::map<std::uint32_t, ServerVrrWindowState> windows_;
  std::optional<VrrResponseExpectation> expectation_;
};

[[nodiscard]] const char* vrr_response_status_name(
    VrrResponseStatus status) noexcept;

}  // namespace glasswyrm::server
