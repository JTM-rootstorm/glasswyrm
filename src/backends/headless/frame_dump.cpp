#include "backends/headless/frame_dump.hpp"

#include "output/model/scale.hpp"
#include "output/model/transform.hpp"

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

const char* transform_name(const output::OutputTransform transform) noexcept {
  switch (transform) {
    case output::OutputTransform::Normal: return "normal";
    case output::OutputTransform::Rotate90: return "rotate-90";
    case output::OutputTransform::Rotate180: return "rotate-180";
    case output::OutputTransform::Rotate270: return "rotate-270";
    case output::OutputTransform::Flipped: return "flipped";
    case output::OutputTransform::Flipped90: return "flipped-90";
    case output::OutputTransform::Flipped180: return "flipped-180";
    case output::OutputTransform::Flipped270: return "flipped-270";
  }
  return "invalid";
}

bool valid_frame_set(const output::SoftwareFrameSetView& frames,
                     const std::span<const StagedFrameDump> staged,
                     std::string& error) {
  if (!frames.valid() || frames.outputs->size() != staged.size() ||
      staged.empty() ||
      staged.size() > output::SoftwareFrameSet::kMaximumOutputs ||
      !frames.outputs->contains(frames.primary_output_id) ||
      frames.aggregate_hash != output::calculate_frame_set_aggregate_hash(
                                   *frames.outputs, frames.layout_generation,
                                   frames.primary_output_id)) {
    error = "headless frame-set manifest metadata is inconsistent";
    return false;
  }
  std::size_t index = 0;
  for (const auto& [output_id, frame] : *frames.outputs) {
    const auto& artifact = staged[index++];
    const auto& metadata = artifact.metadata();
    if (!artifact.active() || metadata.frame != frames.ordinal ||
        metadata.commit_id != frames.commit_id ||
        metadata.generation != frames.generation ||
        metadata.output_id != output_id ||
        metadata.width != frame.output.width ||
        metadata.height != frame.output.height ||
        metadata.damage_rectangles != frame.damage.size() ||
        artifact.fnv1a64() != frame.visible_hash ||
        frame.frame.visible_hash() != frame.visible_hash ||
        frame.logical.x < 0 || frame.logical.y < 0 ||
        frame.logical.width == 0 || frame.logical.height == 0 ||
        !output::valid_output_scale(frame.scale) ||
        !output::valid_output_transform(frame.transform)) {
      error = "headless frame-set output metadata is inconsistent";
      return false;
    }
  }
  return true;
}

std::string frame_set_manifest_line(
    const output::SoftwareFrameSetView& frames,
    const std::span<const StagedFrameDump> staged) {
  std::ostringstream line;
  line << "{\"schema_version\":13,\"transaction_ordinal\":"
       << frames.ordinal << ",\"commit_id\":" << frames.commit_id
       << ",\"generation\":" << frames.generation
       << ",\"layout_generation\":" << frames.layout_generation
       << ",\"primary_output_id\":\"" << hex64(frames.primary_output_id)
       << "\",\"output_count\":" << staged.size()
       << ",\"aggregate_hash\":\"" << hex64(frames.aggregate_hash)
       << "\",\"outputs\":[";
  std::size_t index = 0;
  for (const auto& [output_id, frame] : *frames.outputs) {
    if (index != 0) line << ',';
    const auto& artifact = staged[index++];
    line << "{\"output_id\":\"" << hex64(output_id) << "\",\"file\":\""
         << artifact.final_path().filename().string() << "\",\"fnv1a64\":\""
         << hex64(artifact.fnv1a64()) << "\",\"physical\":{\"width\":"
         << frame.output.width << ",\"height\":" << frame.output.height
         << "},\"logical\":{\"x\":" << frame.logical.x << ",\"y\":"
         << frame.logical.y << ",\"width\":" << frame.logical.width
         << ",\"height\":" << frame.logical.height
         << "},\"scale\":{\"numerator\":" << frame.scale.numerator
         << ",\"denominator\":" << frame.scale.denominator
         << "},\"transform\":\"" << transform_name(frame.transform)
         << "\",\"damage\":[";
    for (std::size_t damage = 0; damage < frame.damage.size(); ++damage) {
      if (damage != 0) line << ',';
      const auto rectangle = frame.damage[damage];
      line << "{\"x\":" << rectangle.x << ",\"y\":" << rectangle.y
           << ",\"width\":" << rectangle.width << ",\"height\":"
           << rectangle.height << '}';
    }
    line << "]}";
  }
  line << "]}\n";
  return line.str();
}

bool read_text(const std::filesystem::path& path, std::string& contents,
               std::string& error) {
  contents.clear();
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    if (!std::filesystem::exists(path)) return true;
    error = "frame-set manifest read failed";
    return false;
  }
  contents.assign(std::istreambuf_iterator<char>(input), {});
  if (!input.bad()) return true;
  error = "frame-set manifest read failed";
  return false;
}

