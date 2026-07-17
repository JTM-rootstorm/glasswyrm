#include "ipc/wire/session_contract.hpp"

#include "ipc/wire/byte_reader.hpp"
#include "ipc/wire/byte_writer.hpp"

namespace gw::ipc::wire {

std::vector<std::uint8_t> encode(const SessionStateChange &value) {
  ByteWriter writer;
  writer.u64(value.generation);
  writer.u16(static_cast<std::uint16_t>(value.state));
  writer.u16(0);
  writer.u32(value.flags);
  return std::move(writer).take();
}

CodecStatus decode(std::span<const std::uint8_t> bytes,
                   SessionStateChange &value) {
  ByteReader reader(bytes);
  SessionStateChange decoded;
  std::uint16_t state = 0;
  std::uint16_t reserved = 0;
  if (!reader.u64(decoded.generation) || !reader.u16(state) ||
      !reader.u16(reserved) || !reader.u32(decoded.flags))
    return CodecStatus::Truncated;
  if (!reader.done()) return CodecStatus::TrailingData;
  decoded.state = static_cast<SessionState>(state);
  if (!decoded.generation || state < 1 || state > 2 || decoded.flags ||
      reserved)
    return CodecStatus::InvalidValue;
  value = decoded;
  return CodecStatus::Ok;
}

std::vector<std::uint8_t> encode(
    const SessionStateAcknowledged &value) {
  ByteWriter writer;
  writer.u64(value.generation);
  writer.u16(static_cast<std::uint16_t>(value.state));
  writer.u16(static_cast<std::uint16_t>(value.result));
  writer.u32(value.flags);
  return std::move(writer).take();
}

CodecStatus decode(std::span<const std::uint8_t> bytes,
                   SessionStateAcknowledged &value) {
  ByteReader reader(bytes);
  SessionStateAcknowledged decoded;
  std::uint16_t state = 0;
  std::uint16_t result = 0;
  if (!reader.u64(decoded.generation) || !reader.u16(state) ||
      !reader.u16(result) || !reader.u32(decoded.flags))
    return CodecStatus::Truncated;
  if (!reader.done()) return CodecStatus::TrailingData;
  decoded.state = static_cast<SessionState>(state);
  decoded.result = static_cast<SessionStateResult>(result);
  if (!decoded.generation || state < 1 || state > 2 || result < 1 ||
      result > 4 || decoded.flags)
    return CodecStatus::InvalidValue;
  value = decoded;
  return CodecStatus::Ok;
}

} // namespace gw::ipc::wire
