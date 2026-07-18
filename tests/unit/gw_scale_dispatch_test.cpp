#include "glasswyrmd/extension_registry.hpp"
#include "glasswyrmd/randr_state.hpp"
#include "glasswyrmd/request_dispatcher.hpp"
#include "protocol/x11/byte_cursor.hpp"
#include "tests/helpers/test_support.hpp"

#include <algorithm>
#include <ranges>
#include <span>

namespace {

using namespace glasswyrm::server;
namespace x11 = gw::protocol::x11;
using gw::test::require;

x11::ByteWriter header(const x11::ByteOrder order, const std::uint8_t minor,
                       const std::uint16_t units) {
  x11::ByteWriter writer(order);
  writer.write_u8(135);
  writer.write_u8(minor);
  writer.write_u16(units);
  return writer;
}

x11::FramedRequest finish(x11::ByteWriter writer, const std::uint8_t minor) {
  x11::FramedRequest request;
  request.opcode = 135;
  request.data = minor;
  request.bytes = std::move(writer).take();
  request.length_units = request.bytes.size() / 4U;
  return request;
}

x11::FramedRequest window_request(const x11::ByteOrder order,
                                  const std::uint8_t minor,
                                  const std::uint32_t window) {
  auto writer = header(order, minor, 2);
  writer.write_u32(window);
  return finish(std::move(writer), minor);
}

x11::FramedRequest pair_request(const x11::ByteOrder order,
                                const std::uint8_t minor,
                                const std::uint32_t window,
                                const std::uint32_t value) {
  auto writer = header(order, minor, 3);
  writer.write_u32(window);
  writer.write_u32(value);
  return finish(std::move(writer), minor);
}

std::uint16_t u16(const std::span<const std::uint8_t> bytes,
                  const x11::ByteOrder order, const std::size_t offset) {
  x11::ByteReader reader(bytes.subspan(offset), order);
  std::uint16_t value{};
  require(reader.read_u16(value), "read GW_SCALE u16");
  return value;
}

std::uint32_t u32(const std::span<const std::uint8_t> bytes,
                  const x11::ByteOrder order, const std::size_t offset) {
  x11::ByteReader reader(bytes.subspan(offset), order);
  std::uint32_t value{};
  require(reader.read_u32(value), "read GW_SCALE u32");
  return value;
}

void create_window(ServerState& state, const ClientId owner,
                   const std::uint32_t base, const std::uint32_t xid,
                   const std::uint32_t parent,
                   const WindowClass window_class = WindowClass::InputOutput) {
  WindowCreateSpec spec;
  spec.xid = xid;
  spec.parent = parent;
  spec.x = 20;
  spec.y = 30;
  spec.width = 200;
  spec.height = 100;
  spec.depth = window_class == WindowClass::InputOnly
                   ? 0
                   : state.screen().root_depth;
  spec.window_class = window_class;
  spec.visual = window_class == WindowClass::InputOnly
                    ? 0
                    : state.screen().root_visual;
  spec.attributes.colormap = state.screen().default_colormap;
  require(state.resources().create_window(owner, base, 0x1fffff, spec) ==
              CreateWindowStatus::Success,
          "create GW_SCALE test window");
}

void require_error(const DispatchResult& result, const x11::CoreErrorCode code,
                   const x11::ByteOrder order, const std::uint32_t bad_value,
                   const std::uint8_t minor) {
  require(result.output.size() == 32 && result.output[0] == 0 &&
              result.output[1] == static_cast<std::uint8_t>(code) &&
              u32(result.output, order, 4) == bad_value &&
              u16(result.output, order, 8) == minor &&
              result.output[10] == 135,
          "GW_SCALE error carries exact code, value, and opcode metadata");
}

void test_scale_state(const x11::ByteOrder order) {
  constexpr std::uint32_t owned = 0x400001;
  constexpr std::uint32_t nested = 0x400002;
  constexpr std::uint32_t input_only = 0x400003;
  constexpr std::uint32_t foreign = 0x600001;
  ServerState state;
  create_window(state, 1, 0x400000, owned, state.screen().root_window);
  create_window(state, 1, 0x400000, nested, owned);
  create_window(state, 1, 0x400000, input_only, state.screen().root_window,
                WindowClass::InputOnly);
  create_window(state, 2, 0x600000, foreign, state.screen().root_window);
  const ExtensionRegistry enabled(ExtensionCapability::ScaleProtocol, {});
  DispatchContext owner{1, 0x400000, 0x1fffff, 0x12345, order, false, {},
                        &enabled};
  DispatchContext other{2, 0x600000, 0x1fffff, 7, order, false, {}, &enabled};

  auto result = dispatch_request(state, owner, pair_request(order, 1, owned, 7));
  require(result.output.empty() &&
              state.resources()
                      .find_window(owned)
                      ->scale.event_selections.at(owner.client_id) == 7,
          "SelectInput records the complete supported mask");
  result = dispatch_request(state, owner, pair_request(order, 1, owned, 8));
  require_error(result, x11::CoreErrorCode::BadValue, order, 8, 1);
  require(state.resources()
              .find_window(owned)
              ->scale.event_selections.at(owner.client_id) == 7,
          "invalid SelectInput mask does not mutate the prior selection");
  require_error(dispatch_request(state, owner,
                                 window_request(order, 1, owned)),
                x11::CoreErrorCode::BadLength, order, 0, 1);
  require_error(dispatch_request(state, owner,
                                 pair_request(order, 1, 0x400099, 1)),
                x11::CoreErrorCode::BadWindow, order, 0x400099, 1);
  require_error(dispatch_request(state, owner,
                                 pair_request(order, 1, nested, 1)),
                x11::CoreErrorCode::BadMatch, order, nested, 1);
  require_error(dispatch_request(state, owner,
                                 pair_request(order, 1, input_only, 1)),
                x11::CoreErrorCode::BadMatch, order, input_only, 1);
  require_error(dispatch_request(state, owner,
                                 pair_request(order, 1, foreign, 1)),
                x11::CoreErrorCode::BadAccess, order, foreign, 1);
  require_error(dispatch_request(state, owner,
                                 pair_request(order, 3, owned, 0)),
                x11::CoreErrorCode::BadLength, order, 0, 3);
  require_error(dispatch_request(state, owner,
                                 window_request(order, 3, nested)),
                x11::CoreErrorCode::BadMatch, order, nested, 3);

  result = dispatch_request(state, owner, window_request(order, 3, owned));
  require(result.output.size() == 44 && result.output[0] == 1 &&
              u16(result.output, order, 2) == 0x2345 &&
              u32(result.output, order, 4) == 3 &&
              u32(result.output, order, 8) == owned &&
              u32(result.output, order, 12) == kRandROutputId &&
              u32(result.output, order, 16) == 1 &&
              u32(result.output, order, 20) == 1 &&
              u32(result.output, order, 24) == 1 &&
              u16(result.output, order, 28) == 1 &&
              u16(result.output, order, 30) == 1 &&
              u32(result.output, order, 32) == 0 &&
              u32(result.output, order, 36) ==
                  kRandRConfigurationTimestamp &&
              u32(result.output, order, 40) == kRandROutputId,
          "GetWindowScale returns the exact fixed-output legacy reply");
  result = dispatch_request(state, owner, window_request(order, 3, foreign));
  require(result.output.size() == 44 && u32(result.output, order, 8) == foreign,
          "GetWindowScale is a read-only query for another client window");

  auto* tracked_scale = &state.resources().find_window(owned)->scale;
  tracked_scale->primary_output = 0x101;
  tracked_scale->preferred_scale_numerator = 3;
  tracked_scale->preferred_scale_denominator = 2;
  tracked_scale->layout_generation = UINT64_C(0x0102030405060708);
  tracked_scale->output_memberships = {0x100, 0x101};
  tracked_scale->has_output_state = true;
  result = dispatch_request(state, owner, window_request(order, 3, owned));
  require(result.output.size() == 48 && u32(result.output, order, 4) == 4 &&
              u32(result.output, order, 12) == 0x101 &&
              u32(result.output, order, 16) == 3 &&
              u32(result.output, order, 20) == 2 &&
              u16(result.output, order, 30) == 2 &&
              u32(result.output, order, 32) == 0x01020304 &&
              u32(result.output, order, 36) == 0x05060708 &&
              u32(result.output, order, 40) == 0x100 &&
              u32(result.output, order, 44) == 0x101,
          "GetWindowScale serializes tracked memberships and 64-bit generation");

  require_error(dispatch_request(state, owner,
                                 window_request(order, 4, owned)),
                x11::CoreErrorCode::BadLength, order, 0, 4);
  result =
      dispatch_request(state, owner, pair_request(order, 4, owned, 3));
  require(result.output.size() == 32 && u32(result.output, order, 4) == 0 &&
              u32(result.output, order, 8) == 3 &&
              u32(result.output, order, 12) == 3 &&
              u32(result.output, order, 16) == 2 &&
              u32(result.output, order, 20) == 0x01020304 &&
              u32(result.output, order, 24) == 0x05060708 &&
              std::ranges::all_of(result.output | std::views::drop(28),
                                  [](const auto byte) { return byte == 0; }),
          "SetWindowBufferScale reports accepted scale and zero padding");
  const auto* scaled = state.resources().find_window(owned);
  require(scaled->scale.accepted_buffer_scale == 3 &&
              scaled->scale.presentation ==
                  WindowScalePresentationState::ScaleAwareAwaitingPixmap,
          "SetWindowBufferScale enters awaiting-pixmap without replacing backing");
  for (const auto bad_scale : {0U, 5U}) {
    result = dispatch_request(
        state, owner, pair_request(order, 4, owned, bad_scale));
    require(result.output.size() == 32 && result.output[1] == 139 &&
                u32(result.output, order, 4) == bad_scale &&
                u16(result.output, order, 8) == 4 && result.output[10] == 135,
            "invalid integer scale reports GW_SCALE BadScale");
    require(state.resources().find_window(owned)->scale.accepted_buffer_scale ==
                3,
            "BadScale leaves the accepted scale unchanged");
  }
  require_error(dispatch_request(state, other,
                                 pair_request(order, 4, owned, 2)),
                x11::CoreErrorCode::BadAccess, order, owned, 4);
  result = dispatch_request(state, owner, window_request(order, 3, owned));
  require(u32(result.output, order, 24) == 3 &&
              u16(result.output, order, 28) == 1,
          "awaiting-pixmap continues reporting legacy presentation mode");

  state.resources().find_window(owned)->scale.presentation =
      WindowScalePresentationState::ScaleAwareActive;
  result = dispatch_request(state, owner, window_request(order, 3, owned));
  require(u16(result.output, order, 28) == 2,
          "active scaled-pixmap state uses the frozen scaled mode value");
  require_error(dispatch_request(state, owner,
                                 pair_request(order, 6, owned, 0)),
                x11::CoreErrorCode::BadLength, order, 0, 6);
  result = dispatch_request(state, owner, window_request(order, 6, owned));
  require(result.output.empty() &&
              state.resources().find_window(owned)->scale.accepted_buffer_scale ==
                  1 &&
              state.resources().find_window(owned)->scale.presentation ==
                  WindowScalePresentationState::Legacy,
          "ResetWindowBufferScale restores the complete legacy state");
  require_error(dispatch_request(state, other,
                                 window_request(order, 6, owned)),
                x11::CoreErrorCode::BadAccess, order, owned, 6);

  auto* offscreen = state.resources().find_window(owned);
  offscreen->scale.has_output_state = false;
  offscreen->x = 2000;
  result = dispatch_request(state, owner, window_request(order, 3, owned));
  require(result.output.size() == 40 && u32(result.output, order, 4) == 2 &&
              u32(result.output, order, 12) == 0 &&
              u16(result.output, order, 30) == 0,
          "entirely offscreen window has no primary output or membership");

  result = dispatch_request(state, owner, pair_request(order, 1, owned, 0));
  require(result.output.empty() &&
              offscreen->scale.event_selections.empty(),
          "zero SelectInput mask removes the selection");
  (void)dispatch_request(state, owner, pair_request(order, 1, owned, 7));
  const auto cleanup = state.resources().prepare_client_cleanup(owner.client_id);
  require(state.resources()
              .find_window(owned)
              ->scale.event_selections.empty(),
          "client cleanup removes GW_SCALE selections before destruction");
  (void)state.resources().commit_client_cleanup(cleanup);
}

void test_profile_absence(const x11::ByteOrder order) {
  ServerState state;
  create_window(state, 1, 0x400000, 0x400001, state.screen().root_window);
  const ExtensionRegistry historical(ExtensionCapability::GameCompat, {});
  DispatchContext context{1, 0x400000, 0x1fffff, 1, order, false, {},
                          &historical};
  const auto result = dispatch_request(
      state, context, pair_request(order, 4, 0x400001, 3));
  require_error(result, x11::CoreErrorCode::BadRequest, order, 0, 4);
  require(state.resources()
                  .find_window(0x400001)
                  ->scale.accepted_buffer_scale == 1,
          "absent GW_SCALE profile preserves historical window behavior");
}

}  // namespace

int main() {
  for (const auto order : {x11::ByteOrder::LittleEndian,
                           x11::ByteOrder::BigEndian}) {
    test_scale_state(order);
    test_profile_absence(order);
  }
  return 0;
}
