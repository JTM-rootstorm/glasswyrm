#include "glasswyrmd/property.hpp"

#include <algorithm>
#include <type_traits>

namespace glasswyrm::server {
namespace {

template <typename Values>
Values slice_values(const Values& values, const std::size_t byte_offset,
                    const std::size_t byte_length) {
  using Value = typename Values::value_type;
  const std::size_t first = byte_offset / sizeof(Value);
  const std::size_t count = byte_length / sizeof(Value);
  const std::size_t last = std::min(values.size(), first + count);
  if (first >= values.size()) {
    return {};
  }
  return Values(values.begin() + static_cast<std::ptrdiff_t>(first),
                values.begin() + static_cast<std::ptrdiff_t>(last));
}

}  // namespace

std::uint8_t Property::format() const noexcept {
  return std::visit(
      [](const auto& values) {
        using Value = typename std::decay_t<decltype(values)>::value_type;
        return static_cast<std::uint8_t>(sizeof(Value) * 8U);
      },
      data);
}

std::size_t Property::item_count() const noexcept {
  return std::visit([](const auto& values) { return values.size(); }, data);
}

std::size_t Property::byte_size() const noexcept {
  return std::visit(
      [](const auto& values) {
        using Value = typename std::decay_t<decltype(values)>::value_type;
        return values.size() * sizeof(Value);
      },
      data);
}

std::size_t PropertySlice::item_count() const noexcept {
  return std::visit([](const auto& values) { return values.size(); }, data);
}

PropertyData slice_property_data(const PropertyData& data,
                                 const std::size_t byte_offset,
                                 const std::size_t byte_length) {
  return std::visit(
      [byte_offset, byte_length](const auto& values) -> PropertyData {
        return slice_values(values, byte_offset, byte_length);
      },
      data);
}

PropertyData concatenate_property_data(const PropertyData& first,
                                       const PropertyData& second) {
  return std::visit(
      [](const auto& left, const auto& right) -> PropertyData {
        using Left = std::decay_t<decltype(left)>;
        using Right = std::decay_t<decltype(right)>;
        if constexpr (!std::is_same_v<Left, Right>) {
          return left;
        } else {
          Left result;
          result.reserve(left.size() + right.size());
          result.insert(result.end(), left.begin(), left.end());
          result.insert(result.end(), right.begin(), right.end());
          return result;
        }
      },
      first, second);
}

}  // namespace glasswyrm::server