bool stage_text(const std::filesystem::path& final_path,
                const std::string_view contents,
                std::filesystem::path& temporary_path, std::string& error) {
  temporary_path = final_path.parent_path() /
      ("." + final_path.filename().string() + ".tmp." +
       std::to_string(static_cast<long long>(::getpid())));
  const int fd = ::open(temporary_path.c_str(),
                        O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
  if (fd < 0) {
    error = "cannot stage frame-set manifest: " +
            std::string(std::strerror(errno));
    return false;
  }
  bool success = write_all(
      fd, std::span(reinterpret_cast<const std::uint8_t*>(contents.data()),
                    contents.size()), error);
  if (success && ::fsync(fd) != 0) {
    error = "frame-set manifest fsync failed: " +
            std::string(std::strerror(errno));
    success = false;
  }
  if (::close(fd) != 0 && success) {
    error = "frame-set manifest close failed: " +
            std::string(std::strerror(errno));
    success = false;
  }
  if (!success) {
    std::error_code ignored;
    std::filesystem::remove(temporary_path, ignored);
  }
  return success;
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
                             const output::SoftwareFrameSetView &frames,
                             std::vector<FrameDumpResult> &results,
                             std::string &error,
                             const bool record_frame_set) const {
  results.clear();
  if (!valid_frame_set(frames, staged, error)) return false;

  const auto frame_manifest_path = directory_ / "frames.jsonl";
  const auto set_manifest_path = directory_ / "frame-sets.jsonl";
  std::string frame_manifest;
  std::string set_manifest;
  if (!read_text(frame_manifest_path, frame_manifest, error) ||
      (record_frame_set &&
       !read_text(set_manifest_path, set_manifest, error)))
    return false;
  for (const auto &frame : staged)
    frame_manifest +=
        manifest_line(frame.metadata_, frame.hash_, frame.file_name_);
  if (record_frame_set)
    set_manifest += frame_set_manifest_line(frames, staged);

  std::filesystem::path temporary_frame_manifest;
  std::filesystem::path temporary_set_manifest;
  if (!stage_text(frame_manifest_path, frame_manifest,
                  temporary_frame_manifest, error) ||
      (record_frame_set &&
       !stage_text(set_manifest_path, set_manifest,
                   temporary_set_manifest, error))) {
    std::error_code ignored;
    std::filesystem::remove(temporary_frame_manifest, ignored);
    return false;
  }

  struct Publication {
    std::filesystem::path temporary;
    std::filesystem::path final;
    std::filesystem::path backup;
    bool backed_up{};
  };
  std::vector<Publication> publications;
  publications.reserve(staged.size() + 2U);
  const auto backup_suffix =
      ".previous." + std::to_string(static_cast<long long>(::getpid()));
  for (const auto &frame : staged)
    publications.push_back(
        {frame.temporary_path_, frame.final_path_,
         directory_ / ("." + frame.file_name_ + backup_suffix)});
  publications.push_back(
      {temporary_frame_manifest, frame_manifest_path,
       directory_ / (".frames.jsonl" + backup_suffix)});
  if (record_frame_set)
    publications.push_back(
        {temporary_set_manifest, set_manifest_path,
         directory_ / (".frame-sets.jsonl" + backup_suffix)});
  for (const auto &publication : publications) {
    if (std::filesystem::exists(publication.backup)) {
      error = "frame dump set cannot stage its publication rollback";
      std::error_code ignored;
      std::filesystem::remove(temporary_frame_manifest, ignored);
      std::filesystem::remove(temporary_set_manifest, ignored);
      return false;
    }
  }

  std::size_t published = 0;
  for (; published < publications.size(); ++published) {
    auto &publication = publications[published];
    if (std::filesystem::exists(publication.final)) {
      if (::rename(publication.final.c_str(), publication.backup.c_str()) != 0) {
        error = "frame-set publication backup failed: " +
                std::string(std::strerror(errno));
        break;
      }
      publication.backed_up = true;
    }
    if (::rename(publication.temporary.c_str(), publication.final.c_str()) !=
        0) {
      error = "frame-set publication failed: " +
              std::string(std::strerror(errno));
      if (publication.backed_up) {
        (void)::rename(publication.backup.c_str(), publication.final.c_str());
        publication.backed_up = false;
      }
      break;
    }
  }
  if (published == publications.size()) {
    std::error_code ignored;
    for (const auto &publication : publications)
      if (publication.backed_up)
        std::filesystem::remove(publication.backup, ignored);
    results.reserve(staged.size());
    for (auto &frame : staged) {
      results.push_back({frame.final_path_, frame.hash_});
      frame.active_ = false;
    }
    error.clear();
    return true;
  }

  bool rollback_failed = false;
  std::error_code ignored;
  for (std::size_t index = published; index > 0; --index) {
    auto &publication = publications[index - 1U];
    std::filesystem::remove(publication.final, ignored);
    if (publication.backed_up &&
        ::rename(publication.backup.c_str(), publication.final.c_str()) != 0)
      rollback_failed = true;
  }
  for (const auto &publication : publications)
    std::filesystem::remove(publication.temporary, ignored);
  if (rollback_failed) error += "; publication rollback failed";
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
