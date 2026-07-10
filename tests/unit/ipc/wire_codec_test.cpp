#include "ipc/wire/control.hpp"
#include "ipc/wire/envelope.hpp"
#include "tests/helpers/test_support.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace {

using namespace gw::ipc::wire;
using gw::test::require;

std::vector<std::uint8_t> hex(const std::string_view text) {
  const auto digit = [](const char value) -> std::uint8_t {
    return static_cast<std::uint8_t>(value <= '9' ? value - '0'
                                                  : value - 'a' + 10);
  };
  std::vector<std::uint8_t> result;
  result.reserve(text.size() / 2);
  for (std::size_t index = 0; index < text.size(); index += 2) {
    result.push_back(static_cast<std::uint8_t>((digit(text[index]) << 4U) |
                                               digit(text[index + 1])));
  }
  return result;
}

void test_envelope_golden() {
  const Envelope envelope{1, 0, MessageType::Pong,
                          static_cast<std::uint32_t>(MessageFlag::Reply), 8, 0,
                          0x0102030405060708ULL, 0x1112131415161718ULL};
  constexpr std::array<std::uint8_t, 40> golden{
      'G',  'W',  'I',  'P',  40,   0,    1,    0,    0,    0,
      5,    0,    1,    0,    0,    0,    8,    0,    0,    0,
      0,    0,    0,    0,    0x08, 0x07, 0x06, 0x05, 0x04, 0x03,
      0x02, 0x01, 0x18, 0x17, 0x16, 0x15, 0x14, 0x13, 0x12, 0x11};
  require(encode_envelope(envelope) == golden,
          "envelope uses the exact 40-byte little-endian encoding");

  std::vector<std::uint8_t> record(golden.begin(), golden.end());
  record.insert(record.end(), 8, 0xa5);
  Envelope decoded;
  require(decode_envelope(record, 0, 65536, decoded) == CodecStatus::Ok &&
              decoded.type == MessageType::Pong && decoded.sequence == envelope.sequence &&
              decoded.reply_to == envelope.reply_to,
          "envelope golden decodes with reply correlation intact");

  const auto original = record;
  record[0] = 'X';
  require(decode_envelope(record, 0, 65536, decoded) ==
              CodecStatus::InvalidValue,
          "bad envelope magic is rejected");
  record = original;
  record[22] = 1;
  require(decode_envelope(record, 0, 65536, decoded) ==
              CodecStatus::InvalidValue,
          "nonzero envelope reserved field is rejected");
  record = original;
  record[12] = 0x80;
  require(decode_envelope(record, 0, 65536, decoded) ==
              CodecStatus::InvalidValue,
          "unknown envelope flags are rejected");
  require(decode_envelope(std::span(original).first(39), 0, 65536, decoded) ==
              CodecStatus::Truncated,
          "short envelope is rejected without reading past the record");
  require(decode_envelope(original, 1, 65536, decoded) ==
              CodecStatus::SizeMismatch,
          "declared and received descriptor counts must match");
}

