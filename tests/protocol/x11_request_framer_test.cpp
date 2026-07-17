#include "protocol/x11/request.hpp"
#include "tests/helpers/test_support.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace {

using gw::protocol::x11::ByteOrder;
using gw::protocol::x11::RequestFrameStatus;
using gw::protocol::x11::RequestFramer;
using gw::test::require;

std::vector<std::uint8_t> make_request(const ByteOrder order,
                                       const std::uint8_t opcode,
                                       const std::uint8_t data,
                                       const std::uint16_t units) {
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(units) * 4, 0);
  bytes[0] = opcode;
  bytes[1] = data;
  if (order == ByteOrder::LittleEndian) {
    bytes[2] = static_cast<std::uint8_t>(units);
    bytes[3] = static_cast<std::uint8_t>(units >> 8U);
  } else {
    bytes[2] = static_cast<std::uint8_t>(units >> 8U);
    bytes[3] = static_cast<std::uint8_t>(units);
  }
  for (std::size_t index = 4; index < bytes.size(); ++index) {
    bytes[index] = static_cast<std::uint8_t>(index);
  }
  return bytes;
}

std::vector<std::uint8_t> make_big_request(const ByteOrder order,
                                           const std::uint8_t opcode,
                                           const std::uint8_t data,
                                           const std::uint32_t units) {
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(units) * 4, 0);
  bytes[0] = opcode;
  bytes[1] = data;
  if (order == ByteOrder::LittleEndian) {
    for (std::size_t index = 0; index < 4; ++index)
      bytes[4 + index] = static_cast<std::uint8_t>(units >> (index * 8U));
  } else {
    for (std::size_t index = 0; index < 4; ++index)
      bytes[4 + index] = static_cast<std::uint8_t>(units >> ((3U - index) * 8U));
  }
  for (std::size_t index = 8; index < bytes.size(); ++index)
    bytes[index] = static_cast<std::uint8_t>(index);
  return bytes;
}

void test_fragmentation(const ByteOrder order) {
  const auto bytes = make_request(order, 18, 2, 6);
  for (std::size_t boundary = 0; boundary <= bytes.size(); ++boundary) {
    RequestFramer framer(order);
    const auto first = framer.consume(std::span(bytes).first(boundary));
    require(first.consumed == boundary, "request prefix is consumed");
    if (boundary != bytes.size()) {
      require(first.status == RequestFrameStatus::NeedMore,
              "request prefix remains incomplete");
    }
    const auto second = framer.consume(std::span(bytes).subspan(boundary));
    require(second.status == RequestFrameStatus::Complete,
            "request completes at every split point");
    require(framer.request().opcode == 18 && framer.request().data == 2 &&
                framer.request().length_units == 6,
            "request header is decoded");
    require(framer.request().bytes == bytes, "complete request bytes are retained");
    require(framer.request().body().size() == bytes.size() - 4,
            "request body excludes the ordinary header");
  }

  RequestFramer bytewise(order);
  for (const auto byte : bytes) {
    const std::array<std::uint8_t, 1> fragment{byte};
    require(bytewise.consume(fragment).consumed == 1,
            "one-byte request fragment is consumed");
  }
  require(bytewise.eof() == RequestFrameStatus::Complete,
          "one-byte delivery completes");
}

void test_pipeline(const ByteOrder order) {
  const auto first = make_request(order, 127, 0, 1);
  const auto second = make_request(order, 43, 0, 1);
  std::vector<std::uint8_t> pipeline = first;
  pipeline.insert(pipeline.end(), second.begin(), second.end());

  RequestFramer framer(order);
  const auto first_result = framer.consume(pipeline);
  require(first_result.status == RequestFrameStatus::Complete &&
              first_result.consumed == first.size(),
          "framer stops exactly after one pipelined request");
  require(framer.request().opcode == 127, "first pipelined opcode is retained");

  framer.reset();
  const auto second_result =
      framer.consume(std::span(pipeline).subspan(first_result.consumed));
  require(second_result.status == RequestFrameStatus::Complete &&
              second_result.consumed == second.size() &&
              framer.request().opcode == 43,
          "caller can immediately frame the following request");
}

void test_two_unit_pipeline(const ByteOrder order) {
  const auto first = make_request(order, 8, 0, 2);
  const auto second = make_request(order, 43, 0, 1);
  std::vector<std::uint8_t> pipeline = first;
  pipeline.insert(pipeline.end(), second.begin(), second.end());

  RequestFramer framer(order);
  const auto first_result = framer.consume(pipeline);
  require(first_result.status == RequestFrameStatus::Complete &&
              first_result.consumed == first.size() &&
              framer.request().header_size == 4,
          "ordinary two-unit request is not parsed as a BIG-REQUESTS header");
  framer.reset();
  const auto second_result =
      framer.consume(std::span(pipeline).subspan(first_result.consumed));
  require(second_result.status == RequestFrameStatus::Complete &&
              second_result.consumed == second.size(),
          "request after an ordinary two-unit request remains framed");
}

