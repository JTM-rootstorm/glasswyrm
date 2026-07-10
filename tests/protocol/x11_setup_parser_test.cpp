#include "core/checked_math.hpp"
#include "protocol/x11/setup.hpp"
#include "tests/helpers/test_support.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace {

using gw::protocol::x11::ByteOrder;
using gw::protocol::x11::ParseStatus;
using gw::protocol::x11::SetupDecision;
using gw::protocol::x11::SetupParser;
using gw::test::require;

void append_u16(std::vector<std::uint8_t> &bytes, const std::uint16_t value,
                const ByteOrder order) {
  if (order == ByteOrder::LittleEndian) {
    bytes.push_back(static_cast<std::uint8_t>(value));
    bytes.push_back(static_cast<std::uint8_t>(value >> 8U));
  } else {
    bytes.push_back(static_cast<std::uint8_t>(value >> 8U));
    bytes.push_back(static_cast<std::uint8_t>(value));
  }
}

std::vector<std::uint8_t>
request_bytes(const ByteOrder order, const std::uint16_t major = 11,
              const std::uint16_t minor = 0,
              const std::span<const std::uint8_t> authorization_name = {},
              const std::span<const std::uint8_t> authorization_data = {}) {
  std::vector<std::uint8_t> bytes;
  bytes.push_back(static_cast<std::uint8_t>(order));
  bytes.push_back(0);
  append_u16(bytes, major, order);
  append_u16(bytes, minor, order);
  append_u16(bytes, static_cast<std::uint16_t>(authorization_name.size()),
             order);
  append_u16(bytes, static_cast<std::uint16_t>(authorization_data.size()),
             order);
  append_u16(bytes, 0, order);
  bytes.insert(bytes.end(), authorization_name.begin(),
               authorization_name.end());
  bytes.insert(bytes.end(), (4 - authorization_name.size() % 4) % 4, 0);
  bytes.insert(bytes.end(), authorization_data.begin(),
               authorization_data.end());
  bytes.insert(bytes.end(), (4 - authorization_data.size() % 4) % 4, 0);
  return bytes;
}

void test_valid_order(const ByteOrder order) {
  const auto bytes = request_bytes(order);
  SetupParser parser;
  const auto result = parser.consume(bytes);
  require(result.status == ParseStatus::Complete, "valid request completes");
  require(result.consumed == bytes.size(), "valid request is fully consumed");
  require(parser.request().byte_order == order, "byte order is retained");
  require(parser.request().protocol_major == 11 &&
              parser.request().protocol_minor == 0,
          "protocol version is decoded");
  require(gw::protocol::x11::evaluate_setup_request(parser.request()) ==
              SetupDecision::Accepted,
          "11.0 without authorization is accepted");
}

void test_every_fragmentation_boundary(const ByteOrder order) {
  const std::array<std::uint8_t, 3> name{'a', 'b', 'c'};
  const std::array<std::uint8_t, 2> data{0x55, 0xaa};
  const auto bytes = request_bytes(order, 11, 0, name, data);
  for (std::size_t boundary = 0; boundary <= bytes.size(); ++boundary) {
    SetupParser parser;
    const auto first = parser.consume(std::span(bytes).first(boundary));
    require(first.consumed == boundary, "fragment prefix is consumed");
    if (boundary != bytes.size()) {
      require(first.status == ParseStatus::NeedMore,
              "fragment prefix requests more input");
    }
    const auto second = parser.consume(std::span(bytes).subspan(boundary));
    require(second.status == ParseStatus::Complete,
            "request completes after every split point");
    require(parser.request().authorization_name ==
                std::vector<std::uint8_t>(name.begin(), name.end()),
            "authorization name excludes padding");
    require(parser.request().authorization_data ==
                std::vector<std::uint8_t>(data.begin(), data.end()),
            "authorization data excludes padding");
  }

  SetupParser parser;
  for (const auto byte : bytes) {
    const std::array<std::uint8_t, 1> fragment{byte};
    const auto result = parser.consume(fragment);
    require(result.consumed == 1, "one-byte fragment is consumed");
  }
  require(parser.eof() == ParseStatus::Complete, "one-byte delivery completes");
}

