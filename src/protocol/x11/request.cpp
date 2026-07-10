#include "protocol/x11/request.hpp"

#include "core/checked_math.hpp"
#include "protocol/x11/byte_cursor.hpp"

#include <algorithm>
#include <stdexcept>

namespace gw::protocol::x11 {

RequestFramer::RequestFramer(const ByteOrder order,
                             const std::uint16_t maximum_length_units)
    : order_(order), maximum_length_units_(maximum_length_units) {
  if (maximum_length_units_ == 0) {
    throw std::invalid_argument("X11 request length limit must be nonzero");
  }
  request_.bytes.reserve(kCoreRequestHeaderSize);
}

RequestFrameResult
RequestFramer::consume(const std::span<const std::uint8_t> input) {
  if (status_ != RequestFrameStatus::NeedMore) {
    return {status_, 0};
  }

  std::size_t consumed = 0;
  while (consumed < input.size() && status_ == RequestFrameStatus::NeedMore) {
    const auto needed = expected_size_ - request_.bytes.size();
    const auto copy_size = std::min(needed, input.size() - consumed);
    request_.bytes.insert(
        request_.bytes.end(),
        input.begin() + static_cast<std::ptrdiff_t>(consumed),
        input.begin() + static_cast<std::ptrdiff_t>(consumed + copy_size));
    consumed += copy_size;

    if (request_.bytes.size() == kCoreRequestHeaderSize &&
        expected_size_ == kCoreRequestHeaderSize) {
      status_ = inspect_header();
    }
    if (status_ == RequestFrameStatus::NeedMore &&
        request_.bytes.size() == expected_size_) {
      status_ = RequestFrameStatus::Complete;
    }
  }
  return {status_, consumed};
}

RequestFrameStatus RequestFramer::eof() const noexcept {
  if (status_ == RequestFrameStatus::NeedMore && !request_.bytes.empty()) {
    return RequestFrameStatus::TruncatedInput;
  }
  return status_;
}

void RequestFramer::reset() {
  expected_size_ = kCoreRequestHeaderSize;
  status_ = RequestFrameStatus::NeedMore;
  request_ = {};
  request_.bytes.reserve(kCoreRequestHeaderSize);
}

RequestFrameStatus RequestFramer::inspect_header() noexcept {
  request_.opcode = request_.bytes[0];
  request_.data = request_.bytes[1];
  ByteReader reader(std::span<const std::uint8_t>(request_.bytes).subspan(2),
                    order_);
  if (!reader.read_u16(request_.length_units)) {
    return RequestFrameStatus::TruncatedInput;
  }
  if (request_.length_units == 0) {
    return RequestFrameStatus::ZeroLength;
  }
  if (request_.length_units > maximum_length_units_) {
    return RequestFrameStatus::TooLarge;
  }
  const auto size = core::checked_multiply(
      static_cast<std::size_t>(request_.length_units), std::size_t{4});
  if (!size || *size < kCoreRequestHeaderSize) {
    return RequestFrameStatus::TooLarge;
  }
  expected_size_ = *size;
  request_.bytes.reserve(expected_size_);
  return RequestFrameStatus::NeedMore;
}

} // namespace gw::protocol::x11
