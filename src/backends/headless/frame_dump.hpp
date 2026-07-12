#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>

namespace glasswyrm::headless {

struct FrameDumpMetadata {
  std::uint64_t frame{0};
  std::uint64_t commit_id{0};
  std::uint64_t generation{0};
  std::uint64_t output_id{0};
  std::uint32_t width{0};
  std::uint32_t height{0};
  std::uint32_t damage_rectangles{0};
};

struct FrameDumpResult {
  std::filesystem::path file;
  std::uint64_t fnv1a64{0};
};

[[nodiscard]] std::uint64_t fnv1a64(std::span<const std::uint8_t> bytes) noexcept;

class FrameDumper {
 public:
  explicit FrameDumper(std::filesystem::path directory)
      : directory_(std::move(directory)) {}

  [[nodiscard]] bool dump(const FrameDumpMetadata& metadata,
                          std::span<const std::uint32_t> xrgb_pixels,
                          FrameDumpResult& result, std::string& error) const;

 private:
  std::filesystem::path directory_;
};

}  // namespace glasswyrm::headless
