#pragma once

#include "protocol/x11/byte_order.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace gw::protocol::x11 {

inline constexpr std::size_t kCoreRequestHeaderSize = 4;
inline constexpr std::uint16_t kMaximumCoreRequestLengthUnits = 65535;
inline constexpr std::size_t kMaximumCoreRequestSize =
    static_cast<std::size_t>(kMaximumCoreRequestLengthUnits) * 4;

struct FramedRequest {
  std::uint8_t opcode{0};
  std::uint8_t data{0};
  std::uint16_t length_units{0};
  std::vector<std::uint8_t> bytes;

  [[nodiscard]] std::span<const std::uint8_t> body() const noexcept {
    return std::span<const std::uint8_t>(bytes).subspan(kCoreRequestHeaderSize);
  }
};

enum class RequestFrameStatus {
  NeedMore,
  Complete,
  ZeroLength,
  TooLarge,
  TruncatedInput,
};

struct RequestFrameResult {
  RequestFrameStatus status{RequestFrameStatus::NeedMore};
  std::size_t consumed{0};
};

class RequestFramer {
public:
  explicit RequestFramer(
      ByteOrder order,
      std::uint16_t maximum_length_units = kMaximumCoreRequestLengthUnits);

  [[nodiscard]] RequestFrameResult consume(std::span<const std::uint8_t> input);
  [[nodiscard]] RequestFrameStatus eof() const noexcept;
  [[nodiscard]] const FramedRequest &request() const noexcept { return request_; }
  [[nodiscard]] std::size_t expected_size() const noexcept {
    return expected_size_;
  }
  void reset();

private:
  [[nodiscard]] RequestFrameStatus inspect_header() noexcept;

  ByteOrder order_;
  std::uint16_t maximum_length_units_;
  std::size_t expected_size_{kCoreRequestHeaderSize};
  RequestFrameStatus status_{RequestFrameStatus::NeedMore};
  FramedRequest request_;
};

} // namespace gw::protocol::x11
