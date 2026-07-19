#pragma once

#include "output/model/types.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace glasswyrm::output {

inline constexpr std::uint64_t kOutputIdentityNamespace = UINT64_C(1) << 63U;
inline constexpr std::uint64_t kModeIdentityNamespace = UINT64_C(1) << 62U;

struct DrmIdentityInput {
  std::string_view device_identity;
  std::string_view connector_identity;
  std::span<const std::uint8_t> edid_digest;
};

struct DerivedOutputIdentity {
  OutputId id{};
  bool edid_participated{};
};

[[nodiscard]] std::optional<OutputId>
derive_headless_output_id(std::string_view name) noexcept;

[[nodiscard]] std::optional<DerivedOutputIdentity>
derive_drm_output_id(const DrmIdentityInput &input) noexcept;

[[nodiscard]] std::optional<OutputModeId>
derive_output_mode_id(OutputId output_id, std::uint32_t physical_width,
                      std::uint32_t physical_height,
                      std::uint32_t refresh_millihertz, std::uint32_t flags,
                      std::string_view name) noexcept;

[[nodiscard]] bool
output_identities_are_unique(std::span<const OutputId> identities) noexcept;

[[nodiscard]] bool
mode_identities_are_unique(std::span<const OutputModeId> identities) noexcept;

} // namespace glasswyrm::output
