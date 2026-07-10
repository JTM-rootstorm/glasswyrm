#include "protocol/x11/reply.hpp"

#include <limits>
#include <stdexcept>

namespace gw::protocol::x11 {
namespace {

[[nodiscard]] constexpr std::size_t padding_for(const std::size_t size) noexcept {
  return (4 - (size & 3U)) & 3U;
}

} // namespace

ReplyBuilder::ReplyBuilder(const ByteOrder order, const std::uint64_t sequence,
                           const std::uint8_t response_data) noexcept
    : order_(order), sequence_(sequence), response_data_(response_data),
      fixed_(order), payload_(order) {}

void ReplyBuilder::ensure_fixed_capacity(const std::size_t count) const {
  if (count > 24 || fixed_.size() > 24 - count) {
    throw std::length_error("X11 reply fixed fields exceed 24 bytes");
  }
}

void ReplyBuilder::write_u8(const std::uint8_t value) {
  ensure_fixed_capacity(1);
  fixed_.write_u8(value);
}

void ReplyBuilder::write_u16(const std::uint16_t value) {
  ensure_fixed_capacity(2);
  fixed_.write_u16(value);
}

void ReplyBuilder::write_u32(const std::uint32_t value) {
  ensure_fixed_capacity(4);
  fixed_.write_u32(value);
}

void ReplyBuilder::write_padding(const std::size_t count) {
  ensure_fixed_capacity(count);
  fixed_.write_padding(count);
}

void ReplyBuilder::write_payload(const std::span<const std::uint8_t> bytes) {
  payload_.write_bytes(bytes);
}

void ReplyBuilder::write_payload_u16(const std::uint16_t value) {
  payload_.write_u16(value);
}

void ReplyBuilder::write_payload_u32(const std::uint32_t value) {
  payload_.write_u32(value);
}

std::vector<std::uint8_t> ReplyBuilder::finish() && {
  fixed_.write_padding(24 - fixed_.size());
  payload_.write_padding(padding_for(payload_.size()));
  const auto payload_units = payload_.size() / 4;
  if (payload_units > std::numeric_limits<std::uint32_t>::max()) {
    throw std::length_error("X11 reply payload exceeds wire length field");
  }

  ByteWriter reply(order_);
  reply.write_u8(1);
  reply.write_u8(response_data_);
  reply.write_u16(wire_sequence(sequence_));
  reply.write_u32(static_cast<std::uint32_t>(payload_units));
  const auto fixed = std::move(fixed_).take();
  const auto payload = std::move(payload_).take();
  reply.write_bytes(fixed);
  reply.write_bytes(payload);
  return std::move(reply).take();
}

std::vector<std::uint8_t> encode_core_error(const ByteOrder order,
                                            const CoreError &error) {
  ByteWriter packet(order);
  packet.write_u8(0);
  packet.write_u8(static_cast<std::uint8_t>(error.code));
  packet.write_u16(wire_sequence(error.sequence));
  packet.write_u32(error.bad_value);
  packet.write_u16(error.minor_opcode);
  packet.write_u8(error.major_opcode);
  packet.write_padding(21);
  return std::move(packet).take();
}

} // namespace gw::protocol::x11