void test_control_goldens() {
  Hello hello;
  hello.sender_role = Role::TestProducer;
  hello.offered_capabilities = 0x0102030405060708ULL;
  hello.required_capabilities =
      static_cast<std::uint64_t>(Capability::Snapshots);
  hello.maximum_payload = 65536;
  hello.maximum_fd_count = 4;
  for (std::size_t index = 0; index < hello.sender_instance_id.size(); ++index) {
    hello.sender_instance_id[index] = static_cast<std::uint8_t>(index + 1);
  }
  hello.name = "producer";
  const auto bytes = encode(hello);
  require(bytes == hex("0100000001000000040000000807060504030201020000000000"
                       "000000000100040008000102030405060708090a0b0c0d0e0f10"
                       "70726f6475636572"),
          "Hello matches its complete byte golden");
  require(bytes.size() == 60 && bytes[0] == 1 && bytes[8] == 4 &&
              bytes[28] == 0 && bytes[32] == 4 && bytes[34] == 8 &&
              bytes[48] == 13 && bytes[59] == 'r',
          "Hello has a deterministic fixed prefix and unpadded UTF-8 suffix");
  Hello decoded_hello;
  require(decode(bytes, decoded_hello) == CodecStatus::Ok &&
              decoded_hello.name == hello.name &&
              decoded_hello.offered_capabilities == hello.offered_capabilities &&
              decoded_hello.sender_instance_id == hello.sender_instance_id,
          "Hello round trips all negotiated fields");

  Welcome welcome;
  welcome.sender_role = Role::TestConsumer;
  welcome.negotiated_capabilities = 2;
  welcome.negotiated_maximum_payload = 32768;
  welcome.negotiated_maximum_fd_count = 2;
  welcome.connection_id = 99;
  welcome.sender_instance_id.fill(0xa5);
  Welcome decoded_welcome;
  require(encode(welcome) ==
              hex("010000000500000002000000000000000080000002000000630000"
                  "0000000000a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5") &&
              decode(encode(welcome), decoded_welcome) == CodecStatus::Ok &&
              decoded_welcome.connection_id == 99,
          "Welcome has its fixed wire size and round trips");

  Reject reject{RejectReason::RoleNotAllowed, 1, 0, 1, 0, "role"};
  Reject decoded_reject;
  require(encode(reject) ==
              hex("02000400010000000100000000000000726f6c65") &&
              decode(encode(reject), decoded_reject) == CodecStatus::Ok &&
              decoded_reject.reason == RejectReason::RoleNotAllowed,
          "Reject reason and bounded detail round trip");

  Ping ping{0x8877665544332211ULL};
  Pong pong;
  require(encode(ping) ==
              std::vector<std::uint8_t>({0x11, 0x22, 0x33, 0x44, 0x55, 0x66,
                                         0x77, 0x88}) &&
              encode(Pong{ping.nonce}) == encode(ping) &&
              decode(encode(Pong{ping.nonce}), pong) == CodecStatus::Ok &&
              pong.nonce == ping.nonce,
          "Ping and Pong use the exact nonce encoding");

  ProtocolError error{ProtocolErrorCode::MalformedPayload,
                      MessageType::SurfaceUpsert, 42, "bad surface"};
  ProtocolError decoded_error;
  require(encode(error) ==
              hex("02001001000000002a000000000000000b00000062616420737572"
                  "66616365") &&
              decode(encode(error), decoded_error) == CodecStatus::Ok &&
              decoded_error.offending_sequence == 42,
          "ProtocolError preserves its offending message correlation");

  SnapshotBegin begin{7, SnapshotDomain::Surfaces, 0, 8, 2};
  SnapshotEnd end{7, 8, 2};
  SnapshotAbort abort{7, 1, "cancelled"};
  SnapshotBegin decoded_begin;
  SnapshotEnd decoded_end;
  SnapshotAbort decoded_abort;
  require(encode(begin) ==
              hex("07000000000000000200000008000000000000000200000000000000") &&
              encode(end) ==
                  hex("070000000000000008000000000000000200000000000000") &&
              encode(abort) ==
                  hex("0700000000000000010009000000000063616e63656c6c6564") &&
              decode(encode(begin), decoded_begin) == CodecStatus::Ok &&
              decode(encode(end), decoded_end) == CodecStatus::Ok &&
              decode(encode(abort), decoded_abort) == CodecStatus::Ok,
          "all snapshot control payloads round trip at fixed widths");
}

void test_malformed_control() {
  Hello hello;
  hello.sender_role = Role::TestProducer;
  hello.maximum_payload = 4096;
  hello.maximum_fd_count = 1;
  hello.sender_instance_id[0] = 1;
  auto bytes = encode(hello);
  Hello decoded;
  for (std::size_t size = 0; size < bytes.size(); ++size) {
    require(decode(std::span(bytes).first(size), decoded) ==
                CodecStatus::Truncated,
            "every truncated Hello prefix is rejected");
  }
  bytes.push_back(0);
  require(decode(bytes, decoded) == CodecStatus::TrailingData,
          "Hello trailing data is rejected");

  hello.sender_instance_id.fill(0);
  require(decode(encode(hello), decoded) == CodecStatus::InvalidValue,
          "zero Hello instance IDs are rejected");
  hello.sender_instance_id[0] = 1;
  hello.name = std::string("\xc0\x80", 2);
  require(decode(encode(hello), decoded) == CodecStatus::InvalidValue,
          "overlong UTF-8 in a peer label is rejected");
  hello.name.assign(kMaximumInstanceLabel + 1, 'x');
  require(encode(hello).empty(), "oversized peer labels cannot be encoded");

  auto begin = encode(SnapshotBegin{5, SnapshotDomain::Test, 0, 2, 0});
  begin[10] = 1;
  SnapshotBegin decoded_begin;
  require(decode(begin, decoded_begin) == CodecStatus::InvalidValue,
          "unknown snapshot flags are rejected");
}

} // namespace

int main() {
  test_envelope_golden();
  test_control_goldens();
  test_malformed_control();
  return 0;
}
