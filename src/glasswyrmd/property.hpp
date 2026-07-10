#pragma once

#include <cstddef>
#include <cstdint>
#include <variant>
#include <vector>

namespace glasswyrm::server {

using PropertyData =
    std::variant<std::vector<std::uint8_t>, std::vector<std::uint16_t>,
                 std::vector<std::uint32_t>>;

struct Property {
  std::uint32_t type{0};
  PropertyData data{std::vector<std::uint8_t>{}};

  [[nodiscard]] std::uint8_t format() const noexcept;
  [[nodiscard]] std::size_t item_count() const noexcept;
  [[nodiscard]] std::size_t byte_size() const noexcept;
};

enum class PropertyMode { Replace, Prepend, Append };

struct PropertySlice {
  std::uint32_t type{0};
  std::uint8_t format{0};
  std::uint32_t bytes_after{0};
  PropertyData data{std::vector<std::uint8_t>{}};

  [[nodiscard]] std::size_t item_count() const noexcept;
};

[[nodiscard]] PropertyData slice_property_data(const PropertyData& data,
                                               std::size_t byte_offset,
                                               std::size_t byte_length);
[[nodiscard]] PropertyData concatenate_property_data(const PropertyData& first,
                                                     const PropertyData& second);

}  // namespace glasswyrm::server
