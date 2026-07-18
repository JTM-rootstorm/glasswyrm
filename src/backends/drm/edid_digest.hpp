#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace glasswyrm::drm {

inline constexpr std::size_t kEdidIdentityDigestBytes = 8;
using EdidIdentityDigest = std::array<std::uint8_t, kEdidIdentityDigestBytes>;

// This digest is stable identity material, not a security primitive. Empty
// EDID data means that EDID does not participate in the output identity.
[[nodiscard]] EdidIdentityDigest
derive_edid_identity_digest(std::span<const std::uint8_t> edid) noexcept;

} // namespace glasswyrm::drm
