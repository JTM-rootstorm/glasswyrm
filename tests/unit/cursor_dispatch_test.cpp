#include "glasswyrmd/request_dispatcher.hpp"
#include "helpers/test_support.hpp"
#include "input/cursor_model.hpp"
#include "protocol/x11/byte_cursor.hpp"
#include "protocol/x11/core.hpp"

#include <cstdint>
#include <span>
#include <string_view>
#include <utility>

namespace x11 = gw::protocol::x11;
using namespace glasswyrm::server;
using gw::test::require;

namespace {

x11::ByteWriter header(const x11::ByteOrder order,
                       const x11::CoreOpcode opcode,
                       const std::uint8_t data,
                       const std::uint16_t units) {
  x11::ByteWriter writer(order);
  writer.write_u8(static_cast<std::uint8_t>(opcode));
  writer.write_u8(data);
  writer.write_u16(units);
  return writer;
}

x11::FramedRequest finish(x11::ByteWriter writer,
                          const x11::CoreOpcode opcode,
                          const std::uint8_t data = 0) {
  x11::FramedRequest request;
  request.opcode = static_cast<std::uint8_t>(opcode);
  request.data = data;
  request.bytes = std::move(writer).take();
  request.length_units =
      static_cast<std::uint16_t>(request.bytes.size() / 4U);
  return request;
}

DispatchResult dispatch(ServerState& state, const DispatchContext& context,
                        x11::ByteWriter writer,
                        const x11::CoreOpcode opcode,
                        const std::uint8_t data = 0) {
  return dispatch_request(state, context,
                          finish(std::move(writer), opcode, data));
}

void require_error(const DispatchResult& result,
                   const x11::CoreErrorCode code,
                   const std::string_view message) {
  require(result.output.size() == 32U && result.output[0] == 0 &&
              result.output[1] == static_cast<std::uint8_t>(code),
          message);
}

DispatchResult open_font(ServerState& state, const DispatchContext& context,
                         const std::uint32_t xid,
                         const std::string_view name) {
  const auto padded = (name.size() + 3U) & ~std::size_t{3U};
  auto writer = header(context.byte_order, x11::CoreOpcode::OpenFont, 0,
                       static_cast<std::uint16_t>((12U + padded) / 4U));
  writer.write_u32(xid);
  writer.write_u16(static_cast<std::uint16_t>(name.size()));
  writer.write_u16(0);
  writer.write_bytes(std::span(
      reinterpret_cast<const std::uint8_t*>(name.data()), name.size()));
  writer.write_padding(padded - name.size());
  return dispatch(state, context, std::move(writer),
                  x11::CoreOpcode::OpenFont);
}

void write_colors(x11::ByteWriter& writer,
                  const glasswyrm::input::CursorColor foreground,
                  const glasswyrm::input::CursorColor background) {
  writer.write_u16(foreground.red);
  writer.write_u16(foreground.green);
  writer.write_u16(foreground.blue);
  writer.write_u16(background.red);
  writer.write_u16(background.green);
  writer.write_u16(background.blue);
}

}  // namespace