void test_invalid_lengths() {
  for (const auto order : {ByteOrder::LittleEndian, ByteOrder::BigEndian}) {
    std::array<std::uint8_t, 4> zero{18, 0, 0, 0};
    RequestFramer zero_framer(order);
    const auto zero_result = zero_framer.consume(zero);
    require(zero_result.status == RequestFrameStatus::ZeroLength &&
                zero_result.consumed == zero.size(),
            "BIG-REQUESTS zero length is rejected after its header");

    const auto oversized = make_request(order, 18, 0, 3);
    RequestFramer capped(order, 2);
    const auto oversized_result = capped.consume(oversized);
    require(oversized_result.status == RequestFrameStatus::TooLarge &&
                oversized_result.consumed == 4,
            "oversized body is not consumed or retained");

    RequestFramer partial_header(order);
    require(partial_header.consume(std::span(oversized).first(3)).status ==
                RequestFrameStatus::NeedMore &&
                partial_header.eof() == RequestFrameStatus::TruncatedInput,
            "EOF reports a partial header");

    RequestFramer partial_body(order);
    require(partial_body.consume(std::span(oversized).first(7)).status ==
                RequestFrameStatus::NeedMore &&
                partial_body.eof() == RequestFrameStatus::TruncatedInput,
            "EOF reports a partial request body");
  }

  bool threw = false;
  try {
    RequestFramer invalid(ByteOrder::LittleEndian, 0);
  } catch (const std::invalid_argument &) {
    threw = true;
  }
  require(threw, "a zero server request limit is rejected");
}

void test_big_requests(const ByteOrder order) {
  const auto bytes = make_big_request(order, 72, 2, 6);
  for (std::size_t boundary = 0; boundary <= bytes.size(); ++boundary) {
    RequestFramer framer(order);
    framer.enable_big_requests();
    const auto first = framer.consume(std::span(bytes).first(boundary));
    require(first.consumed == boundary, "BIG-REQUESTS prefix is consumed");
    const auto second = framer.consume(std::span(bytes).subspan(boundary));
    require(second.status == RequestFrameStatus::Complete,
            "BIG-REQUESTS completes at every header and body boundary");
    require(framer.request().length_units == 6 &&
                framer.request().header_size == 8 &&
                framer.request().body().size() == 16,
            "extended length and body offset are decoded");
  }

  RequestFramer capped(order);
  capped.enable_big_requests(4);
  const auto too_large = make_big_request(order, 72, 2, 5);
  require(capped.consume(too_large).status == RequestFrameStatus::TooLarge &&
              capped.expected_size() == 8,
          "one unit above the configured BIG-REQUESTS limit is rejected");

  const auto larger_than_core = make_big_request(
      order, 72, 2,
      static_cast<std::uint32_t>(
          gw::protocol::x11::kMaximumCoreRequestLengthUnits) +
          2U);
  RequestFramer large(order);
  large.enable_big_requests();
  require(large.consume(larger_than_core).status ==
              RequestFrameStatus::Complete &&
              large.request().bytes.size() >
                  gw::protocol::x11::kMaximumCoreRequestSize,
          "enabled framing accepts a request above the normal core limit");

  const auto extended = make_big_request(order, 127, 0, 2);
  const auto ordinary = make_request(order, 43, 0, 1);
  std::vector<std::uint8_t> pipeline = extended;
  pipeline.insert(pipeline.end(), ordinary.begin(), ordinary.end());
  RequestFramer pipelined(order);
  pipelined.enable_big_requests();
  const auto first = pipelined.consume(pipeline);
  require(first.status == RequestFrameStatus::Complete &&
              first.consumed == extended.size(),
          "extended framing preserves following pipelined bytes");
  pipelined.reset();
  const auto second =
      pipelined.consume(std::span(pipeline).subspan(first.consumed));
  require(second.status == RequestFrameStatus::Complete &&
              pipelined.request().opcode == 43,
          "ordinary request follows an extended request");
}

} // namespace

int main() {
  test_fragmentation(ByteOrder::LittleEndian);
  test_fragmentation(ByteOrder::BigEndian);
  test_pipeline(ByteOrder::LittleEndian);
  test_pipeline(ByteOrder::BigEndian);
  test_two_unit_pipeline(ByteOrder::LittleEndian);
  test_two_unit_pipeline(ByteOrder::BigEndian);
  test_invalid_lengths();
  test_big_requests(ByteOrder::LittleEndian);
  test_big_requests(ByteOrder::BigEndian);
  return 0;
}
