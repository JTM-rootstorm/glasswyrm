#pragma once

#include "ipc/wire/policy_contract.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace gw::ipc::wire {

enum class PolicyStackMode : std::uint16_t { None, Above, Below };

struct PolicyLifecycleWindowUpsert {
  PolicyWindowUpsert window;
  std::uint64_t geometry_serial{};
  std::uint64_t stack_serial{};
  std::uint32_t stack_sibling{};
  PolicyStackMode stack_mode{PolicyStackMode::None};
  std::uint32_t flags{};
};

struct SurfacePolicyUpsert {
  std::uint64_t surface_id{};
  std::uint32_t x11_window_id{};
  std::uint32_t workspace_id{};
  PolicyWindowType window_type{PolicyWindowType::Unknown};
  PolicyAppliedState applied_state{PolicyAppliedState::Normal};
  bool focused{};
  bool managed{};
  bool decoration_eligible{};
  bool override_redirect{};
  bool attention_requested{};
  std::uint16_t fullscreen_eligible{};
  std::uint16_t direct_scanout_eligible{};
  std::uint32_t flags{};
};

std::vector<std::uint8_t> encode(const PolicyLifecycleWindowUpsert &value);
CodecStatus decode(std::span<const std::uint8_t> bytes,
                   PolicyLifecycleWindowUpsert &value);
std::vector<std::uint8_t> encode(const SurfacePolicyUpsert &value);
CodecStatus decode(std::span<const std::uint8_t> bytes,
                   SurfacePolicyUpsert &value);

}  // namespace gw::ipc::wire
