#include "backends/headless/frame_dump.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <system_error>
#include <unistd.h>
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

}  // namespace

std::uint64_t fnv1a64(const std::span<const std::uint8_t> bytes) noexcept {
  std::uint64_t hash = 14695981039346656037ULL;
  for (const auto byte : bytes) {
    hash ^= byte;
    hash *= 1099511628211ULL;
  }
  return hash;
}

bool FrameDumper::dump(const FrameDumpMetadata& metadata,
                       const std::span<const std::uint32_t> xrgb_pixels,
                       FrameDumpResult& result, std::string& error) const {
  const auto pixel_count = static_cast<std::uint64_t>(metadata.width) * metadata.height;
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
      ("." + name + ".tmp." + std::to_string(static_cast<long long>(::getpid())));
  const int fd = ::open(temporary_path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
  if (fd < 0) {
    error = std::string("cannot open frame dump temporary file: ") + std::strerror(errno);
    return false;
  }

  const std::string header = "P6\n" + std::to_string(metadata.width) + " " +
                             std::to_string(metadata.height) + "\n255\n";
  bool success = write_all(fd, std::span(reinterpret_cast<const std::uint8_t*>(header.data()),
                                         header.size()), error) &&
                 write_all(fd, rgb, error);
  if (success && ::fsync(fd) != 0) {
    error = std::string("frame dump fsync failed: ") + std::strerror(errno);
    success = false;
  }
  if (::close(fd) != 0 && success) {
    error = std::string("frame dump close failed: ") + std::strerror(errno);
    success = false;
  }
  if (!success || ::rename(temporary_path.c_str(), final_path.c_str()) != 0) {
    if (success) error = std::string("frame dump rename failed: ") + std::strerror(errno);
    std::filesystem::remove(temporary_path, filesystem_error);
    return false;
  }

  const auto hash = fnv1a64(rgb);
  const auto manifest_path = directory_ / "frames.jsonl";
  std::ofstream manifest(manifest_path, std::ios::binary | std::ios::app);
  manifest << "{\"frame\":" << metadata.frame << ",\"commit_id\":" << metadata.commit_id
           << ",\"generation\":" << metadata.generation << ",\"output_id\":"
           << metadata.output_id << ",\"width\":" << metadata.width << ",\"height\":"
           << metadata.height << ",\"damage_rectangles\":" << metadata.damage_rectangles
           << ",\"fnv1a64\":\"" << hex64(hash) << "\",\"file\":\"" << name << "\"}\n";
  manifest.flush();
  if (!manifest) {
    error = "frame manifest write failed";
    manifest.close();
    std::filesystem::remove(final_path, filesystem_error);
    return false;
  }

  result = FrameDumpResult{final_path, hash};
  error.clear();
  return true;
}

}  // namespace glasswyrm::headless
