#include "glasswyrmd/atom_table.hpp"

#include <limits>
#include <new>
#include <utility>

namespace glasswyrm::server {
AtomTable::AtomTable() {
  ids_by_name_.reserve(kHighestPredefinedAtom);
  names_by_id_.reserve(kHighestPredefinedAtom);
  for (const auto& predefined : gw::protocol::x11::kPredefinedAtoms) {
    std::string name(predefined.name);
    ids_by_name_.emplace(name, predefined.id);
    names_by_id_.emplace(predefined.id, std::move(name));
  }
}

InternAtomResult AtomTable::intern(const std::string_view atom_name,
                                  const bool only_if_exists) {
  if (const auto existing = find(atom_name); existing) {
    return {.atom = *existing};
  }
  if (only_if_exists) {
    return {};
  }
  if (next_dynamic_atom_ == 0) {
    return {.status = InternAtomStatus::Exhausted};
  }

  try {
    const std::uint32_t atom = next_dynamic_atom_;
    std::string owned_name(atom_name);
    ids_by_name_.emplace(owned_name, atom);
    try {
      names_by_id_.emplace(atom, std::move(owned_name));
    } catch (...) {
      ids_by_name_.erase(std::string(atom_name));
      throw;
    }
    next_dynamic_atom_ =
        atom == std::numeric_limits<std::uint32_t>::max() ? 0 : atom + 1;
    return {.atom = atom};
  } catch (const std::bad_alloc&) {
    return {.status = InternAtomStatus::Exhausted};
  }
}

std::optional<std::uint32_t> AtomTable::find(
    const std::string_view atom_name) const {
  const auto iterator = ids_by_name_.find(std::string(atom_name));
  return iterator == ids_by_name_.end()
             ? std::nullopt
             : std::optional<std::uint32_t>(iterator->second);
}

std::optional<std::string_view> AtomTable::name(const std::uint32_t atom) const {
  const auto iterator = names_by_id_.find(atom);
  return iterator == names_by_id_.end()
             ? std::nullopt
             : std::optional<std::string_view>(iterator->second);
}

bool AtomTable::valid(const std::uint32_t atom,
                      const bool allow_none) const noexcept {
  return (allow_none && atom == 0) || names_by_id_.contains(atom);
}

}  // namespace glasswyrm::server
