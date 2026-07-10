#pragma once

#include "protocol/x11/byte_cursor.hpp"
#include "protocol/x11/core.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace gw::protocol::x11 {

inline constexpr std::size_t kCoreReplySize = 32;
inline constexpr std::size_t kCoreErrorSize = 32;

class ReplyBuilder {
public:
  ReplyBuilder(ByteOrder order, std::uint64_t sequence,
               std::uint8_t response_data = 0) noexcept;

  void write_u8(std::uint8_t value);
  void write_u16(std::uint16_t value);
  void write_u32(std::uint32_t value);
  void write_padding(std::size_t count);
  void write_payload(std::span<const std::uint8_t> bytes);
  void write_payload_u16(std::uint16_t value);
  void write_payload_u32(std::uint32_t value);

  [[nodiscard]] std::vector<std::uint8_t> finish() &&;

private:
  void ensure_fixed_capacity(std::size_t count) const;

  ByteOrder order_;
  std::uint64_t sequence_;
  std::uint8_t response_data_;
  ByteWriter fixed_;
  ByteWriter payload_;
};

struct CoreError {
  CoreErrorCode code{CoreErrorCode::BadImplementation};
  std::uint64_t sequence{0};
  std::uint32_t bad_value{0};
  std::uint8_t major_opcode{0};
  std::uint16_t minor_opcode{0};
};

[[nodiscard]] std::vector<std::uint8_t>
encode_core_error(ByteOrder order, const CoreError &error);

} // namespace gw::protocol::x11
