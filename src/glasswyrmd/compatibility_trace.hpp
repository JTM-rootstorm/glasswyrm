#pragma once

#include "protocol/x11/byte_order.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace glasswyrm::server {

class CompatibilityTrace {
 public:
  static constexpr std::size_t kMaximumBytes = 64U * 1024U * 1024U;

  static std::unique_ptr<CompatibilityTrace> create(const std::string& path,
                                                     std::string& error);
  ~CompatibilityTrace();

  CompatibilityTrace(const CompatibilityTrace&) = delete;
  CompatibilityTrace& operator=(const CompatibilityTrace&) = delete;

  void connection(std::uint64_t client, std::string_view outcome);
  void request(std::uint64_t client, std::uint64_t sequence,
               std::uint8_t opcode, std::size_t length,
               const std::vector<std::uint8_t>& output,
               std::span<const std::uint8_t> request_bytes = {},
               gw::protocol::x11::ByteOrder byte_order =
                   gw::protocol::x11::ByteOrder::LittleEndian);
  void packet(std::uint64_t client, std::uint64_t sequence,
              const std::vector<std::uint8_t>& bytes,
              gw::protocol::x11::ByteOrder byte_order =
                  gw::protocol::x11::ByteOrder::LittleEndian);

  [[nodiscard]] bool enabled() const noexcept { return descriptor_ >= 0; }
  [[nodiscard]] std::size_t bytes_written() const noexcept { return bytes_; }

 private:
  explicit CompatibilityTrace(int descriptor) : descriptor_(descriptor) {}
  bool append(std::string_view line);

  int descriptor_{-1};
  std::size_t bytes_{0};
};

[[nodiscard]] std::string_view x11_request_name(std::uint8_t opcode) noexcept;

}  // namespace glasswyrm::server
