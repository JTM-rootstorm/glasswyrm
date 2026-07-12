#include "helpers/x11_request_builder.hpp"

#include "protocol/x11/byte_cursor.hpp"

#include <bit>
#include <limits>
#include <stdexcept>

namespace gw::test {
namespace x11 = protocol::x11;
namespace {

std::vector<std::uint8_t> finish_request(x11::ByteWriter writer,
                                         x11::ByteOrder order) {
  while ((writer.size() & 3U) != 0) {
    writer.write_u8(0);
  }
  auto bytes = std::move(writer).take();
  if (bytes.size() / 4 > std::numeric_limits<std::uint16_t>::max()) {
    throw std::length_error("X11 test request is too large");
  }
  const auto units = static_cast<std::uint16_t>(bytes.size() / 4);
  if (order == x11::ByteOrder::LittleEndian) {
    bytes[2] = static_cast<std::uint8_t>(units);
    bytes[3] = static_cast<std::uint8_t>(units >> 8U);
  } else {
    bytes[2] = static_cast<std::uint8_t>(units >> 8U);
    bytes[3] = static_cast<std::uint8_t>(units);
  }
  return bytes;
}

x11::ByteWriter header(std::uint8_t opcode, std::uint8_t data,
                       x11::ByteOrder order) {
  x11::ByteWriter writer(order);
  writer.write_u8(opcode);
  writer.write_u8(data);
  writer.write_u16(0);
  return writer;
}

}  // namespace

std::vector<std::uint8_t> X11RequestBuilder::create_window(
    std::uint32_t window, std::uint32_t parent, std::int16_t x, std::int16_t y,
    std::uint16_t width, std::uint16_t height, std::uint32_t value_mask,
    std::span<const std::uint32_t> values, std::uint8_t depth,
    std::uint16_t window_class, std::uint32_t visual,
    std::uint16_t border_width) const {
  if (static_cast<std::size_t>(std::popcount(value_mask)) != values.size()) {
    throw std::invalid_argument("CreateWindow mask/value count mismatch");
  }
  auto writer = header(1, depth, order_);
  writer.write_u32(window);
  writer.write_u32(parent);
  writer.write_u16(static_cast<std::uint16_t>(x));
  writer.write_u16(static_cast<std::uint16_t>(y));
  writer.write_u16(width);
  writer.write_u16(height);
  writer.write_u16(border_width);
  writer.write_u16(window_class);
  writer.write_u32(visual);
  writer.write_u32(value_mask);
  for (const auto value : values) {
    writer.write_u32(value);
  }
  return finish_request(std::move(writer), order_);
}

std::vector<std::uint8_t> X11RequestBuilder::destroy_window(
    std::uint32_t window) const {
  auto writer = header(4, 0, order_);
  writer.write_u32(window);
  return finish_request(std::move(writer), order_);
}

std::vector<std::uint8_t> X11RequestBuilder::change_window_attributes(
    std::uint32_t window, std::uint32_t value_mask,
    std::span<const std::uint32_t> values) const {
  if (static_cast<std::size_t>(std::popcount(value_mask)) != values.size()) {
    throw std::invalid_argument(
        "ChangeWindowAttributes mask/value count mismatch");
  }
  auto writer = header(2, 0, order_);
  writer.write_u32(window);
  writer.write_u32(value_mask);
  for (const auto value : values) writer.write_u32(value);
  return finish_request(std::move(writer), order_);
}

std::vector<std::uint8_t> X11RequestBuilder::get_window_attributes(
    std::uint32_t window) const {
  auto writer = header(3, 0, order_);
  writer.write_u32(window);
  return finish_request(std::move(writer), order_);
}

std::vector<std::uint8_t> X11RequestBuilder::get_geometry(
    std::uint32_t drawable) const {
  auto writer = header(14, 0, order_);
  writer.write_u32(drawable);
  return finish_request(std::move(writer), order_);
}

std::vector<std::uint8_t> X11RequestBuilder::query_tree(
    std::uint32_t window) const {
  auto writer = header(15, 0, order_);
  writer.write_u32(window);
  return finish_request(std::move(writer), order_);
}

std::vector<std::uint8_t> X11RequestBuilder::intern_atom(
    std::string_view name, bool only_if_exists) const {
  if (name.size() > std::numeric_limits<std::uint16_t>::max()) {
    throw std::length_error("atom name is too long");
  }
  auto writer = header(16, only_if_exists ? 1 : 0, order_);
  writer.write_u16(static_cast<std::uint16_t>(name.size()));
  writer.write_padding(2);
  writer.write_bytes(std::span<const std::uint8_t>(
      reinterpret_cast<const std::uint8_t*>(name.data()), name.size()));
  return finish_request(std::move(writer), order_);
}

std::vector<std::uint8_t> X11RequestBuilder::get_atom_name(
    std::uint32_t atom) const {
  auto writer = header(17, 0, order_);
  writer.write_u32(atom);
  return finish_request(std::move(writer), order_);
}

std::vector<std::uint8_t> X11RequestBuilder::change_property(
    std::uint8_t mode, std::uint32_t window, std::uint32_t property,
    std::uint32_t type, std::uint8_t format,
    std::span<const std::uint8_t> encoded_data,
    std::uint32_t item_count) const {
  auto writer = header(18, mode, order_);
  writer.write_u32(window);
  writer.write_u32(property);
  writer.write_u32(type);
  writer.write_u8(format);
  writer.write_padding(3);
  writer.write_u32(item_count);
  writer.write_bytes(encoded_data);
  return finish_request(std::move(writer), order_);
}

std::vector<std::uint8_t> X11RequestBuilder::delete_property(
    std::uint32_t window, std::uint32_t property) const {
  auto writer = header(19, 0, order_);
  writer.write_u32(window);
  writer.write_u32(property);
  return finish_request(std::move(writer), order_);
}

std::vector<std::uint8_t> X11RequestBuilder::get_property(
    std::uint32_t window, std::uint32_t property, std::uint32_t type,
    std::uint32_t long_offset, std::uint32_t long_length,
    bool delete_after) const {
  auto writer = header(20, delete_after ? 1 : 0, order_);
  writer.write_u32(window);
  writer.write_u32(property);
  writer.write_u32(type);
  writer.write_u32(long_offset);
  writer.write_u32(long_length);
  return finish_request(std::move(writer), order_);
}

std::vector<std::uint8_t> X11RequestBuilder::list_properties(
    std::uint32_t window) const {
  auto writer = header(21, 0, order_);
  writer.write_u32(window);
  return finish_request(std::move(writer), order_);
}

std::vector<std::uint8_t> X11RequestBuilder::get_input_focus() const {
  return raw(43, 0);
}

std::vector<std::uint8_t> X11RequestBuilder::no_operation(
    std::uint16_t length_units) const {
  if (length_units == 0) {
    auto bytes = raw(127, 0);
    bytes[2] = 0;
    bytes[3] = 0;
    return bytes;
  }
  std::vector<std::uint8_t> body(
      static_cast<std::size_t>(length_units - 1) * 4, 0);
  return raw(127, 0, body);
}

std::vector<std::uint8_t> X11RequestBuilder::raw(
    std::uint8_t opcode, std::uint8_t data,
    std::span<const std::uint8_t> body) const {
  auto writer = header(opcode, data, order_);
  writer.write_bytes(body);
  return finish_request(std::move(writer), order_);
}

std::uint16_t read_wire_u16(const std::uint8_t* bytes, x11::ByteOrder order) {
  if (order == x11::ByteOrder::LittleEndian) {
    return static_cast<std::uint16_t>(bytes[0]) |
           static_cast<std::uint16_t>(
               static_cast<std::uint16_t>(bytes[1]) << 8U);
  }
  return static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[0]) << 8U) |
         static_cast<std::uint16_t>(bytes[1]);
}

std::uint32_t read_wire_u32(const std::uint8_t* bytes, x11::ByteOrder order) {
  if (order == x11::ByteOrder::LittleEndian) {
    return static_cast<std::uint32_t>(bytes[0]) |
           (static_cast<std::uint32_t>(bytes[1]) << 8U) |
           (static_cast<std::uint32_t>(bytes[2]) << 16U) |
           (static_cast<std::uint32_t>(bytes[3]) << 24U);
  }
  return (static_cast<std::uint32_t>(bytes[0]) << 24U) |
         (static_cast<std::uint32_t>(bytes[1]) << 16U) |
         (static_cast<std::uint32_t>(bytes[2]) << 8U) |
         static_cast<std::uint32_t>(bytes[3]);
}

}  // namespace gw::test
