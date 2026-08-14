#include "glasswyrmd/atom_table.hpp"

#include <limits>
#include <new>
#include <utility>

namespace glasswyrm::server {
AtomTable::AtomTable() : AtomTable(AtomLimits{}) {}

AtomTable::AtomTable(const std::uint32_t maximum_atom)
    : AtomTable(AtomLimits{.maximum_atom_id = maximum_atom}) {}

AtomTable::AtomTable(AtomLimits limits) : limits_(limits) {
  ids_by_name_.reserve(kHighestPredefinedAtom);
  names_by_id_.reserve(kHighestPredefinedAtom);
  for (const auto& predefined : gw::protocol::x11::kPredefinedAtoms) {
    std::string name(predefined.name);
    name_bytes_ += name.size();
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
  if (next_dynamic_atom_ == 0 ||
      next_dynamic_atom_ > limits_.maximum_atom_id ||
      names_by_id_.size() >= limits_.maximum_atoms ||
      name_bytes_ > limits_.maximum_name_bytes ||
      atom_name.size() > limits_.maximum_name_bytes - name_bytes_) {
    return {.status = InternAtomStatus::Exhausted};
  }

  try {
    const std::uint32_t atom = next_dynamic_atom_;
    std::string owned_name(atom_name);
    const auto [name_iterator, inserted] =
        ids_by_name_.emplace(owned_name, atom);
    if (!inserted) {
      return {.atom = name_iterator->second};
    }
    try {
      names_by_id_.emplace(atom, std::move(owned_name));
    } catch (...) {
      ids_by_name_.erase(name_iterator);
      throw;
    }
    name_bytes_ += atom_name.size();
    next_dynamic_atom_ =
        atom == std::numeric_limits<std::uint32_t>::max() ? 0 : atom + 1;
    return {.atom = atom};
  } catch (const std::bad_alloc&) {
    return {.status = InternAtomStatus::Exhausted};
  }
}

std::optional<std::uint32_t> AtomTable::find(
    const std::string_view atom_name) const {
  const auto iterator = ids_by_name_.find(atom_name);
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
