#ifndef GLASSWYRM_IPC_VRR_MEMBERSHIP_HINT_HPP
#define GLASSWYRM_IPC_VRR_MEMBERSHIP_HINT_HPP

#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace glasswyrm::ipc::internal {

// M14 needs to carry an exact surface-output membership through the existing
// policy hint record without changing its public wire layout.  When the VRR
// policy capability is negotiated, preferred_output_id is this tagged bitmap;
// bit N names canonical (ascending) output N.  Historical profiles continue
// to interpret preferred_output_id as an ordinary output ID.
inline constexpr std::uint64_t kVrrMembershipHintTag =
    UINT64_C(0x8000475752521400);
inline constexpr std::uint64_t kVrrMembershipHintTagMask =
    UINT64_C(0xffffffffffffff00);
inline constexpr std::size_t kVrrMembershipHintMaximumOutputs = 8;

[[nodiscard]] inline bool valid_vrr_membership_output_order(
    const std::span<const std::uint64_t> output_ids) noexcept {
  if (output_ids.empty() ||
      output_ids.size() > kVrrMembershipHintMaximumOutputs)
    return false;
  for (std::size_t index = 0; index < output_ids.size(); ++index)
    if (output_ids[index] == 0 ||
        (index != 0 && output_ids[index - 1] >= output_ids[index]))
      return false;
  return true;
}

[[nodiscard]] inline std::optional<std::uint64_t>
encode_vrr_membership_hint(
    const std::span<const std::uint64_t> canonical_output_ids,
    const std::span<const std::uint64_t> membership) noexcept {
  if (!valid_vrr_membership_output_order(canonical_output_ids))
    return std::nullopt;
  std::uint8_t bits = 0;
  std::uint64_t previous = 0;
  for (const auto output_id : membership) {
    if (output_id == 0 || output_id <= previous) return std::nullopt;
    previous = output_id;
    const auto found = std::ranges::lower_bound(canonical_output_ids,
                                                 output_id);
    if (found == canonical_output_ids.end() || *found != output_id)
      return std::nullopt;
    const auto index = static_cast<std::size_t>(
        std::distance(canonical_output_ids.begin(), found));
    bits = static_cast<std::uint8_t>(bits | (UINT8_C(1) << index));
  }
  return kVrrMembershipHintTag | bits;
}

[[nodiscard]] inline std::optional<std::vector<std::uint64_t>>
decode_vrr_membership_hint(
    const std::span<const std::uint64_t> canonical_output_ids,
    const std::uint64_t encoded) {
  if (!valid_vrr_membership_output_order(canonical_output_ids) ||
      (encoded & kVrrMembershipHintTagMask) != kVrrMembershipHintTag)
    return std::nullopt;
  const auto bits = static_cast<std::uint8_t>(encoded);
  const auto valid_bits = static_cast<std::uint8_t>(
      (UINT16_C(1) << canonical_output_ids.size()) - UINT16_C(1));
  if ((bits & static_cast<std::uint8_t>(~valid_bits)) != 0)
    return std::nullopt;
  std::vector<std::uint64_t> membership;
  membership.reserve(canonical_output_ids.size());
  for (std::size_t index = 0; index < canonical_output_ids.size(); ++index)
    if ((bits & (UINT8_C(1) << index)) != 0)
      membership.push_back(canonical_output_ids[index]);
  return membership;
}

}  // namespace glasswyrm::ipc::internal

#endif
