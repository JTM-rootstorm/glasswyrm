#pragma once

#include "protocol/x11/byte_order.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace gw::protocol::x11 {

class ByteReader {
public:
  ByteReader(std::span<const std::uint8_t> bytes, ByteOrder order) noexcept
      : bytes_(bytes), order_(order) {}

  [[nodiscard]] bool skip(std::size_t count) noexcept {
    if (count > remaining()) {
      return false;
    }
    offset_ += count;
    return true;
  }

  [[nodiscard]] bool read_u16(std::uint16_t &value) noexcept {
    if (remaining() < 2) {
      return false;
    }
    if (order_ == ByteOrder::LittleEndian) {
      value = static_cast<std::uint16_t>(bytes_[offset_]) |
              static_cast<std::uint16_t>(bytes_[offset_ + 1] << 8U);
    } else {
      value = static_cast<std::uint16_t>(bytes_[offset_] << 8U) |
              static_cast<std::uint16_t>(bytes_[offset_ + 1]);
    }
    offset_ += 2;
    return true;
  }

  [[nodiscard]] std::size_t remaining() const noexcept {
    return bytes_.size() - offset_;
  }

private:
  std::span<const std::uint8_t> bytes_;
  ByteOrder order_;
  std::size_t offset_{0};
};

class ByteWriter {
public:
  explicit ByteWriter(ByteOrder order) noexcept : order_(order) {}

  void write_u8(std::uint8_t value) { bytes_.push_back(value); }

  void write_u16(std::uint16_t value) {
    if (order_ == ByteOrder::LittleEndian) {
      bytes_.push_back(static_cast<std::uint8_t>(value));
      bytes_.push_back(static_cast<std::uint8_t>(value >> 8U));
    } else {
      bytes_.push_back(static_cast<std::uint8_t>(value >> 8U));
      bytes_.push_back(static_cast<std::uint8_t>(value));
    }
  }

  void write_u32(std::uint32_t value) {
    if (order_ == ByteOrder::LittleEndian) {
      bytes_.push_back(static_cast<std::uint8_t>(value));
      bytes_.push_back(static_cast<std::uint8_t>(value >> 8U));
      bytes_.push_back(static_cast<std::uint8_t>(value >> 16U));
      bytes_.push_back(static_cast<std::uint8_t>(value >> 24U));
    } else {
      bytes_.push_back(static_cast<std::uint8_t>(value >> 24U));
      bytes_.push_back(static_cast<std::uint8_t>(value >> 16U));
      bytes_.push_back(static_cast<std::uint8_t>(value >> 8U));
      bytes_.push_back(static_cast<std::uint8_t>(value));
    }
  }

  void write_bytes(std::span<const std::uint8_t> bytes) {
    bytes_.insert(bytes_.end(), bytes.begin(), bytes.end());
  }

  void write_padding(std::size_t count) {
    bytes_.insert(bytes_.end(), count, std::uint8_t{0});
  }

  [[nodiscard]] std::size_t size() const noexcept { return bytes_.size(); }
  [[nodiscard]] std::vector<std::uint8_t> take() && {
    return std::move(bytes_);
  }

private:
  ByteOrder order_;
  std::vector<std::uint8_t> bytes_;
};

} // namespace gw::protocol::x11
