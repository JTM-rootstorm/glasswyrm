#include "render/renderer_report.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

namespace gw::render {
namespace {

std::string json_quote(const std::string_view value) {
  std::ostringstream output;
  output << '"';
  for (const unsigned char character : value) {
    switch (character) {
      case '"': output << "\\\""; break;
      case '\\': output << "\\\\"; break;
      case '\b': output << "\\b"; break;
      case '\f': output << "\\f"; break;
      case '\n': output << "\\n"; break;
      case '\r': output << "\\r"; break;
      case '\t': output << "\\t"; break;
      default:
        if (character < 0x20U) {
          constexpr char digits[] = "0123456789abcdef";
          output << "\\u00" << digits[character >> 4U]
                 << digits[character & 0xfU];
        } else {
          output << static_cast<char>(character);
        }
    }
  }
  output << '"';
  return output.str();
}

const char* disposition_name(const RenderDisposition disposition) {
  switch (disposition) {
    case RenderDisposition::Complete: return "complete";
    case RenderDisposition::InvalidFrame: return "invalid-frame";
    case RenderDisposition::InvalidBuffer: return "invalid-buffer";
    case RenderDisposition::Fatal: return "fatal";
  }
  return "fatal";
}

bool same_identity(const struct stat& status, const std::uint64_t device,
                   const std::uint64_t inode) {
  return S_ISREG(status.st_mode) && status.st_nlink == 1 &&
         static_cast<std::uint64_t>(status.st_dev) == device &&
         static_cast<std::uint64_t>(status.st_ino) == inode;
}

} // namespace

RendererReport::~RendererReport() {
  if (descriptor_ >= 0) (void)::close(descriptor_);
}

bool RendererReport::initialize(const RendererSelectionReport& selection,
                                std::string& error) {
  if (descriptor_ >= 0) {
    error = "renderer report is already initialized";
    return false;
  }
  if (path_.empty() || path_.filename().empty() || path_.filename() == "." ||
      path_.filename() == "..") {
    error = "renderer report path must name a file";
    return false;
  }
  auto parent = path_.parent_path();
  if (parent.empty()) parent = ".";
  struct stat parent_status {};
  if (::lstat(parent.c_str(), &parent_status) != 0 ||
      !S_ISDIR(parent_status.st_mode) || S_ISLNK(parent_status.st_mode)) {
    error = "renderer report parent must be a real directory";
    return false;
  }
  descriptor_ = ::open(path_.c_str(),
                       O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW,
                       0600);
  if (descriptor_ < 0) {
    error = std::string("cannot create renderer report: ") +
            std::strerror(errno);
    return false;
  }
  struct stat status {};
  if (::fstat(descriptor_, &status) != 0 || !S_ISREG(status.st_mode) ||
      status.st_nlink != 1) {
    error = "renderer report is not a private regular file";
    (void)::close(descriptor_);
    descriptor_ = -1;
    (void)::unlink(path_.c_str());
    return false;
  }
  device_ = static_cast<std::uint64_t>(status.st_dev);
  inode_ = static_cast<std::uint64_t>(status.st_ino);

  std::ostringstream line;
  const auto optional = [](const std::optional<std::string>& value) {
    return value ? json_quote(*value) : std::string("null");
  };
  line << "{\"record\":\"selection\",\"requested\":"
       << json_quote(renderer_request_name(selection.requested))
       << ",\"selected\":" << json_quote(selection.selected)
       << ",\"egl_platform\":" << optional(selection.egl_platform)
       << ",\"egl_vendor\":" << optional(selection.egl_vendor)
       << ",\"egl_version\":" << optional(selection.egl_version)
       << ",\"gles_version\":" << optional(selection.gles_version)
       << ",\"gl_vendor\":" << optional(selection.gl_vendor)
       << ",\"gl_renderer\":" << optional(selection.gl_renderer)
       << ",\"gl_version\":" << optional(selection.gl_version)
       << ",\"gbm_device\":" << optional(selection.gbm_device)
       << ",\"render_node\":" << optional(selection.render_node)
       << ",\"software_renderer\":"
       << (selection.software_renderer ? "true" : "false")
       << ",\"fallback_reasons\":[";
  for (std::size_t index = 0; index < selection.fallback_reasons.size();
       ++index) {
    if (index != 0) line << ',';
    line << json_quote(selection.fallback_reasons[index]);
  }
  line << "]}\n";
  if (append(line.str(), error)) return true;
  (void)::close(descriptor_);
  descriptor_ = -1;
  (void)::unlink(path_.c_str());
  return false;
}

bool RendererReport::append_frame(const RenderFrameRequest& request,
                                  const RenderFrameResult& result,
                                  const std::string_view selected,
                                  std::string& error) {
  std::ostringstream line;
  line << "{\"record\":\"frame\",\"selected\":" << json_quote(selected)
       << ",\"commit_id\":" << request.commit_id
       << ",\"generation\":" << request.generation
       << ",\"ordinal\":" << request.ordinal
       << ",\"disposition\":"
       << json_quote(disposition_name(result.disposition))
       << ",\"texture_uploads\":" << result.metrics.texture_uploads
       << ",\"texture_upload_bytes\":"
       << result.metrics.texture_upload_bytes << ",\"damage_rectangles\":"
       << result.metrics.damage_rectangles << ",\"readback_bytes\":"
       << result.metrics.readback_bytes << ",\"texture_cache_bytes\":"
       << result.metrics.texture_cache_bytes << ",\"fallback_reason\":"
       << (result.fallback_reason.empty() ? "null"
                                          : json_quote(result.fallback_reason))
       << ",\"error\":"
       << (result.error.empty() ? "null" : json_quote(result.error)) << "}\n";
  return append(line.str(), error);
}

bool RendererReport::target_is_unchanged(std::string& error) const {
  struct stat descriptor_status {};
  struct stat path_status {};
  if (descriptor_ < 0 || ::fstat(descriptor_, &descriptor_status) != 0 ||
      ::lstat(path_.c_str(), &path_status) != 0 ||
      !same_identity(descriptor_status, device_, inode_) ||
      !same_identity(path_status, device_, inode_)) {
    error = "renderer report target was replaced";
    return false;
  }
  return true;
}

bool RendererReport::append(const std::string_view line, std::string& error) {
  if (!target_is_unchanged(error)) return false;
  std::size_t offset = 0;
  while (offset < line.size()) {
    const auto written =
        ::write(descriptor_, line.data() + offset, line.size() - offset);
    if (written < 0 && errno == EINTR) continue;
    if (written <= 0) {
      error = std::string("renderer report write failed: ") +
              std::strerror(errno);
      return false;
    }
    offset += static_cast<std::size_t>(written);
  }
  if (::fsync(descriptor_) != 0) {
    error = std::string("renderer report fsync failed: ") +
            std::strerror(errno);
    return false;
  }
  error.clear();
  return true;
}

} // namespace gw::render
