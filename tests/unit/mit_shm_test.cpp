#include "glasswyrmd/extension_registry.hpp"
#include "glasswyrmd/request_dispatcher.hpp"
#include "protocol/x11/byte_cursor.hpp"
#include "tests/helpers/test_support.hpp"

#include <sys/shm.h>
#include <unistd.h>

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace {

using namespace glasswyrm::server;
namespace x11 = gw::protocol::x11;
using gw::test::require;

class Segment {
 public:
  explicit Segment(const std::size_t size) {
    id = ::shmget(IPC_PRIVATE, size, IPC_CREAT | 0600);
    require(id >= 0, "create SysV fixture segment");
    address = ::shmat(id, nullptr, 0);
    require(address != reinterpret_cast<void*>(-1), "map SysV fixture segment");
  }
  ~Segment() {
    if (address != reinterpret_cast<void*>(-1)) (void)::shmdt(address);
    if (id >= 0) (void)::shmctl(id, IPC_RMID, nullptr);
  }
  Segment(const Segment&) = delete;
  Segment& operator=(const Segment&) = delete;
  int id{-1};
  void* address{reinterpret_cast<void*>(-1)};
};

x11::ByteWriter header(const x11::ByteOrder order, const std::uint8_t minor,
                       const std::uint16_t units) {
  x11::ByteWriter writer(order);
  writer.write_u8(129);
  writer.write_u8(minor);
  writer.write_u16(units);
  return writer;
}

x11::FramedRequest finish(x11::ByteWriter writer, const std::uint8_t minor) {
  x11::FramedRequest request;
  request.opcode = 129;
  request.data = minor;
  request.bytes = std::move(writer).take();
  request.length_units = static_cast<std::uint32_t>(request.bytes.size() / 4U);
  return request;
}

std::uint16_t u16(const std::span<const std::uint8_t> bytes,
                  const x11::ByteOrder order, const std::size_t offset) {
  x11::ByteReader reader(bytes.subspan(offset), order);
  std::uint16_t value{};
  require(reader.read_u16(value), "read u16");
  return value;
}

std::uint32_t u32(const std::span<const std::uint8_t> bytes,
                  const x11::ByteOrder order, const std::size_t offset) {
  x11::ByteReader reader(bytes.subspan(offset), order);
  std::uint32_t value{};
  require(reader.read_u32(value), "read u32");
  return value;
}

std::size_t attachment_count(const int shmid) {
  struct shmid_ds status {};
  require(::shmctl(shmid, IPC_STAT, &status) == 0, "inspect SysV fixture");
  return status.shm_nattch;
}

x11::FramedRequest attach_request(const x11::ByteOrder order,
                                  const std::uint32_t xid, const int shmid,
                                  const bool read_only) {
  auto writer = header(order, 1, 4);
  writer.write_u32(xid);
  writer.write_u32(static_cast<std::uint32_t>(shmid));
  writer.write_u8(read_only ? 1 : 0);
  writer.write_padding(3);
  return finish(std::move(writer), 1);
}

x11::FramedRequest detach_request(const x11::ByteOrder order,
                                  const std::uint32_t xid) {
  auto writer = header(order, 2, 2);
  writer.write_u32(xid);
  return finish(std::move(writer), 2);
}

x11::FramedRequest put_request(
    const x11::ByteOrder order, const std::uint32_t drawable,
    const std::uint32_t gc, const std::uint32_t shmseg,
    const bool send_event, const std::uint32_t offset = 0) {
  auto writer = header(order, 3, 10);
  writer.write_u32(drawable);
  writer.write_u32(gc);
  writer.write_u16(4);
  writer.write_u16(3);
  writer.write_u16(1);
  writer.write_u16(1);
  writer.write_u16(2);
  writer.write_u16(2);
  writer.write_u16(0);
  writer.write_u16(0);
  writer.write_u8(24);
  writer.write_u8(2);
  writer.write_u8(send_event ? 1 : 0);
  writer.write_u8(0);
  writer.write_u32(shmseg);
  writer.write_u32(offset);
  return finish(std::move(writer), 3);
}

x11::FramedRequest get_request(const x11::ByteOrder order,
                               const std::uint32_t drawable,
                               const std::uint32_t shmseg,
                               const std::uint32_t offset) {
  auto writer = header(order, 4, 8);
  writer.write_u32(drawable);
  writer.write_u16(0);
  writer.write_u16(0);
  writer.write_u16(2);
  writer.write_u16(2);
  writer.write_u32(0x00ffffff);
  writer.write_u8(2);
  writer.write_padding(3);
  writer.write_u32(shmseg);
  writer.write_u32(offset);
  return finish(std::move(writer), 4);
}

void test_query_and_security(const x11::ByteOrder order) {
  const ExtensionRegistry extensions(true, {});
  ServerState state;
  DispatchContext context{1, 0x400000, 0x1fffff, 0x12345, order, false, {},
                          &extensions, static_cast<std::uint32_t>(::getuid())};
  auto result = dispatch_request(
      state, context, finish(header(order, 0, 1), 0));
  require(result.output.size() == 32 && result.output[1] == 0 &&
              u16(result.output, order, 8) == 1 &&
              u16(result.output, order, 10) == 1 && result.output[16] == 2,
          "MIT-SHM 1.1 reports no shared pixmaps and ZPixmap");

  Segment segment(4096);
  const auto baseline = attachment_count(segment.id);
  context.peer_uid = static_cast<std::uint32_t>(::getuid()) + 1U;
  result = dispatch_request(
      state, context, attach_request(order, 0x400010, segment.id, true));
  require(result.output[1] ==
              static_cast<std::uint8_t>(x11::CoreErrorCode::BadAccess) &&
              attachment_count(segment.id) == baseline,
          "Attach rejects a segment without peer UID ownership evidence");
  context.peer_uid = static_cast<std::uint32_t>(::getuid());
  result = dispatch_request(
      state, context, attach_request(order, 0x400010, segment.id, true));
  require(result.output.empty() && attachment_count(segment.id) == baseline + 1,
          "Attach maps a same-UID segment exactly once");
  result = dispatch_request(state, context,
                            detach_request(order, 0x400010));
  require(result.output.empty() && attachment_count(segment.id) == baseline,
          "Detach unmaps a segment exactly once");
  result = dispatch_request(state, context,
                            detach_request(order, 0x400010));
  require(result.output[1] == 128 && u16(result.output, order, 8) == 2 &&
              result.output[10] == 129,
          "unknown segment reports MIT-SHM BadSeg with request metadata");
}

void test_images_and_cleanup(const x11::ByteOrder order) {
  const ExtensionRegistry extensions(true, {});
  ServerState state;
  DispatchContext context{7, 0x400000, 0x1fffff, 33, order, false, {},
                          &extensions, static_cast<std::uint32_t>(::getuid())};
  Segment segment(4096);
  auto* words = static_cast<std::uint32_t*>(segment.address);
  for (std::uint32_t index = 0; index < 12; ++index)
    words[index] = 0x00010203U + index * 0x00010101U;

  constexpr std::uint32_t read_only_segment = 0x400020;
  constexpr std::uint32_t writable_segment = 0x400021;
  auto result = dispatch_request(
      state, context,
      attach_request(order, read_only_segment, segment.id, true));
  require(result.output.empty(), "attach read-only image segment");
  result = dispatch_request(
      state, context,
      attach_request(order, writable_segment, segment.id, false));
  require(result.output.empty(), "attach writable image segment");

  WindowCreateSpec window;
  window.xid = 0x400030;
  window.parent = state.screen().root_window;
  window.width = 2;
  window.height = 2;
  window.depth = 24;
  window.window_class = WindowClass::InputOutput;
  window.visual = state.screen().root_visual;
  window.attributes.colormap = state.screen().default_colormap;
  require(state.resources().create_window(
              context.client_id, context.resource_base, context.resource_mask,
              window) == CreateWindowStatus::Success,
          "create SHM destination window");
  constexpr std::uint32_t gc = 0x400031;
  require(state.resources().create_gc(
              context.client_id, context.resource_base, context.resource_mask,
              gc, window.xid, {}) == CreateGcStatus::Success,
          "create SHM destination GC");

  result = dispatch_request(
      state, context,
      put_request(order, window.xid, gc, read_only_segment, true));
  const auto* pixels = state.resources().find_window(window.xid)->storage.get();
  require(result.output.size() == 32,
          "SHM PutImage emits one exact-size Completion");
  require(result.output[0] == 64,
          "SHM PutImage emits the MIT-SHM Completion event code");
  require(u16(result.output, order, 2) == 33 &&
              u32(result.output, order, 4) == window.xid &&
              u16(result.output, order, 8) == 3 && result.output[10] == 129 &&
              u32(result.output, order, 12) == read_only_segment,
          "SHM Completion carries request metadata");
  require(pixels->at(0, 0) == (words[5] | 0xff000000U) &&
              pixels->at(1, 1) == (words[10] | 0xff000000U),
          "SHM PutImage crops source pixels");
  require(result.drawable_damage.size() == 1,
          "SHM PutImage reports drawable damage");

  result = dispatch_request(
      state, context, get_request(order, window.xid, read_only_segment, 128));
  require(result.output[1] ==
              static_cast<std::uint8_t>(x11::CoreErrorCode::BadAccess),
          "SHM GetImage rejects read-only segment mappings");
  result = dispatch_request(
      state, context, get_request(order, window.xid, writable_segment, 128));
  auto* bytes = static_cast<std::uint8_t*>(segment.address);
  require(result.output.size() == 32 && result.output[1] == 24 &&
              u32(result.output, order, 8) == state.screen().root_visual &&
              u32(result.output, order, 12) == 16 && bytes[128] == 0x08 &&
              bytes[129] == 0x07 && bytes[130] == 0x06 && bytes[131] == 0,
          "SHM GetImage returns XRGB bytes and exact size");

  result = dispatch_request(
      state, context,
      put_request(order, window.xid, gc, read_only_segment, false, 4090));
  require(result.output[1] ==
              static_cast<std::uint8_t>(x11::CoreErrorCode::BadValue),
          "SHM PutImage rejects an out-of-bounds source image");
  const auto before_cleanup = attachment_count(segment.id);
  const auto cleanup = state.cleanup_client(context.client_id);
  require(cleanup.resources_destroyed >= 4 &&
              attachment_count(segment.id) + 2 == before_cleanup &&
              state.resources().resource_count(ResourceType::ShmSegment) == 0 &&
              state.invariants_hold(),
          "client cleanup detaches every SHM mapping before XID reuse");
}

}  // namespace

int main() {
  for (const auto order : {x11::ByteOrder::LittleEndian,
                           x11::ByteOrder::BigEndian}) {
    test_query_and_security(order);
    test_images_and_cleanup(order);
  }
  return 0;
}
