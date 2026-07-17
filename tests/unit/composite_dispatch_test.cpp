#include "glasswyrmd/extension_registry.hpp"
#include "glasswyrmd/request_dispatcher.hpp"
#include "protocol/x11/byte_cursor.hpp"
#include "tests/helpers/test_support.hpp"

#include <cstdint>

namespace {

using namespace glasswyrm::server;
namespace x11 = gw::protocol::x11;
using gw::test::require;

x11::ByteWriter header(const x11::ByteOrder order, const std::uint8_t opcode,
                       const std::uint8_t data, const std::uint16_t units) {
  x11::ByteWriter writer(order);
  writer.write_u8(opcode);
  writer.write_u8(data);
  writer.write_u16(units);
  return writer;
}

x11::FramedRequest finish(x11::ByteWriter writer, const std::uint8_t opcode,
                          const std::uint8_t data) {
  x11::FramedRequest request;
  request.opcode = opcode;
  request.data = data;
  request.bytes = std::move(writer).take();
  request.length_units =
      static_cast<std::uint32_t>(request.bytes.size() / 4U);
  return request;
}

std::uint32_t u32(const std::span<const std::uint8_t> bytes,
                  const x11::ByteOrder order, const std::size_t offset) {
  x11::ByteReader reader(bytes.subspan(offset), order);
  std::uint32_t value{};
  require(reader.read_u32(value), "decode COMPOSITE u32");
  return value;
}

DispatchResult redirect(ServerState& state, const DispatchContext& context,
                        const std::uint8_t minor, const std::uint32_t window,
                        const std::uint8_t mode) {
  auto writer = header(context.byte_order, 133, minor, 3);
  writer.write_u32(window);
  writer.write_u8(mode);
  writer.write_padding(3);
  return dispatch_request(state, context,
                          finish(std::move(writer), 133, minor));
}

DispatchResult name_pixmap(ServerState& state, const DispatchContext& context,
                           const std::uint32_t window,
                           const std::uint32_t pixmap) {
  auto writer = header(context.byte_order, 133, 6, 3);
  writer.write_u32(window);
  writer.write_u32(pixmap);
  return dispatch_request(state, context, finish(std::move(writer), 133, 6));
}

void create_hierarchy(ServerState& state, const std::uint32_t parent,
                      const std::uint32_t child) {
  WindowCreateSpec spec;
  spec.xid = parent;
  spec.parent = state.screen().root_window;
  spec.width = 4;
  spec.height = 4;
  spec.depth = 24;
  spec.window_class = WindowClass::InputOutput;
  spec.visual = state.screen().root_visual;
  spec.attributes.colormap = state.screen().default_colormap;
  require(state.resources().create_window(1, 0x400000, 0x1fffff, spec) ==
              CreateWindowStatus::Success,
          "create COMPOSITE parent window");
  spec.xid = child;
  spec.parent = parent;
  spec.width = 2;
  spec.height = 2;
  require(state.resources().create_window(1, 0x400000, 0x1fffff, spec) ==
              CreateWindowStatus::Success,
          "create COMPOSITE child window");
}

void test_composite(const x11::ByteOrder order) {
  const ExtensionRegistry extensions(true, {});
  ServerState state;
  DispatchContext first{1, 0x400000, 0x1fffff, 101, order, false, {},
                        &extensions, {}};
  DispatchContext second{2, 0x600000, 0x1fffff, 102, order, false, {},
                         &extensions, {}};
  constexpr std::uint32_t parent = 0x400001;
  constexpr std::uint32_t child = 0x400002;
  constexpr std::uint32_t named1 = 0x400010;
  constexpr std::uint32_t named2 = 0x400011;
  create_hierarchy(state, parent, child);

  auto writer = header(order, 133, 0, 3);
  writer.write_u32(1);
  writer.write_u32(99);
  auto result = dispatch_request(state, first,
                                 finish(std::move(writer), 133, 0));
  require(result.output.size() == 32 && u32(result.output, order, 8) == 0 &&
              u32(result.output, order, 12) == 4,
          "COMPOSITE negotiates exactly 0.4");

  require(redirect(state, first, 1, child, 1).output.empty(),
          "manual direct redirect succeeds");
  result = redirect(state, second, 1, child, 1);
  require(result.output[1] ==
              static_cast<std::uint8_t>(x11::CoreErrorCode::BadAccess) &&
              state.composite().manual_owner(child,
                  CompositeRedirectScope::Direct) == 1,
          "conflicting manual redirect reports BadAccess atomically");
  require(redirect(state, second, 1, child, 0).output.empty() &&
              state.composite().automatic_owner_count(
                  child, CompositeRedirectScope::Direct) == 1,
          "automatic redirect coexists with the manual owner");
  result = redirect(state, second, 3, child, 1);
  require(result.output[1] ==
              static_cast<std::uint8_t>(x11::CoreErrorCode::BadValue),
          "unredirect validates mode-specific ownership");
  (void)state.cleanup_client(2);
  require(state.composite().automatic_owner_count(
              child, CompositeRedirectScope::Direct) == 0 &&
              state.composite().manual_owner(child,
                  CompositeRedirectScope::Direct) == 1,
          "client cleanup removes only its redirection ownership");

  require(name_pixmap(state, first, child, named1).output.empty() &&
              name_pixmap(state, first, child, named2).output.empty(),
          "NameWindowPixmap accepts redirected window and two names");
  auto* window = state.resources().find_window(child);
  auto* first_named = state.resources().find_pixmap(named1);
  auto* second_named = state.resources().find_pixmap(named2);
  require(window && window->storage && first_named && second_named &&
              first_named->pixels() == window->storage.get() &&
              second_named->pixels() == window->storage.get(),
          "named pixmaps share the window's current canonical storage");
  window->storage->at(0, 0) = 0xFF123456U;
  require(first_named->pixels()->at(0, 0) == 0xFF123456U,
          "window drawing remains visible through a named pixmap");
  require(state.resources().canonical_drawable_bytes() == 16,
          "shared names count canonical storage exactly once");
  auto* old_storage = first_named->pixels();

  LocalConfigure resize;
  resize.width = 3;
  resize.height = 3;
  require(state.resources().configure_local(child, resize) ==
              LocalLifecycleStatus::Success &&
              state.resources().find_window(child)->storage.get() != old_storage &&
              first_named->pixels() == old_storage &&
              first_named->pixels()->width() == 2,
          "window resize swaps storage while named pixmap retains snapshot");
  require(state.resources().set_local_map_intent(child, false) ==
              LocalLifecycleStatus::Success &&
              first_named->pixels()->at(0, 0) == 0xFF123456U,
          "window unmap does not invalidate named pixmap storage");

  writer = header(order, 4, 0, 2);
  writer.write_u32(child);
  result = dispatch_request(state, first, finish(std::move(writer), 4, 0));
  require(result.output.empty() && !state.resources().find_window(child) &&
              first_named->pixels() == old_storage &&
              !state.composite().redirected(child,
                  CompositeRedirectScope::Direct),
          "window destroy preserves named snapshot and clears redirects");
  require(state.resources().invariants_hold(),
          "named pixmap lifetime preserves resource invariants");
  require(state.resources().free_pixmap(named1) == FreePixmapStatus::Success &&
              state.resources().canonical_drawable_bytes() == 16 &&
              state.resources().free_pixmap(named2) == FreePixmapStatus::Success &&
              state.resources().canonical_drawable_bytes() == 0,
          "unique shared-storage accounting releases only the final name");

  require(redirect(state, first, 2, state.screen().root_window, 0)
              .output.empty(),
          "automatic root-subtree redirection is supported");
  result = redirect(state, first, 1, state.screen().root_window, 0);
  require(result.output[1] ==
              static_cast<std::uint8_t>(x11::CoreErrorCode::BadMatch),
          "direct root redirection reports BadMatch");
}

}  // namespace

int main() {
  for (const auto order : {x11::ByteOrder::LittleEndian,
                           x11::ByteOrder::BigEndian})
    test_composite(order);
  return 0;
}
