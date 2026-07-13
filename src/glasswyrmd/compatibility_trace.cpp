#include "glasswyrmd/compatibility_trace.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

namespace glasswyrm::server {
namespace {

std::string_view error_name(const std::uint8_t code) {
  switch (code) {
    case 1: return "BadRequest";
    case 2: return "BadValue";
    case 3: return "BadWindow";
    case 4: return "BadPixmap";
    case 5: return "BadAtom";
    case 7: return "BadFont";
    case 8: return "BadMatch";
    case 9: return "BadDrawable";
    case 10: return "BadAccess";
    case 11: return "BadAlloc";
    case 12: return "BadColor";
    case 13: return "BadGContext";
    case 14: return "BadIDChoice";
    case 15: return "BadName";
    case 16: return "BadLength";
    case 17: return "BadImplementation";
    default: return "UnknownError";
  }
}

}  // namespace

std::string_view x11_request_name(const std::uint8_t opcode) noexcept {
  switch (opcode) {
    case 1: return "CreateWindow";
    case 2: return "ChangeWindowAttributes";
    case 3: return "GetWindowAttributes";
    case 4: return "DestroyWindow";
    case 8: return "MapWindow";
    case 10: return "UnmapWindow";
    case 12: return "ConfigureWindow";
    case 14: return "GetGeometry";
    case 15: return "QueryTree";
    case 16: return "InternAtom";
    case 17: return "GetAtomName";
    case 18: return "ChangeProperty";
    case 19: return "DeleteProperty";
    case 20: return "GetProperty";
    case 21: return "ListProperties";
    case 43: return "GetInputFocus";
    case 53: return "CreatePixmap";
    case 54: return "FreePixmap";
    case 55: return "CreateGC";
    case 56: return "ChangeGC";
    case 60: return "FreeGC";
    case 61: return "ClearArea";
    case 62: return "CopyArea";
    case 70: return "PolyFillRectangle";
    case 72: return "PutImage";
    case 127: return "NoOperation";
    default: return "Unknown";
  }
}

std::unique_ptr<CompatibilityTrace> CompatibilityTrace::create(
    const std::string& path, std::string& error) {
  const int descriptor = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL |
                                                  O_CLOEXEC | O_NOFOLLOW,
                                S_IRUSR | S_IWUSR);
  if (descriptor < 0) {
    error = std::strerror(errno);
    return {};
  }
  struct stat status {};
  if (::fstat(descriptor, &status) != 0 || !S_ISREG(status.st_mode) ||
      status.st_nlink != 1) {
    error = "trace target is not a single-link regular file";
    ::close(descriptor);
    (void)::unlink(path.c_str());
    return {};
  }
  return std::unique_ptr<CompatibilityTrace>(
      new CompatibilityTrace(descriptor));
}

CompatibilityTrace::~CompatibilityTrace() {
  if (descriptor_ >= 0) ::close(descriptor_);
}

bool CompatibilityTrace::append(const std::string_view line) {
  if (descriptor_ < 0) return false;
  if (line.size() > kMaximumBytes - bytes_) {
    std::fprintf(stderr, "glasswyrmd: X11 trace size limit reached; tracing disabled\n");
    ::close(descriptor_);
    descriptor_ = -1;
    return false;
  }
  std::size_t offset = 0;
  while (offset < line.size()) {
    const auto count = ::write(descriptor_, line.data() + offset,
                               line.size() - offset);
    if (count < 0 && errno == EINTR) continue;
    if (count <= 0) {
      std::fprintf(stderr, "glasswyrmd: X11 trace write failed: %s; tracing disabled\n",
                   std::strerror(errno));
      ::close(descriptor_);
      descriptor_ = -1;
      return false;
    }
    offset += static_cast<std::size_t>(count);
    bytes_ += static_cast<std::size_t>(count);
  }
  return true;
}

void CompatibilityTrace::connection(const std::uint64_t client,
                                    const std::string_view outcome) {
  std::ostringstream line;
  line << "{\"direction\":\"connection\",\"client\":" << client
       << ",\"outcome\":\"" << outcome << "\"}\n";
  (void)append(line.str());
}

void CompatibilityTrace::request(const std::uint64_t client,
                                 const std::uint64_t sequence,
                                 const std::uint8_t opcode,
                                 const std::size_t length,
                                 const std::vector<std::uint8_t>& output) {
  const bool is_error = !output.empty() && output[0] == 0;
  std::ostringstream line;
  line << "{\"direction\":\"request\",\"client\":" << client
       << ",\"sequence\":" << sequence << ",\"opcode\":"
       << static_cast<unsigned int>(opcode) << ",\"name\":\""
       << x11_request_name(opcode) << "\",\"length\":" << length
       << ",\"outcome\":\"" << (is_error ? "error" : "success")
       << "\",\"error\":";
  if (is_error && output.size() > 1)
    line << "\"" << error_name(output[1]) << "\"";
  else
    line << "null";
  line << "}\n";
  (void)append(line.str());
}

void CompatibilityTrace::packet(const std::uint64_t client,
                                const std::uint64_t sequence,
                                const std::vector<std::uint8_t>& bytes) {
  if (bytes.empty()) return;
  const char* direction = bytes[0] == 0 ? "error" :
                          bytes[0] == 1 ? "reply" : "event";
  std::ostringstream line;
  line << "{\"direction\":\"" << direction << "\",\"client\":" << client
       << ",\"sequence\":" << sequence;
  if (bytes[0] == 0 && bytes.size() > 1)
    line << ",\"error\":\"" << error_name(bytes[1]) << "\"";
  if (bytes[0] > 1)
    line << ",\"event_type\":" << static_cast<unsigned int>(bytes[0] & 0x7fU);
  line << "}\n";
  (void)append(line.str());
}

}  // namespace glasswyrm::server
