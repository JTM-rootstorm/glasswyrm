#pragma once

#include "ipc/wire/types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace gw::ipc::wire {

inline constexpr std::size_t kEnvelopeSize = 40;
inline constexpr std::uint16_t kWireMajor = 1;
inline constexpr std::uint16_t kWireMinor = 0;

struct Envelope {
  std::uint16_t major{kWireMajor};
  std::uint16_t minor{kWireMinor};
  MessageType type{MessageType::Ping};
  std::uint32_t flags{0};
  std::uint32_t payload_size{0};
  std::uint16_t fd_count{0};
  std::uint64_t sequence{0};
  std::uint64_t reply_to{0};
};

[[nodiscard]] std::array<std::uint8_t, kEnvelopeSize>
encode_envelope(const Envelope &envelope);

[[nodiscard]] CodecStatus decode_envelope(
    std::span<const std::uint8_t> record, std::size_t actual_fd_count,
    std::uint32_t maximum_payload, Envelope &envelope);

} // namespace gw::ipc::wire
