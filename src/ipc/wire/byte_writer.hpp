#pragma once

#include <cstdint>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace gw::ipc::wire {

class ByteWriter {
public:
  void u8(const std::uint8_t value) { bytes_.push_back(value); }

  void u16(const std::uint16_t value) {
    u8(static_cast<std::uint8_t>(value));
    u8(static_cast<std::uint8_t>(value >> 8U));
  }

  void u32(const std::uint32_t value) {
    u16(static_cast<std::uint16_t>(value));
    u16(static_cast<std::uint16_t>(value >> 16U));
  }

  void i32(const std::int32_t value) {
    u32(static_cast<std::uint32_t>(value));
  }

  void u64(const std::uint64_t value) {
    u32(static_cast<std::uint32_t>(value));
    u32(static_cast<std::uint32_t>(value >> 32U));
  }

  void bytes(const std::span<const std::uint8_t> value) {
    bytes_.insert(bytes_.end(), value.begin(), value.end());
  }

  void string(const std::string_view value) {
    bytes(std::span<const std::uint8_t>(
        reinterpret_cast<const std::uint8_t *>(value.data()), value.size()));
  }

  [[nodiscard]] std::vector<std::uint8_t> take() && {
    return std::move(bytes_);
  }

private:
  std::vector<std::uint8_t> bytes_;
};

} // namespace gw::ipc::wire
