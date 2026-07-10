#include "ipc/wire/envelope.hpp"

#include "ipc/wire/byte_reader.hpp"
#include "ipc/wire/byte_writer.hpp"

#include <algorithm>
#include <limits>

namespace gw::ipc::wire {

std::array<std::uint8_t, kEnvelopeSize>
encode_envelope(const Envelope &envelope) {
  ByteWriter writer;
  writer.bytes(std::array<std::uint8_t, 4>{'G', 'W', 'I', 'P'});
  writer.u16(static_cast<std::uint16_t>(kEnvelopeSize));
  writer.u16(envelope.major);
  writer.u16(envelope.minor);
  writer.u16(static_cast<std::uint16_t>(envelope.type));
  writer.u32(envelope.flags);
  writer.u32(envelope.payload_size);
  writer.u16(envelope.fd_count);
  writer.u16(0);
  writer.u64(envelope.sequence);
  writer.u64(envelope.reply_to);
  const auto bytes = std::move(writer).take();
  std::array<std::uint8_t, kEnvelopeSize> result{};
  std::copy(bytes.begin(), bytes.end(), result.begin());
  return result;
}

CodecStatus decode_envelope(const std::span<const std::uint8_t> record,
                            const std::size_t actual_fd_count,
                            const std::uint32_t maximum_payload,
                            Envelope &envelope) {
  if (record.size() < kEnvelopeSize) {
    return CodecStatus::Truncated;
  }
  ByteReader reader(record.first(kEnvelopeSize));
  std::span<const std::uint8_t> magic;
  std::uint16_t header_size = 0;
  std::uint16_t type = 0;
  std::uint16_t reserved = 0;
  Envelope decoded;
  if (!reader.bytes(4, magic) || !reader.u16(header_size) ||
      !reader.u16(decoded.major) || !reader.u16(decoded.minor) ||
      !reader.u16(type) || !reader.u32(decoded.flags) ||
      !reader.u32(decoded.payload_size) || !reader.u16(decoded.fd_count) ||
      !reader.u16(reserved) || !reader.u64(decoded.sequence) ||
      !reader.u64(decoded.reply_to)) {
    return CodecStatus::Truncated;
  }
  constexpr std::array<std::uint8_t, 4> expected_magic{'G', 'W', 'I', 'P'};
  if (!std::equal(magic.begin(), magic.end(), expected_magic.begin()) ||
      header_size != kEnvelopeSize || reserved != 0 || decoded.sequence == 0 ||
      (decoded.flags & ~kKnownMessageFlags) != 0) {
    return CodecStatus::InvalidValue;
  }
  if (decoded.payload_size > maximum_payload ||
      actual_fd_count > std::numeric_limits<std::uint16_t>::max()) {
    return CodecStatus::LimitExceeded;
  }
  if (decoded.fd_count != actual_fd_count ||
      record.size() - kEnvelopeSize != decoded.payload_size) {
    return CodecStatus::SizeMismatch;
  }
  const bool reply = has_flag(decoded.flags, MessageFlag::Reply);
  const bool error = has_flag(decoded.flags, MessageFlag::Error);
  if ((reply && decoded.reply_to == 0) || (!reply && decoded.reply_to != 0) ||
      (error && !reply)) {
    return CodecStatus::InvalidValue;
  }
  decoded.type = static_cast<MessageType>(type);
  envelope = decoded;
  return CodecStatus::Ok;
}

} // namespace gw::ipc::wire
