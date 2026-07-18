#include "output/model/identity.hpp"

#include <array>
#include <limits>

namespace glasswyrm::output {
namespace {

constexpr std::uint64_t kFnvOffsetBasis = UINT64_C(14695981039346656037);
constexpr std::uint64_t kFnvPrime = UINT64_C(1099511628211);

class StableHash {
public:
  void append(const std::span<const std::uint8_t> bytes) noexcept {
    for (const auto byte : bytes) {
      value_ ^= byte;
      value_ *= kFnvPrime;
    }
  }

  void append(const std::string_view value) noexcept {
    append(std::span<const std::uint8_t>(
        reinterpret_cast<const std::uint8_t *>(value.data()), value.size()));
  }

  void append_u32(const std::uint32_t value) noexcept {
    const std::array<std::uint8_t, 4> bytes{
        static_cast<std::uint8_t>(value >> 24U),
        static_cast<std::uint8_t>(value >> 16U),
        static_cast<std::uint8_t>(value >> 8U),
        static_cast<std::uint8_t>(value)};
    append(bytes);
  }

  void append_u64(const std::uint64_t value) noexcept {
    append_u32(static_cast<std::uint32_t>(value >> 32U));
    append_u32(static_cast<std::uint32_t>(value));
  }

  [[nodiscard]] std::uint64_t value() const noexcept { return value_; }

private:
  std::uint64_t value_{kFnvOffsetBasis};
};

bool append_sized(StableHash &hash, const std::string_view value) noexcept {
  if (value.size() > std::numeric_limits<std::uint32_t>::max())
    return false;
  hash.append_u32(static_cast<std::uint32_t>(value.size()));
  hash.append(value);
  return true;
}

std::uint64_t output_identity(const std::uint64_t hash) noexcept {
  return hash | kOutputIdentityNamespace;
}

std::uint64_t mode_identity(const std::uint64_t hash) noexcept {
  return (hash & ~kOutputIdentityNamespace) | kModeIdentityNamespace;
}

} // namespace

std::optional<OutputId>
derive_headless_output_id(const std::string_view name) noexcept {
  if (name.empty() || name.size() > 63U)
    return std::nullopt;
  StableHash hash;
  hash.append("glasswyrm:headless:");
  hash.append(name);
  return OutputId{output_identity(hash.value())};
}

std::optional<DerivedOutputIdentity>
derive_drm_output_id(const DrmIdentityInput &input) noexcept {
  if (input.device_identity.empty() || input.connector_identity.empty() ||
      input.device_identity.size() >
          std::numeric_limits<std::uint32_t>::max() ||
      input.connector_identity.size() >
          std::numeric_limits<std::uint32_t>::max() ||
      input.edid_digest.size() > std::numeric_limits<std::uint32_t>::max())
    return std::nullopt;

  StableHash hash;
  hash.append("glasswyrm:drm:");
  if (!append_sized(hash, input.device_identity) ||
      !append_sized(hash, input.connector_identity))
    return std::nullopt;
  hash.append_u32(static_cast<std::uint32_t>(input.edid_digest.size()));
  hash.append(input.edid_digest);
  return DerivedOutputIdentity{OutputId{output_identity(hash.value())},
                               !input.edid_digest.empty()};
}

std::optional<OutputModeId> derive_output_mode_id(
    const OutputId output_id, const std::uint32_t physical_width,
    const std::uint32_t physical_height, const std::uint32_t refresh_millihertz,
    const std::uint32_t flags, const std::string_view name) noexcept {
  if (output_id.value == 0 || physical_width == 0 || physical_height == 0 ||
      refresh_millihertz == 0 || name.empty() || name.size() > 63U)
    return std::nullopt;

  StableHash hash;
  hash.append("glasswyrm:mode:");
  hash.append_u64(output_id.value);
  hash.append_u32(physical_width);
  hash.append_u32(physical_height);
  hash.append_u32(refresh_millihertz);
  hash.append_u32(flags);
  if (!append_sized(hash, name))
    return std::nullopt;
  return OutputModeId{mode_identity(hash.value())};
}

bool output_identities_are_unique(
    const std::span<const OutputId> identities) noexcept {
  for (std::size_t index = 0; index < identities.size(); ++index) {
    if (!identities[index])
      return false;
    for (std::size_t prior = 0; prior < index; ++prior)
      if (identities[prior] == identities[index])
        return false;
  }
  return true;
}

bool mode_identities_are_unique(
    const std::span<const OutputModeId> identities) noexcept {
  for (std::size_t index = 0; index < identities.size(); ++index) {
    if (!identities[index])
      return false;
    for (std::size_t prior = 0; prior < index; ++prior)
      if (identities[prior] == identities[index])
        return false;
  }
  return true;
}

} // namespace glasswyrm::output
