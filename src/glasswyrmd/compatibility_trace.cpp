#include "glasswyrmd/compatibility_trace.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <optional>
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

std::uint16_t read_u16(const std::span<const std::uint8_t> bytes,
                       const std::size_t offset,
                       const gw::protocol::x11::ByteOrder order) {
  if (order == gw::protocol::x11::ByteOrder::LittleEndian)
    return static_cast<std::uint16_t>(bytes[offset]) |
           static_cast<std::uint16_t>(bytes[offset + 1]) << 8U;
  return static_cast<std::uint16_t>(bytes[offset]) << 8U |
         static_cast<std::uint16_t>(bytes[offset + 1]);
}

std::uint32_t read_u32(const std::span<const std::uint8_t> bytes,
                       const std::size_t offset,
                       const gw::protocol::x11::ByteOrder order) {
  if (order == gw::protocol::x11::ByteOrder::LittleEndian)
    return static_cast<std::uint32_t>(bytes[offset]) |
           static_cast<std::uint32_t>(bytes[offset + 1]) << 8U |
           static_cast<std::uint32_t>(bytes[offset + 2]) << 16U |
           static_cast<std::uint32_t>(bytes[offset + 3]) << 24U;
  return static_cast<std::uint32_t>(bytes[offset]) << 24U |
         static_cast<std::uint32_t>(bytes[offset + 1]) << 16U |
         static_cast<std::uint32_t>(bytes[offset + 2]) << 8U |
         static_cast<std::uint32_t>(bytes[offset + 3]);
}

std::optional<std::string_view> extension_name(
    const std::span<const std::uint8_t> bytes,
    const gw::protocol::x11::ByteOrder order) {
  if (bytes.size() < 8) return std::nullopt;
  const auto length = read_u16(bytes, 4, order);
  if (length > 32 || 8U + length > bytes.size()) return std::string_view{"OTHER"};
  const std::string_view candidate(
      reinterpret_cast<const char*>(bytes.data() + 8), length);
  constexpr std::string_view known[] = {
      "SHAPE",          "BIG-REQUESTS", "MIT-SHM", "XInputExtension",
      "XFIXES",         "DAMAGE",       "RENDER",  "Composite",
      "RANDR",          "Present"};
  for (const auto name : known)
    if (candidate == name) return name;
  return std::string_view{"OTHER"};
}

std::optional<std::string_view> dispatched_extension_name(
    const std::uint8_t opcode) {
  switch (opcode) {
    case 128: return "BIG-REQUESTS";
    case 129: return "MIT-SHM";
    case 130: return "XFIXES";
    case 131: return "DAMAGE";
    case 132: return "RENDER";
    case 133: return "Composite";
    case 134: return "RANDR";
    default: return std::nullopt;
  }
}

std::size_t event_window_offset(const std::uint8_t type) {
  if (type >= 2 && type <= 8) return 12;
  return 4;
}

}  // namespace

