#pragma once
#include "ipc/wire/types.hpp"
#include <cstdint>
#include <span>
#include <vector>
namespace gw::ipc::wire {
enum class PolicyWindowType : std::uint16_t {
  Unknown,
  Normal,
  Dialog,
  Utility
};
enum class PolicyMapIntent : std::uint16_t { Unmapped, WantsMap };
enum class PolicyAppliedState : std::uint16_t {
  Normal = 1,
  Maximized,
  Fullscreen,
  Minimized
};
enum class PolicyResult : std::uint16_t {
  Accepted = 1,
  RejectedIncompleteSnapshot,
  RejectedInvalidContext,
  RejectedInvalidWindow,
  RejectedUnknownReference,
  RejectedLimit,
  RejectedUnsupportedMetadata
};
struct PolicyContextUpsert {
  std::uint32_t root_window_id{}, workspace_id{};
  std::uint64_t output_id{};
  std::int32_t work_x{}, work_y{};
  std::uint32_t work_width{}, work_height{}, flags{};
};
struct PolicyWindowUpsert {
  std::uint32_t window_id{}, parent_window_id{}, transient_for{},
      workspace_id{};
  std::int32_t requested_x{}, requested_y{};
  std::uint32_t requested_width{}, requested_height{}, border_width{};
  PolicyWindowType window_type{};
  PolicyMapIntent map_intent{};
  bool override_redirect{};
  std::uint16_t decoration_preference{};
  bool fullscreen_requested{}, maximized_requested{}, minimized_requested{},
      attention_requested{};
  std::uint64_t creation_serial{}, map_serial{}, focus_serial{};
  std::uint32_t flags{};
};
struct PolicyWindowRemove {
  std::uint32_t window_id{};
};
struct PolicyCommit {
  std::uint64_t commit_id{}, producer_generation{};
  std::uint32_t flags{};
};
struct PolicyWindowState {
  std::uint32_t window_id{}, transient_for{}, workspace_id{};
  std::uint64_t output_id{};
  std::int32_t final_x{}, final_y{};
  std::uint32_t final_width{}, final_height{};
  std::int32_t stacking{};
  PolicyWindowType window_type{};
  PolicyAppliedState applied_state{PolicyAppliedState::Normal};
  bool visible{}, focused{}, managed{}, decoration_eligible{},
      override_redirect{}, attention_requested{};
  std::uint16_t fullscreen_eligible{}, direct_scanout_eligible{};
  std::uint32_t flags{};
};
struct PolicyAcknowledged {
  std::uint64_t commit_id{}, producer_generation{}, applied_generation{},
      policy_hash{};
  std::uint32_t window_count{};
  PolicyResult result{PolicyResult::Accepted};
};
struct PolicyBindingsUpsert {
  std::uint16_t move_modifiers{}, resize_modifiers{}, close_modifiers{};
  std::uint8_t move_button{}, resize_button{};
  std::uint32_t close_keysym{}, minimum_width{}, minimum_height{};
  bool raise_on_focus{}, consume_wm_bindings{};
};
#define GWIPC_POLICY_CODEC(T)                                                  \
  std::vector<std::uint8_t> encode(const T &);                                 \
  CodecStatus decode(std::span<const std::uint8_t>, T &)
GWIPC_POLICY_CODEC(PolicyContextUpsert);
GWIPC_POLICY_CODEC(PolicyWindowUpsert);
GWIPC_POLICY_CODEC(PolicyWindowRemove);
GWIPC_POLICY_CODEC(PolicyCommit);
GWIPC_POLICY_CODEC(PolicyWindowState);
GWIPC_POLICY_CODEC(PolicyAcknowledged);
GWIPC_POLICY_CODEC(PolicyBindingsUpsert);
#undef GWIPC_POLICY_CODEC
} // namespace gw::ipc::wire
