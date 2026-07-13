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

class FrameDumper;

class StagedFrameDump {
 public:
  StagedFrameDump() = default;
  ~StagedFrameDump();
  StagedFrameDump(const StagedFrameDump&) = delete;
  StagedFrameDump& operator=(const StagedFrameDump&) = delete;
  StagedFrameDump(StagedFrameDump&& other) noexcept;
  StagedFrameDump& operator=(StagedFrameDump&& other) noexcept;

  [[nodiscard]] bool active() const noexcept { return active_; }
  [[nodiscard]] const std::filesystem::path& temporary_path() const noexcept {
    return temporary_path_;
  }
  [[nodiscard]] const std::filesystem::path& final_path() const noexcept {
    return final_path_;
  }

 private:
  friend class FrameDumper;
  void discard() noexcept;

  FrameDumpMetadata metadata_;
  std::filesystem::path temporary_path_;
  std::filesystem::path final_path_;
  std::string file_name_;
  std::uint64_t hash_{};
  bool active_{};
};

[[nodiscard]] std::uint64_t fnv1a64(std::span<const std::uint8_t> bytes) noexcept;

class FrameDumper {
 public:
  explicit FrameDumper(std::filesystem::path directory)
      : directory_(std::move(directory)) {}

  [[nodiscard]] bool stage(const FrameDumpMetadata& metadata,
                           std::span<const std::uint32_t> xrgb_pixels,
                           StagedFrameDump& staged, std::string& error) const;
  [[nodiscard]] bool commit(StagedFrameDump& staged, FrameDumpResult& result,
                            std::string& error) const;
  void abort(StagedFrameDump& staged) const noexcept;

  [[nodiscard]] bool dump(const FrameDumpMetadata& metadata,
                          std::span<const std::uint32_t> xrgb_pixels,
                          FrameDumpResult& result, std::string& error) const;

 private:
  std::filesystem::path directory_;
};

}  // namespace glasswyrm::headless
