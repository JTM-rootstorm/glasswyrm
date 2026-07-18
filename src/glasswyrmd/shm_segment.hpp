#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace glasswyrm::server {

class SysvShmMapping {
 public:
  SysvShmMapping(void* address, std::size_t size) noexcept
      : address_(address), size_(size) {}
  ~SysvShmMapping();
  SysvShmMapping(const SysvShmMapping&) = delete;
  SysvShmMapping& operator=(const SysvShmMapping&) = delete;

  [[nodiscard]] std::span<const std::uint8_t> bytes() const noexcept {
    return {static_cast<const std::uint8_t*>(address_), size_};
  }
  [[nodiscard]] std::span<std::uint8_t> mutable_bytes() noexcept {
    return {static_cast<std::uint8_t*>(address_), size_};
  }

 private:
  void* address_{};
  std::size_t size_{};
};

struct ShmSegmentResource {
  int shmid{-1};
  std::size_t size{};
  bool read_only{true};
  std::uint32_t peer_uid{};
  std::shared_ptr<SysvShmMapping> mapping;
};

}  // namespace glasswyrm::server
