#pragma once

#include "protocol/x11/byte_order.hpp"

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace gw::test {

class X11RequestBuilder {
 public:
  explicit X11RequestBuilder(protocol::x11::ByteOrder order) : order_(order) {}

  [[nodiscard]] std::vector<std::uint8_t> create_window(
      std::uint32_t window, std::uint32_t parent, std::int16_t x,
      std::int16_t y, std::uint16_t width, std::uint16_t height,
      std::uint32_t value_mask = 0,
      std::span<const std::uint32_t> values = {}, std::uint8_t depth = 24,
      std::uint16_t window_class = 1, std::uint32_t visual = 3,
      std::uint16_t border_width = 0) const;
  [[nodiscard]] std::vector<std::uint8_t> destroy_window(
      std::uint32_t window) const;
  [[nodiscard]] std::vector<std::uint8_t> change_window_attributes(
      std::uint32_t window, std::uint32_t value_mask,
      std::span<const std::uint32_t> values) const;
  [[nodiscard]] std::vector<std::uint8_t> get_window_attributes(
      std::uint32_t window) const;
  [[nodiscard]] std::vector<std::uint8_t> get_geometry(
      std::uint32_t drawable) const;
  [[nodiscard]] std::vector<std::uint8_t> query_tree(
      std::uint32_t window) const;
  [[nodiscard]] std::vector<std::uint8_t> intern_atom(
      std::string_view name, bool only_if_exists = false) const;
  [[nodiscard]] std::vector<std::uint8_t> get_atom_name(
      std::uint32_t atom) const;
  [[nodiscard]] std::vector<std::uint8_t> change_property(
      std::uint8_t mode, std::uint32_t window, std::uint32_t property,
      std::uint32_t type, std::uint8_t format,
      std::span<const std::uint8_t> encoded_data,
      std::uint32_t item_count) const;
  [[nodiscard]] std::vector<std::uint8_t> delete_property(
      std::uint32_t window, std::uint32_t property) const;
  [[nodiscard]] std::vector<std::uint8_t> get_property(
      std::uint32_t window, std::uint32_t property, std::uint32_t type,
      std::uint32_t long_offset, std::uint32_t long_length,
      bool delete_after = false) const;
  [[nodiscard]] std::vector<std::uint8_t> list_properties(
      std::uint32_t window) const;
  [[nodiscard]] std::vector<std::uint8_t> get_input_focus() const;
  [[nodiscard]] std::vector<std::uint8_t> no_operation(
      std::uint16_t length_units = 1) const;
  [[nodiscard]] std::vector<std::uint8_t> raw(
      std::uint8_t opcode, std::uint8_t data,
      std::span<const std::uint8_t> body = {}) const;

 private:
  protocol::x11::ByteOrder order_;
};

[[nodiscard]] std::uint16_t read_wire_u16(
    const std::uint8_t* bytes, protocol::x11::ByteOrder order);
[[nodiscard]] std::uint32_t read_wire_u32(
    const std::uint8_t* bytes, protocol::x11::ByteOrder order);

}  // namespace gw::test
