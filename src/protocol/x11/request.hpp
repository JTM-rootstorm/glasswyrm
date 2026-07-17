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
inline constexpr std::size_t kBigRequestHeaderSize = 8;
inline constexpr std::size_t kMaximumBigRequestSize = 16U * 1024U * 1024U;
inline constexpr std::uint32_t kMaximumBigRequestLengthUnits =
    kMaximumBigRequestSize / 4U;

struct FramedRequest {
  std::uint8_t opcode{0};
  std::uint8_t data{0};
  std::uint32_t length_units{0};
  std::uint8_t header_size{kCoreRequestHeaderSize};
  std::vector<std::uint8_t> bytes;

  [[nodiscard]] std::span<const std::uint8_t> body() const noexcept {
    return std::span<const std::uint8_t>(bytes).subspan(header_size);
  }
  [[nodiscard]] std::size_t core_size() const noexcept {
    return bytes.size() - header_size + kCoreRequestHeaderSize;
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
  void enable_big_requests(
      std::uint32_t maximum_length_units = kMaximumBigRequestLengthUnits);
  [[nodiscard]] bool big_requests_enabled() const noexcept {
    return big_requests_enabled_;
  }
  void reset();

private:
  [[nodiscard]] RequestFrameStatus inspect_header() noexcept;
  [[nodiscard]] RequestFrameStatus inspect_extended_header() noexcept;

  ByteOrder order_;
  std::uint16_t maximum_length_units_;
  std::uint32_t maximum_big_length_units_{kMaximumBigRequestLengthUnits};
  bool big_requests_enabled_{false};
  std::size_t expected_size_{kCoreRequestHeaderSize};
  RequestFrameStatus status_{RequestFrameStatus::NeedMore};
  FramedRequest request_;
};

} // namespace gw::protocol::x11
