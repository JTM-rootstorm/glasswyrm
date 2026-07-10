#pragma once

#include "protocol/x11/atoms.hpp"

#include <cstdint>
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

class AtomTable {
 public:
  explicit AtomTable(
      std::uint32_t maximum_atom = std::numeric_limits<std::uint32_t>::max());

  [[nodiscard]] InternAtomResult intern(std::string_view name,
                                        bool only_if_exists);
  [[nodiscard]] std::optional<std::uint32_t> find(
      std::string_view name) const;
  [[nodiscard]] std::optional<std::string_view> name(
      std::uint32_t atom) const;
  [[nodiscard]] bool valid(std::uint32_t atom,
                           bool allow_none = false) const noexcept;
  [[nodiscard]] std::size_t size() const noexcept { return names_by_id_.size(); }

 private:
  std::unordered_map<std::string, std::uint32_t> ids_by_name_;
  std::unordered_map<std::uint32_t, std::string> names_by_id_;
  std::uint32_t next_dynamic_atom_{kHighestPredefinedAtom + 1};
  std::uint32_t maximum_atom_;
};

}  // namespace glasswyrm::server
