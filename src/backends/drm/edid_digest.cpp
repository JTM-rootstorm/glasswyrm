#include "backends/drm/edid_digest.hpp"

#include <string_view>

namespace glasswyrm::drm {
namespace {

constexpr std::uint64_t kFnvOffsetBasis = UINT64_C(14695981039346656037);
constexpr std::uint64_t kFnvPrime = UINT64_C(1099511628211);

void hash_bytes(std::uint64_t &hash,
                const std::span<const std::uint8_t> bytes) noexcept {
  for (const auto byte : bytes) {
    hash ^= byte;
    hash *= kFnvPrime;
  }
}

} // namespace

EdidIdentityDigest
derive_edid_identity_digest(const std::span<const std::uint8_t> edid) noexcept {
  if (edid.empty())
    return {};

  std::uint64_t hash = kFnvOffsetBasis;
  constexpr std::string_view domain{"glasswyrm:drm:edid:"};
  hash_bytes(hash, std::span<const std::uint8_t>(
                       reinterpret_cast<const std::uint8_t *>(domain.data()),
                       domain.size()));
  hash_bytes(hash, edid);

  EdidIdentityDigest digest{};
  for (std::size_t index = 0; index < digest.size(); ++index)
    digest[index] = static_cast<std::uint8_t>(
        hash >> (8U * static_cast<unsigned>(digest.size() - index - 1U)));
  return digest;
}

} // namespace glasswyrm::drm