int main() {
  constexpr std::uint32_t base = 0x00400000U;
  constexpr std::uint32_t other_base = 0x00800000U;
  constexpr std::uint32_t mask = 0x001fffffU;
  for (const auto order : {x11::ByteOrder::LittleEndian,
                           x11::ByteOrder::BigEndian}) {
    ServerState state;
    DispatchContext context{1, base, mask, 0x12345, order};
    const auto root = state.screen().root_window;
    require(state.resources().create_pixmap(1, base, mask, base + 1U, root, 1,
                                            2, 2) ==
                    CreatePixmapStatus::Success &&
                state.resources().create_pixmap(1, base, mask, base + 2U,
                                                root, 1, 2, 2) ==
                    CreatePixmapStatus::Success,
            "create depth-one cursor pixmaps");
    auto* source = state.resources().find_pixmap(base + 1U)->bitmap();
    auto* mask_bitmap = state.resources().find_pixmap(base + 2U)->bitmap();
    source->set(0, 0, 1);
    source->set(1, 1, 1);
    mask_bitmap->set(0, 0, 1);
    mask_bitmap->set(1, 0, 1);
    mask_bitmap->set(1, 1, 1);

    WindowCreateSpec window;
    window.xid = other_base + 1U;
    window.parent = root;
    window.width = 20;
    window.height = 20;
    window.window_class = WindowClass::InputOutput;
    require(state.resources().create_window(2, other_base, mask, window) ==
                CreateWindowStatus::Success,
            "create cursor target window");

    auto create = header(order, x11::CoreOpcode::CreateCursor, 0, 8);
    create.write_u32(base + 3U);
    create.write_u32(base + 1U);
    create.write_u32(base + 2U);
    write_colors(create, {0xffff, 0, 0}, {0, 0xffff, 0});
    create.write_u16(1);
    create.write_u16(1);
    auto result = dispatch(state, context, std::move(create),
                           x11::CoreOpcode::CreateCursor);
    const auto* cursor = state.resources().find_cursor(base + 3U);
    require(result.output.empty() && cursor && cursor->image &&
                cursor->image->kind == glasswyrm::input::CursorKind::Pixmap &&
                cursor->image->premultiplied_argb[0] == 0xffff0000U &&
                cursor->image->premultiplied_argb[1] == 0xff00ff00U,
            "CreateCursor stores a depth-one ARGB cursor");

    auto change = header(order, x11::CoreOpcode::ChangeWindowAttributes, 0, 4);
    change.write_u32(window.xid);
    change.write_u32(1U << 14U);
    change.write_u32(base + 3U);
    result = dispatch(state, context, std::move(change),
                      x11::CoreOpcode::ChangeWindowAttributes);
    auto* target = state.resources().find_window(window.xid);
    auto original = target->attributes.cursor_image;
    require(result.output.empty() && !target->attributes.cursor_inherit &&
                target->attributes.cursor == base + 3U && original &&
                state.resources().effective_cursor(window.xid) == original,
            "CWCursor selects an explicit cursor");

    auto recolor = header(order, x11::CoreOpcode::RecolorCursor, 0, 5);
    recolor.write_u32(base + 3U);
    write_colors(recolor, {0, 0, 0xffff}, {0xffff, 0xffff, 0});
    result = dispatch(state, context, std::move(recolor),
                      x11::CoreOpcode::RecolorCursor);
    auto recolored = state.resources().find_cursor(base + 3U)->image;
    target = state.resources().find_window(window.xid);
    require(result.output.empty() && recolored != original &&
                recolored->premultiplied_argb[0] == 0xff0000ffU &&
                recolored->premultiplied_argb[1] == 0xffffff00U &&
                original->premultiplied_argb[0] == 0xffff0000U &&
                target->attributes.cursor_image == recolored,
            "RecolorCursor atomically updates live XID users");

    auto typed_free = header(order, x11::CoreOpcode::FreeCursor, 0, 2);
    typed_free.write_u32(base + 1U);
    result = dispatch(state, context, std::move(typed_free),
                      x11::CoreOpcode::FreeCursor);
    require_error(result, x11::CoreErrorCode::BadCursor,
                  "FreeCursor rejects a pixmap XID");
    require(state.resources().find_pixmap(base + 1U) != nullptr,
            "typed cursor failure preserves pixmap");

    auto free = header(order, x11::CoreOpcode::FreeCursor, 0, 2);
    free.write_u32(base + 3U);
    result = dispatch(state, context, std::move(free),
                      x11::CoreOpcode::FreeCursor);
    target = state.resources().find_window(window.xid);
    require(result.output.empty() &&
                !state.resources().find_cursor(base + 3U) &&
                target->attributes.cursor == 0 &&
                !target->attributes.cursor_inherit &&
                target->attributes.cursor_image == recolored &&
                state.resources().effective_cursor(window.xid) == recolored,
            "FreeCursor invalidates the XID but preserves active image lifetime");

    auto invalid = header(order, x11::CoreOpcode::ChangeWindowAttributes, 0, 4);
    invalid.write_u32(window.xid);
    invalid.write_u32(1U << 14U);
    invalid.write_u32(base + 99U);
    result = dispatch(state, context, std::move(invalid),
                      x11::CoreOpcode::ChangeWindowAttributes);
    require_error(result, x11::CoreErrorCode::BadCursor,
                  "CWCursor validates cursor type");
    require(state.resources().find_window(window.xid)->attributes.cursor_image ==
                recolored,
            "invalid CWCursor is atomic");

    auto inherit = header(order, x11::CoreOpcode::ChangeWindowAttributes, 0, 4);
    inherit.write_u32(window.xid);
    inherit.write_u32(1U << 14U);
    inherit.write_u32(0);
    result = dispatch(state, context, std::move(inherit),
                      x11::CoreOpcode::ChangeWindowAttributes);
    target = state.resources().find_window(window.xid);
    require(result.output.empty() && target->attributes.cursor_inherit &&
                !target->attributes.cursor_image &&
                state.resources().effective_cursor(window.xid) ==
                    state.resources().root_default_cursor(),
            "CWCursor None restores ancestor and root inheritance");

    require(open_font(state, context, base + 4U, "cursor").output.empty() &&
                open_font(state, context, base + 5U, "nil2").output.empty() &&
                open_font(state, context, base + 6U, "fixed").output.empty(),
            "OpenFont exposes cursor, nil2, and fixed identities");
    auto glyph = header(order, x11::CoreOpcode::CreateGlyphCursor, 0, 8);
    glyph.write_u32(base + 7U);
    glyph.write_u32(base + 4U);
    glyph.write_u32(base + 4U);
    glyph.write_u16(glasswyrm::input::kCursorGlyphXterm);
    glyph.write_u16(glasswyrm::input::kCursorGlyphXterm + 1U);
    write_colors(glyph, {}, {0xffff, 0xffff, 0xffff});
    result = dispatch(state, context, std::move(glyph),
                      x11::CoreOpcode::CreateGlyphCursor);
    require(result.output.empty() &&
                state.resources().find_cursor(base + 7U)->image->kind ==
                    glasswyrm::input::CursorKind::XtermText,
            "CreateGlyphCursor recognizes xterm cursorfont identity");

    auto scrollbar = header(order, x11::CoreOpcode::CreateGlyphCursor, 0, 8);
    scrollbar.write_u32(base + 9U);
    scrollbar.write_u32(base + 4U);
    scrollbar.write_u32(base + 4U);
    scrollbar.write_u16(
        glasswyrm::input::kCursorGlyphVerticalDoubleArrow);
    scrollbar.write_u16(
        glasswyrm::input::kCursorGlyphVerticalDoubleArrow + 1U);
    write_colors(scrollbar, {}, {0xffff, 0xffff, 0xffff});
    result = dispatch(state, context, std::move(scrollbar),
                      x11::CoreOpcode::CreateGlyphCursor);
    require(result.output.empty() &&
                state.resources().find_cursor(base + 9U)->image->kind ==
                    glasswyrm::input::CursorKind::VerticalResize,
            "CreateGlyphCursor recognizes xterm scrollbar cursor identity");

    auto hidden = header(order, x11::CoreOpcode::CreateGlyphCursor, 0, 8);
    hidden.write_u32(base + 8U);
    hidden.write_u32(base + 5U);
    hidden.write_u32(base + 6U);
    hidden.write_u16('X');
    hidden.write_u16(' ');
    write_colors(hidden, {}, {0xffff, 0xffff, 0xffff});
    result = dispatch(state, context, std::move(hidden),
                      x11::CoreOpcode::CreateGlyphCursor);
    require(result.output.empty() &&
                state.resources().find_cursor(base + 8U)->image->kind ==
                    glasswyrm::input::CursorKind::HiddenGlyph,
            "CreateGlyphCursor recognizes xterm nil2-fixed hidden cursor");

    auto best = header(order, x11::CoreOpcode::QueryBestSize, 0, 3);
    best.write_u32(root);
    best.write_u16(1);
    best.write_u16(1);
    result = dispatch(state, context, std::move(best),
                      x11::CoreOpcode::QueryBestSize, 0);
    x11::ByteReader best_reply(
        std::span<const std::uint8_t>(result.output).subspan(8), order);
    std::uint16_t best_width = 0, best_height = 0;
    require(best_reply.read_u16(best_width) &&
                best_reply.read_u16(best_height) && best_width == 64 &&
                best_height == 64,
            "QueryBestSize reports the cursor class bound");

    auto pattern = header(order, x11::CoreOpcode::QueryBestSize, 1, 3);
    pattern.write_u32(root);
    pattern.write_u16(0);
    pattern.write_u16(20000);
    result = dispatch(state, context, std::move(pattern),
                      x11::CoreOpcode::QueryBestSize, 1);
    x11::ByteReader pattern_reply(
        std::span<const std::uint8_t>(result.output).subspan(8), order);
    require(pattern_reply.read_u16(best_width) &&
                pattern_reply.read_u16(best_height) && best_width == 1 &&
                best_height == 16384,
            "QueryBestSize bounds tile and stipple requests");

    auto bad_class = header(order, x11::CoreOpcode::QueryBestSize, 3, 3);
    bad_class.write_u32(root);
    bad_class.write_u16(1);
    bad_class.write_u16(1);
    result = dispatch(state, context, std::move(bad_class),
                      x11::CoreOpcode::QueryBestSize, 3);
    require_error(result, x11::CoreErrorCode::BadValue,
                  "QueryBestSize rejects unknown classes");

    auto active_glyph = header(order, x11::CoreOpcode::ChangeWindowAttributes,
                               0, 4);
    active_glyph.write_u32(window.xid);
    active_glyph.write_u32(1U << 14U);
    active_glyph.write_u32(base + 7U);
    result = dispatch(state, context, std::move(active_glyph),
                      x11::CoreOpcode::ChangeWindowAttributes);
    const auto cleanup_image =
        state.resources().find_window(window.xid)->attributes.cursor_image;
    const auto cleanup = state.resources().cleanup_client(1);
    target = state.resources().find_window(window.xid);
    require(result.output.empty() && cleanup.resources_destroyed == 8 && target &&
                !target->attributes.cursor_inherit &&
                target->attributes.cursor == 0 &&
                target->attributes.cursor_image == cleanup_image &&
                state.resources().effective_cursor(window.xid) == cleanup_image &&
                state.resources().invariants_hold(),
            "client cleanup is typed and preserves cross-client active images");
  }
}