void test_authorization_padding() {
  for (std::uint8_t length = 1; length <= 4; ++length) {
    const std::vector<std::uint8_t> value(length, length);
    const auto bytes =
        request_bytes(ByteOrder::LittleEndian, 11, 0, value, value);
    SetupParser parser;
    require(parser.consume(bytes).status == ParseStatus::Complete,
            "padded authorization request completes");
    require(parser.expected_size() == 12 + ((length + 3) & ~3U) * 2,
            "padded authorization size is exact");
    require(parser.request().authorization_name == value &&
                parser.request().authorization_data == value,
            "authorization payloads exclude their padding");
    require(gw::protocol::x11::evaluate_setup_request(parser.request()) ==
                SetupDecision::UnsupportedAuthorization,
            "non-empty authorization is rejected");
  }
}

void test_errors_and_boundaries() {
  SetupParser invalid_marker;
  const std::array<std::uint8_t, 3> invalid{'x', 0, 0};
  const auto invalid_result = invalid_marker.consume(invalid);
  require(invalid_result.status == ParseStatus::InvalidByteOrder &&
              invalid_result.consumed == 1,
          "invalid byte order fails before multi-byte decoding");

  for (const auto version : std::array<std::array<std::uint16_t, 2>, 2>{
           std::array<std::uint16_t, 2>{10, 0},
           std::array<std::uint16_t, 2>{11, 1}}) {
    const auto bytes =
        request_bytes(ByteOrder::BigEndian, version[0], version[1]);
    SetupParser parser;
    require(parser.consume(bytes).status == ParseStatus::Complete,
            "unsupported version remains safely framed");
    require(gw::protocol::x11::evaluate_setup_request(parser.request()) ==
                SetupDecision::UnsupportedVersion,
            "exact 11.0 version policy is enforced");
  }

  const auto valid = request_bytes(ByteOrder::LittleEndian);
  for (std::size_t length = 0; length < valid.size(); ++length) {
    SetupParser parser;
    require(parser.consume(std::span(valid).first(length)).status ==
                ParseStatus::NeedMore,
            "truncated fixed header needs more input");
    require(parser.eof() == ParseStatus::TruncatedInput,
            "EOF reports a truncated fixed header");
  }

  const std::array<std::uint8_t, 3> auth{'a', 'b', 'c'};
  const auto with_auth =
      request_bytes(ByteOrder::LittleEndian, 11, 0, auth, auth);
  for (std::size_t length = 12; length < with_auth.size(); ++length) {
    SetupParser parser;
    require(parser.consume(std::span(with_auth).first(length)).status ==
                ParseStatus::NeedMore,
            "truncated authorization body needs more input");
    require(parser.eof() == ParseStatus::TruncatedInput,
            "EOF reports a truncated authorization body");
  }

  SetupParser capped(15);
  require(capped.consume(with_auth).status == ParseStatus::MessageTooLarge,
          "declared padded setup size is capped");

  SetupParser too_small(11);
  require(too_small.consume(valid).status == ParseStatus::MessageTooLarge,
          "a cap smaller than the header is rejected");

  auto extra = valid;
  extra.push_back(1);
  extra.push_back(2);
  SetupParser bounded;
  const auto bounded_result = bounded.consume(extra);
  require(bounded_result.status == ParseStatus::Complete &&
              bounded_result.consumed == valid.size(),
          "parser leaves post-setup bytes for the connection state machine");
}

void test_checked_math() {
  constexpr auto maximum = std::numeric_limits<std::size_t>::max();
  require(!gw::core::checked_add(maximum, 1), "addition overflow is rejected");
  require(!gw::core::checked_multiply(maximum, 2),
          "multiplication overflow is rejected");
  require(!gw::core::checked_align_up(maximum, 4),
          "alignment overflow is rejected");
  require(!gw::core::checked_align_up(4, 3),
          "non-power-of-two alignment is rejected");
  require(gw::core::checked_align_up(5, 4) == 8,
          "alignment rounds up without overflow");
}

} // namespace

int main() {
  test_valid_order(ByteOrder::LittleEndian);
  test_valid_order(ByteOrder::BigEndian);
  test_every_fragmentation_boundary(ByteOrder::LittleEndian);
  test_every_fragmentation_boundary(ByteOrder::BigEndian);
  test_authorization_padding();
  test_errors_and_boundaries();
  test_checked_math();
  return 0;
}
