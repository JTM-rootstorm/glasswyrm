#pragma once

#include "protocol/x11/atoms.hpp"

#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace glasswyrm::server {

inline constexpr std::uint32_t kHighestPredefinedAtom =
    gw::protocol::x11::kLastPredefinedAtom;

enum class InternAtomStatus { Success, Exhausted };

struct InternAtomResult {
  InternAtomStatus status{InternAtomStatus::Success};
  std::uint32_t atom{0};
};

inline constexpr std::size_t kMaximumAtoms = 65536;
inline constexpr std::size_t kMaximumAtomNameBytes = 4U * 1024U * 1024U;

struct AtomLimits {
  std::uint32_t maximum_atom_id{
      std::numeric_limits<std::uint32_t>::max()};
  std::size_t maximum_atoms{kMaximumAtoms};
  std::size_t maximum_name_bytes{kMaximumAtomNameBytes};
};

struct TransparentStringHash {
  using is_transparent = void;

  [[nodiscard]] std::size_t operator()(std::string_view value) const noexcept {
    return std::hash<std::string_view>{}(value);
  }
  [[nodiscard]] std::size_t operator()(const std::string& value) const noexcept {
    return (*this)(std::string_view(value));
  }
};

class AtomTable {
 public:
  AtomTable();
  explicit AtomTable(std::uint32_t maximum_atom);
  explicit AtomTable(AtomLimits limits);

  [[nodiscard]] InternAtomResult intern(std::string_view name,
                                        bool only_if_exists);
  [[nodiscard]] std::optional<std::uint32_t> find(
      std::string_view name) const;
  [[nodiscard]] std::optional<std::string_view> name(
      std::uint32_t atom) const;
  [[nodiscard]] bool valid(std::uint32_t atom,
                           bool allow_none = false) const noexcept;
  [[nodiscard]] std::size_t size() const noexcept { return names_by_id_.size(); }
  [[nodiscard]] std::size_t name_bytes() const noexcept { return name_bytes_; }

 private:
  std::unordered_map<std::string, std::uint32_t, TransparentStringHash,
                     std::equal_to<>>
      ids_by_name_;
  std::unordered_map<std::uint32_t, std::string> names_by_id_;
  std::uint32_t next_dynamic_atom_{kHighestPredefinedAtom + 1};
  AtomLimits limits_;
  std::size_t name_bytes_{0};
};

}  // namespace glasswyrm::server
