#include "glasswyrmd/resource_table.hpp"

#include <algorithm>
#include <new>

namespace glasswyrm::server {

PropertyMutationStatus ResourceTable::change_property(
    const std::uint32_t window_id, const std::uint32_t property_atom,
    Property value, const PropertyMode mode) {
  auto* window = find_window(window_id);
  if (window == nullptr) {
    return PropertyMutationStatus::BadWindow;
  }
  auto current = window->properties.find(property_atom);
  const bool exists = current != window->properties.end();
  if (exists && mode != PropertyMode::Replace &&
      (current->second.type != value.type ||
       current->second.format() != value.format())) {
    return PropertyMutationStatus::BadMatch;
  }

  try {
    Property replacement = std::move(value);
    if (exists && mode == PropertyMode::Append) {
      replacement.data = concatenate_property_data(current->second.data,
                                                   replacement.data);
    } else if (exists && mode == PropertyMode::Prepend) {
      replacement.data = concatenate_property_data(replacement.data,
                                                   current->second.data);
    }
    const std::size_t old_size = exists ? current->second.byte_size() : 0;
    const std::size_t new_size = replacement.byte_size();
    if (new_size > limits_.maximum_bytes_per_property ||
        (!exists && window->properties.size() >=
                        limits_.maximum_properties_per_window) ||
        new_size > limits_.maximum_total_property_bytes -
                       (total_property_bytes_ - old_size)) {
      return PropertyMutationStatus::BadAlloc;
    }

    if (exists) {
      current->second = std::move(replacement);
    } else {
      window->properties.emplace(property_atom, std::move(replacement));
    }
    total_property_bytes_ = total_property_bytes_ - old_size + new_size;
  } catch (const std::bad_alloc&) {
    return PropertyMutationStatus::BadAlloc;
  }
  return PropertyMutationStatus::Success;
}

bool ResourceTable::delete_property(const std::uint32_t window_id,
                                    const std::uint32_t property_atom) {
  auto* window = find_window(window_id);
  if (window == nullptr) {
    return false;
  }
  const auto iterator = window->properties.find(property_atom);
  if (iterator == window->properties.end()) {
    return true;
  }
  total_property_bytes_ -= iterator->second.byte_size();
  window->properties.erase(iterator);
  return true;
}

PropertyReadResult ResourceTable::get_property(
    const std::uint32_t window_id, const std::uint32_t property_atom,
    const std::uint32_t requested_type, const bool delete_after_read,
    const std::uint32_t long_offset, const std::uint32_t long_length) {
  auto* window = find_window(window_id);
  if (window == nullptr) {
    PropertyReadResult result;
    result.status = PropertyReadStatus::BadWindow;
    return result;
  }
  const auto iterator = window->properties.find(property_atom);
  if (iterator == window->properties.end()) {
    return {};
  }
  const auto& property = iterator->second;
  const std::size_t byte_size = property.byte_size();
  const std::uint64_t offset64 = static_cast<std::uint64_t>(long_offset) * 4U;
  if (offset64 > byte_size) {
    PropertyReadResult result;
    result.status = PropertyReadStatus::BadValue;
    return result;
  }

  PropertyReadResult result;
  result.present = true;
  result.value.type = property.type;
  result.value.format = property.format();
  result.type_matched = requested_type == 0 || requested_type == property.type;
  if (!result.type_matched) {
    result.value.bytes_after = static_cast<std::uint32_t>(byte_size);
    return result;
  }

  const std::size_t offset = static_cast<std::size_t>(offset64);
  const std::uint64_t requested64 = static_cast<std::uint64_t>(long_length) * 4U;
  const std::size_t available = byte_size - offset;
  const std::size_t returned = static_cast<std::size_t>(
      std::min<std::uint64_t>(available, requested64));
  result.value.bytes_after = static_cast<std::uint32_t>(available - returned);
  result.value.data = slice_property_data(property.data, offset, returned);
  if (delete_after_read && result.value.bytes_after == 0) {
    total_property_bytes_ -= byte_size;
    window->properties.erase(iterator);
    result.deleted = true;
  }
  return result;
}

std::vector<std::uint32_t> ResourceTable::list_properties(
    const std::uint32_t window_id) const {
  std::vector<std::uint32_t> result;
  const auto* window = find_window(window_id);
  if (window == nullptr) {
    return result;
  }
  result.reserve(window->properties.size());
  for (const auto& [atom, property] : window->properties) {
    static_cast<void>(property);
    result.push_back(atom);
  }
  std::sort(result.begin(), result.end());
  return result;
}

}  // namespace glasswyrm::server
