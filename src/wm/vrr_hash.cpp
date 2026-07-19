#include "wm/vrr_hash.hpp"

#include <algorithm>
#include <string_view>
#include <type_traits>

namespace glasswyrm::wm {
namespace {

void hash_byte(std::uint64_t& hash, const std::uint8_t value) noexcept {
  hash ^= value;
  hash *= UINT64_C(1099511628211);
}

template <typename Value>
void hash_little(std::uint64_t& hash, const Value value) noexcept {
  using Unsigned = std::make_unsigned_t<Value>;
  auto bits = static_cast<std::uint64_t>(static_cast<Unsigned>(value));
  for (std::size_t index = 0; index < sizeof(Value); ++index) {
    hash_byte(hash, static_cast<std::uint8_t>(bits));
    bits >>= 8U;
  }
}

void hash_output_inputs(std::uint64_t& hash,
                        const VrrInputs& inputs) noexcept {
  hash_little(hash, static_cast<std::uint32_t>(inputs.outputs.size()));
  for (const auto& [id, output] : inputs.outputs) {
    (void)id;
    hash_little(hash, output.output_id);
    hash_little(hash, static_cast<std::uint16_t>(output.mode));
    hash_little(hash, static_cast<std::uint8_t>(output.hardware_capable));
    hash_little(hash, static_cast<std::uint8_t>(output.kms_controllable));
    hash_little(hash, output.flags);
  }
}

void hash_window_inputs(std::uint64_t& hash,
                        const VrrInputs& inputs) noexcept {
  hash_little(hash, static_cast<std::uint32_t>(inputs.windows.size()));
  for (const auto& [id, window] : inputs.windows) {
    (void)id;
    hash_little(hash, window.window_id);
    hash_little(hash, static_cast<std::uint16_t>(window.preference));
    auto membership = window.output_membership;
    std::sort(membership.begin(), membership.end());
    hash_little(hash, static_cast<std::uint32_t>(membership.size()));
    for (const auto output_id : membership)
      hash_little(hash, output_id);
    hash_little(hash, window.flags);
  }
}

void hash_output_results(std::uint64_t& hash,
                         const VrrPolicyState& policy) noexcept {
  hash_little(hash, static_cast<std::uint32_t>(policy.outputs.size()));
  for (const auto& [id, output] : policy.outputs) {
    (void)id;
    hash_little(hash, output.output_id);
    hash_little(hash, static_cast<std::uint16_t>(output.mode));
    hash_little(hash, output.selected_window_id);
    hash_little(hash, static_cast<std::uint8_t>(output.desired_enabled));
    hash_little(hash, static_cast<std::uint8_t>(output.candidate_required));
    hash_little(hash, output.reason_flags);
    hash_little(hash, output.flags);
  }
}

void hash_window_results(std::uint64_t& hash,
                         const VrrPolicyState& policy) noexcept {
  hash_little(hash, static_cast<std::uint32_t>(policy.windows.size()));
  for (const auto& [id, window] : policy.windows) {
    (void)id;
    hash_little(hash, window.window_id);
    hash_little(hash, window.output_id);
    hash_little(hash, static_cast<std::uint16_t>(window.preference));
    hash_little(hash, static_cast<std::uint8_t>(window.selected));
    hash_little(hash, static_cast<std::uint8_t>(window.eligible));
    hash_little(hash, static_cast<std::uint8_t>(window.focused));
    hash_little(hash, static_cast<std::uint8_t>(window.fullscreen));
    hash_little(hash,
                static_cast<std::uint8_t>(window.borderless_fullscreen));
    hash_little(
        hash, static_cast<std::uint8_t>(window.exclusive_output_membership));
    hash_little(hash, window.reason_flags);
    hash_little(hash, window.flags);
  }
}

}  // namespace

std::uint64_t vrr_policy_hash(const PolicyState& base,
                              const VrrInputs& inputs,
                              const VrrPolicyState& policy) noexcept {
  std::uint64_t hash = UINT64_C(14695981039346656037);
  for (const char byte : std::string_view{"glasswyrm-policy-v4"})
    hash_byte(hash, static_cast<std::uint8_t>(byte));
  hash_little(hash, base.hash);
  hash_output_inputs(hash, inputs);
  hash_window_inputs(hash, inputs);
  hash_output_results(hash, policy);
  hash_window_results(hash, policy);
  return hash;
}

}  // namespace glasswyrm::wm
