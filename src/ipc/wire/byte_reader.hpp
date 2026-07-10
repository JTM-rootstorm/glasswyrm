#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace gw::ipc::wire {

class ByteReader {
public:
  explicit ByteReader(const std::span<const std::uint8_t> bytes) : bytes_(bytes) {}

  [[nodiscard]] bool u8(std::uint8_t &value) {
    if (remaining() < 1) {
      return false;
    }
    value = bytes_[offset_++];
    return true;
  }

  [[nodiscard]] bool u16(std::uint16_t &value) {
    std::uint8_t low = 0;
    std::uint8_t high = 0;
    if (!u8(low) || !u8(high)) {
      return false;
    }
    value = static_cast<std::uint16_t>(low) |
            static_cast<std::uint16_t>(high << 8U);
    return true;
  }

  [[nodiscard]] bool u32(std::uint32_t &value) {
    std::uint16_t low = 0;
    std::uint16_t high = 0;
    if (!u16(low) || !u16(high)) {
      return false;
    }
    value = static_cast<std::uint32_t>(low) |
            (static_cast<std::uint32_t>(high) << 16U);
    return true;
  }

  [[nodiscard]] bool i32(std::int32_t &value) {
    std::uint32_t bits = 0;
    if (!u32(bits)) {
      return false;
    }
    value = static_cast<std::int32_t>(bits);
    return true;
  }

  [[nodiscard]] bool u64(std::uint64_t &value) {
    std::uint32_t low = 0;
    std::uint32_t high = 0;
    if (!u32(low) || !u32(high)) {
      return false;
    }
    value = static_cast<std::uint64_t>(low) |
            (static_cast<std::uint64_t>(high) << 32U);
    return true;
  }

  [[nodiscard]] bool bytes(const std::size_t size,
                           std::span<const std::uint8_t> &value) {
    if (remaining() < size) {
      return false;
    }
    value = bytes_.subspan(offset_, size);
    offset_ += size;
    return true;
  }

  [[nodiscard]] bool string(const std::size_t size, std::string &value) {
    std::span<const std::uint8_t> data;
    if (!bytes(size, data)) {
      return false;
    }
    value.assign(reinterpret_cast<const char *>(data.data()), data.size());
    return true;
  }

  [[nodiscard]] std::size_t remaining() const noexcept {
    return bytes_.size() - offset_;
  }

  [[nodiscard]] bool done() const noexcept { return offset_ == bytes_.size(); }

private:
  std::span<const std::uint8_t> bytes_;
  std::size_t offset_{0};
};

} // namespace gw::ipc::wire