std::string_view x11_request_name(const std::uint8_t opcode) noexcept {
  switch (opcode) {
    case 1: return "CreateWindow";
    case 2: return "ChangeWindowAttributes";
    case 3: return "GetWindowAttributes";
    case 4: return "DestroyWindow";
    case 8: return "MapWindow";
    case 9: return "MapSubwindows";
    case 10: return "UnmapWindow";
    case 11: return "UnmapSubwindows";
    case 12: return "ConfigureWindow";
    case 14: return "GetGeometry";
    case 15: return "QueryTree";
    case 16: return "InternAtom";
    case 17: return "GetAtomName";
    case 18: return "ChangeProperty";
    case 19: return "DeleteProperty";
    case 20: return "GetProperty";
    case 21: return "ListProperties";
    case 22: return "SetSelectionOwner";
    case 23: return "GetSelectionOwner";
    case 24: return "ConvertSelection";
    case 25: return "SendEvent";
    case 26: return "GrabPointer";
    case 27: return "UngrabPointer";
    case 28: return "GrabButton";
    case 29: return "UngrabButton";
    case 30: return "ChangeActivePointerGrab";
    case 31: return "GrabKeyboard";
    case 32: return "UngrabKeyboard";
    case 33: return "GrabKey";
    case 34: return "UngrabKey";
    case 35: return "AllowEvents";
    case 38: return "QueryPointer";
    case 40: return "TranslateCoordinates";
    case 41: return "WarpPointer";
    case 43: return "GetInputFocus";
    case 44: return "QueryKeymap";
    case 45: return "OpenFont";
    case 46: return "CloseFont";
    case 47: return "QueryFont";
    case 48: return "QueryTextExtents";
    case 49: return "ListFonts";
    case 53: return "CreatePixmap";
    case 54: return "FreePixmap";
    case 55: return "CreateGC";
    case 56: return "ChangeGC";
    case 60: return "FreeGC";
    case 61: return "ClearArea";
    case 62: return "CopyArea";
    case 65: return "PolyLine";
    case 66: return "PolySegment";
    case 69: return "FillPoly";
    case 70: return "PolyFillRectangle";
    case 71: return "PolyFillArc";
    case 72: return "PutImage";
    case 74: return "PolyText8";
    case 76: return "ImageText8";
    case 78: return "CreateColormap";
    case 79: return "FreeColormap";
    case 81: return "InstallColormap";
    case 82: return "UninstallColormap";
    case 83: return "ListInstalledColormaps";
    case 84: return "AllocColor";
    case 85: return "AllocNamedColor";
    case 88: return "FreeColors";
    case 91: return "QueryColors";
    case 92: return "LookupColor";
    case 93: return "CreateCursor";
    case 94: return "CreateGlyphCursor";
    case 95: return "FreeCursor";
    case 96: return "RecolorCursor";
    case 97: return "QueryBestSize";
    case 98: return "QueryExtension";
    case 99: return "ListExtensions";
    case 101: return "GetKeyboardMapping";
    case 102: return "ChangeKeyboardControl";
    case 103: return "GetKeyboardControl";
    case 104: return "Bell";
    case 115: return "ForceScreenSaver";
    case 117: return "GetPointerMapping";
    case 119: return "GetModifierMapping";
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
                                 const std::vector<std::uint8_t>& output,
                                 const std::span<const std::uint8_t> request_bytes,
                                 const gw::protocol::x11::ByteOrder byte_order) {
  const bool is_error = !output.empty() && output[0] == 0;
  std::ostringstream line;
  line << "{\"direction\":\"request\",\"client\":" << client
       << ",\"sequence\":" << sequence << ",\"opcode\":"
       << static_cast<unsigned int>(opcode) << ",\"name\":\""
       << x11_request_name(opcode) << "\"";
  if (opcode == 98) {
    const auto extension = extension_name(request_bytes, byte_order);
    line << ",\"extension\":\"" << extension.value_or("OTHER") << "\"";
  } else if (const auto extension = dispatched_extension_name(opcode);
             extension) {
    line << ",\"extension\":\"" << *extension << "\"";
    if (request_bytes.size() >= 2)
      line << ",\"minor\":" << static_cast<unsigned int>(request_bytes[1]);
  }
  line << ",\"length\":" << length
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
                                const std::vector<std::uint8_t>& bytes,
                                const gw::protocol::x11::ByteOrder byte_order) {
  if (bytes.empty()) return;
  const char* direction = bytes[0] == 0 ? "error" :
                          bytes[0] == 1 ? "reply" : "event";
  std::ostringstream line;
  const auto type = static_cast<std::uint8_t>(bytes[0] & 0x7fU);
  const auto record_sequence = bytes[0] > 1 && bytes.size() >= 4
                                   ? read_u16(bytes, 2, byte_order)
                                   : sequence;
  line << "{\"direction\":\"" << direction << "\",\"client\":" << client
       << ",\"sequence\":" << record_sequence;
  if (bytes[0] == 0 && bytes.size() > 1)
    line << ",\"error\":\"" << error_name(bytes[1]) << "\"";
  if (bytes[0] > 1) {
    line << ",\"event_type\":" << static_cast<unsigned int>(type);
    const auto offset = event_window_offset(type);
    if (bytes.size() >= offset + 4)
      line << ",\"window\":" << read_u32(bytes, offset, byte_order);
  }
  line << "}\n";
  (void)append(line.str());
}

}  // namespace glasswyrm::server
