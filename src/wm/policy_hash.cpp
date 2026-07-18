#include "wm/policy_engine.hpp"

#include "wm/policy_engine_internal.hpp"

#include <string_view>
#include <type_traits>

namespace glasswyrm::wm {
namespace {

void hash_byte(std::uint64_t& hash, const std::uint8_t value) noexcept {
  hash ^= value;
  hash *= UINT64_C(1099511628211);
}

template <class Value>
void hash_little(std::uint64_t& hash, Value value) noexcept {
  using Unsigned = std::make_unsigned_t<Value>;
  auto bits = static_cast<std::uint64_t>(static_cast<Unsigned>(value));
  for (std::size_t index = 0; index < sizeof(Value); ++index) {
    hash_byte(hash, static_cast<std::uint8_t>(bits));
    bits >>= 8U;
  }
}

template <class Value>
void write_little(std::array<std::uint8_t, 64>& bytes, std::size_t& offset,
                  Value value) noexcept {
  using Unsigned = std::make_unsigned_t<Value>;
  auto bits = static_cast<Unsigned>(value);
  for (std::size_t index = 0; index < sizeof(Value); ++index) {
    bytes[offset++] = static_cast<std::uint8_t>(bits);
    bits >>= 8U;
  }
}

}  // namespace

std::uint64_t interactive_policy_hash(
    const PolicyState& policy, const InteractiveBindings& bindings) noexcept {
  std::uint64_t hash = UINT64_C(14695981039346656037);
  constexpr std::string_view tag = "glasswyrm-policy-v2";
  for (const char byte : tag) hash_byte(hash, static_cast<std::uint8_t>(byte));
  hash_little(hash, policy.hash);
  hash_little(hash, bindings.move_modifiers);
  hash_little(hash, bindings.resize_modifiers);
  hash_little(hash, bindings.close_modifiers);
  hash_little(hash, bindings.move_button);
  hash_little(hash, bindings.resize_button);
  hash_little(hash, bindings.close_keysym);
  hash_little(hash, bindings.minimum_width);
  hash_little(hash, bindings.minimum_height);
  hash_little(hash, static_cast<std::uint8_t>(bindings.raise_on_focus));
  hash_little(hash,
              static_cast<std::uint8_t>(bindings.consume_wm_bindings));
  return hash;
}

std::array<std::uint8_t, 64> encode_policy_window_state(
    const WindowState& state) noexcept {
  std::array<std::uint8_t, 64> bytes{};
  std::size_t offset = 0;
  write_little(bytes, offset, state.window_id);
  write_little(bytes, offset, state.transient_for);
  write_little(bytes, offset, state.workspace_id);
  write_little(bytes, offset, UINT32_C(0));
  write_little(bytes, offset, state.output_id);
  write_little(bytes, offset, state.final_x);
  write_little(bytes, offset, state.final_y);
  write_little(bytes, offset, state.final_width);
  write_little(bytes, offset, state.final_height);
  write_little(bytes, offset, state.stacking);
  write_little(bytes, offset, static_cast<std::uint16_t>(state.window_type));
  write_little(bytes, offset, static_cast<std::uint16_t>(state.applied_state));
  bytes[offset++] = state.visible;
  bytes[offset++] = state.focused;
  bytes[offset++] = state.managed;
  bytes[offset++] = state.decoration_eligible;
  bytes[offset++] = state.override_redirect;
  bytes[offset++] = state.attention_requested;
  bytes[offset++] = static_cast<std::uint8_t>(state.fullscreen_eligible);
  bytes[offset++] = static_cast<std::uint8_t>(state.direct_scanout_eligible);
  write_little(bytes, offset, UINT32_C(0));
  write_little(bytes, offset, UINT32_C(0));
  return bytes;
}

namespace detail {

std::uint64_t policy_hash(const PolicyState& policy) noexcept {
  std::uint64_t hash = UINT64_C(14695981039346656037);
  constexpr std::string_view tag = "glasswyrm-policy-v1";
  for (const char byte : tag) hash_byte(hash, static_cast<std::uint8_t>(byte));
  hash_little(hash, policy.generation);
  const auto& context = policy.context;
  hash_little(hash, context.root_window_id);
  hash_little(hash, context.workspace_id);
  hash_little(hash, context.output_id);
  hash_little(hash, context.work_x);
  hash_little(hash, context.work_y);
  hash_little(hash, context.work_width);
  hash_little(hash, context.work_height);
  hash_little(hash, context.flags);
  for (const auto id : policy.output_order) {
    for (const auto byte : encode_policy_window_state(policy.windows.at(id)))
      hash_byte(hash, byte);
  }
  return hash;
}

}  // namespace detail
}  // namespace glasswyrm::wm
