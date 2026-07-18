#include "backends/headless/frame_dump.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string_view>
#include <system_error>
#include <unistd.h>
#include <utility>
#include <vector>

namespace glasswyrm::headless {
namespace {

bool write_all(const int fd, std::span<const std::uint8_t> bytes,
               std::string& error) {
  while (!bytes.empty()) {
    const auto count = ::write(fd, bytes.data(), bytes.size());
    if (count < 0) {
      if (errno == EINTR) continue;
      error = std::string("frame dump write failed: ") + std::strerror(errno);
      return false;
    }
    bytes = bytes.subspan(static_cast<std::size_t>(count));
  }
  return true;
}

std::string hex64(const std::uint64_t value) {
  std::ostringstream stream;
  stream << std::hex << std::setfill('0') << std::setw(16) << value;
  return stream.str();
}

std::string frame_name(const FrameDumpMetadata& metadata) {
  std::ostringstream stream;
  stream << "frame-" << std::setfill('0') << std::setw(6) << metadata.frame
         << "-output-" << std::setw(16) << metadata.output_id << ".ppm";
  return stream.str();
}

std::string manifest_line(const FrameDumpMetadata &metadata,
                          const std::uint64_t hash,
                          const std::string_view file_name) {
  std::ostringstream manifest;
  manifest << "{\"frame\":" << metadata.frame << ",\"commit_id\":"
           << metadata.commit_id << ",\"generation\":" << metadata.generation
           << ",\"output_id\":" << metadata.output_id << ",\"width\":"
           << metadata.width << ",\"height\":" << metadata.height
           << ",\"damage_rectangles\":" << metadata.damage_rectangles
           << ",\"fnv1a64\":\"" << hex64(hash) << "\",\"file\":\""
           << file_name << "\"}\n";
  return manifest.str();
}

}  // namespace

StagedFrameDump::~StagedFrameDump() { discard(); }

StagedFrameDump::StagedFrameDump(StagedFrameDump&& other) noexcept
    : metadata_(other.metadata_),
      temporary_path_(std::move(other.temporary_path_)),
      final_path_(std::move(other.final_path_)),
      file_name_(std::move(other.file_name_)),
      hash_(other.hash_),
      active_(std::exchange(other.active_, false)) {}

StagedFrameDump& StagedFrameDump::operator=(StagedFrameDump&& other) noexcept {
  if (this == &other) return *this;
  discard();
  metadata_ = other.metadata_;
  temporary_path_ = std::move(other.temporary_path_);
  final_path_ = std::move(other.final_path_);
  file_name_ = std::move(other.file_name_);
  hash_ = other.hash_;
  active_ = std::exchange(other.active_, false);
  return *this;
}

void StagedFrameDump::discard() noexcept {
  if (!active_) return;
  std::error_code ignored;
  std::filesystem::remove(temporary_path_, ignored);
  active_ = false;
}

std::uint64_t fnv1a64(const std::span<const std::uint8_t> bytes) noexcept {
  std::uint64_t hash = 14695981039346656037ULL;
  for (const auto byte : bytes) {
    hash ^= byte;
    hash *= 1099511628211ULL;
  }
  return hash;
}

bool FrameDumper::stage(const FrameDumpMetadata& metadata,
                        const std::span<const std::uint32_t> xrgb_pixels,
                        StagedFrameDump& staged, std::string& error) const {
  const auto pixel_count =
      static_cast<std::uint64_t>(metadata.width) * metadata.height;
  if (metadata.frame == 0 || metadata.output_id == 0 || metadata.width == 0 ||
      metadata.height == 0 || pixel_count != xrgb_pixels.size() ||
      pixel_count > std::numeric_limits<std::size_t>::max() / 3) {
    error = "invalid frame dump metadata or framebuffer size";
    return false;
  }

  std::vector<std::uint8_t> rgb(static_cast<std::size_t>(pixel_count) * 3);
  for (std::size_t index = 0; index < xrgb_pixels.size(); ++index) {
    const auto pixel = xrgb_pixels[index];
    rgb[index * 3] = static_cast<std::uint8_t>(pixel >> 16);
    rgb[index * 3 + 1] = static_cast<std::uint8_t>(pixel >> 8);
    rgb[index * 3 + 2] = static_cast<std::uint8_t>(pixel);
  }

  std::error_code filesystem_error;
  std::filesystem::create_directories(directory_, filesystem_error);
  if (filesystem_error) {
    error = "cannot create frame dump directory: " + filesystem_error.message();
    return false;
  }

  const auto name = frame_name(metadata);
  const auto final_path = directory_ / name;
  const auto temporary_path = directory_ /
      ("." + name + ".tmp." +
       std::to_string(static_cast<long long>(::getpid())));
  const int fd = ::open(temporary_path.c_str(),
                        O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
  if (fd < 0) {
    error = std::string("cannot open frame dump temporary file: ") +
            std::strerror(errno);
    return false;
  }

  const std::string header = "P6\n" + std::to_string(metadata.width) + " " +
                             std::to_string(metadata.height) + "\n255\n";
  bool success = write_all(
                     fd,
                     std::span(reinterpret_cast<const std::uint8_t*>(
                                   header.data()),
                               header.size()),
                     error) &&
                 write_all(fd, rgb, error);
  if (success && ::fsync(fd) != 0) {
    error = std::string("frame dump fsync failed: ") + std::strerror(errno);
    success = false;
  }
  if (::close(fd) != 0 && success) {
    error = std::string("frame dump close failed: ") + std::strerror(errno);
    success = false;
  }
  if (!success) {
    std::filesystem::remove(temporary_path, filesystem_error);
    return false;
  }

  StagedFrameDump replacement;
  replacement.metadata_ = metadata;
  replacement.temporary_path_ = temporary_path;
  replacement.final_path_ = final_path;
  replacement.file_name_ = name;
  replacement.hash_ = fnv1a64(rgb);
  replacement.active_ = true;
  staged = std::move(replacement);
  error.clear();
  return true;
}

bool FrameDumper::commit(StagedFrameDump& staged, FrameDumpResult& result,
                         std::string& error) const {
  if (!staged.active_) {
    error = "frame dump is not staged";
    return false;
  }

  if (::rename(staged.temporary_path_.c_str(), staged.final_path_.c_str()) != 0) {
    error = std::string("frame dump rename failed: ") + std::strerror(errno);
    staged.discard();
    return false;
  }

  const auto manifest_path = directory_ / "frames.jsonl";
  std::ofstream manifest(manifest_path, std::ios::binary | std::ios::app);
  manifest << manifest_line(staged.metadata_, staged.hash_, staged.file_name_);
  manifest.flush();
  if (!manifest) {
    error = "frame manifest write failed";
    manifest.close();
    std::error_code ignored;
    std::filesystem::remove(staged.final_path_, ignored);
    staged.active_ = false;
    return false;
  }

  result = FrameDumpResult{staged.final_path_, staged.hash_};
  staged.active_ = false;
  error.clear();
  return true;
}

bool FrameDumper::commit_all(const std::span<StagedFrameDump> staged,
                             std::vector<FrameDumpResult> &results,
                             std::string &error) const {
  results.clear();
  if (staged.empty()) {
    error = "frame dump set is empty";
    return false;
  }
  std::vector<std::filesystem::path> backups;
  backups.reserve(staged.size());
  for (const auto &frame : staged) {
    backups.push_back(directory_ /
                      ("." + frame.file_name_ + ".previous." +
                       std::to_string(static_cast<long long>(::getpid()))));
    if (!frame.active_ || std::filesystem::exists(backups.back())) {
      error = "frame dump set cannot stage its publication rollback";
      return false;
    }
  }

  std::string manifest_contents;
  const auto manifest_path = directory_ / "frames.jsonl";
  {
    std::ifstream existing(manifest_path, std::ios::binary);
    if (existing) {
      manifest_contents.assign(std::istreambuf_iterator<char>(existing), {});
      if (existing.bad()) {
        error = "frame manifest read failed";
        return false;
      }
    }
  }
  for (const auto &frame : staged)
    manifest_contents +=
        manifest_line(frame.metadata_, frame.hash_, frame.file_name_);

  const auto temporary_manifest = directory_ /
      (".frames.jsonl.tmp." +
       std::to_string(static_cast<long long>(::getpid())));
  const int manifest_fd =
      ::open(temporary_manifest.c_str(),
             O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
  if (manifest_fd < 0) {
    error = std::string("cannot open frame-set manifest temporary file: ") +
            std::strerror(errno);
    return false;
  }
  const auto manifest_bytes = std::span(
      reinterpret_cast<const std::uint8_t *>(manifest_contents.data()),
      manifest_contents.size());
  bool manifest_staged = write_all(manifest_fd, manifest_bytes, error);
  if (manifest_staged && ::fsync(manifest_fd) != 0) {
    error = std::string("frame-set manifest fsync failed: ") +
            std::strerror(errno);
    manifest_staged = false;
  }
  if (::close(manifest_fd) != 0 && manifest_staged) {
    error = std::string("frame-set manifest close failed: ") +
            std::strerror(errno);
    manifest_staged = false;
  }
  if (!manifest_staged) {
    std::error_code ignored;
    std::filesystem::remove(temporary_manifest, ignored);
    return false;
  }

  std::size_t published = 0;
  std::vector<bool> backed_up(staged.size());
  for (; published < staged.size(); ++published) {
    auto &frame = staged[published];
    if (std::filesystem::exists(frame.final_path_)) {
      if (::rename(frame.final_path_.c_str(), backups[published].c_str()) != 0) {
        error = std::string("frame-set output backup failed: ") +
                std::strerror(errno);
        break;
      }
      backed_up[published] = true;
    }
    if (::rename(frame.temporary_path_.c_str(), frame.final_path_.c_str()) != 0) {
      error = std::string("frame-set output publish failed: ") +
              std::strerror(errno);
      if (backed_up[published]) {
        (void)::rename(backups[published].c_str(), frame.final_path_.c_str());
        backed_up[published] = false;
      }
      break;
    }
  }
  if (published == staged.size() &&
      ::rename(temporary_manifest.c_str(), manifest_path.c_str()) != 0) {
    error = std::string("frame-set manifest publish failed: ") +
            std::strerror(errno);
  } else if (published == staged.size()) {
    results.reserve(staged.size());
    for (std::size_t index = 0; index < staged.size(); ++index) {
      auto &frame = staged[index];
      if (backed_up[index]) {
        std::error_code ignored;
        std::filesystem::remove(backups[index], ignored);
      }
      results.push_back({frame.final_path_, frame.hash_});
      frame.active_ = false;
    }
    error.clear();
    return true;
  }

  std::error_code ignored;
  std::filesystem::remove(temporary_manifest, ignored);
  for (std::size_t index = 0; index < published; ++index) {
    std::filesystem::remove(staged[index].final_path_, ignored);
    if (backed_up[index])
      (void)::rename(backups[index].c_str(), staged[index].final_path_.c_str());
    staged[index].active_ = false;
  }
  return false;
}

void FrameDumper::abort(StagedFrameDump& staged) const noexcept {
  staged.discard();
}

bool FrameDumper::dump(const FrameDumpMetadata& metadata,
                       const std::span<const std::uint32_t> xrgb_pixels,
                       FrameDumpResult& result, std::string& error) const {
  StagedFrameDump staged;
  if (!stage(metadata, xrgb_pixels, staged, error)) return false;
  if (commit(staged, result, error)) return true;
  abort(staged);
  return false;
}

}  // namespace glasswyrm::headless
