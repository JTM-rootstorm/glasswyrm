#include "glasswyrmd/extension_registry.hpp"
#include "glasswyrmd/extension_event_helpers.hpp"
#include "glasswyrmd/randr_state.hpp"
#include "glasswyrmd/request_dispatcher.hpp"
#include "output/model/layout.hpp"
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

x11::FramedRequest present_request(const x11::ByteOrder order,
                                   const std::uint32_t window,
                                   const std::uint32_t pixmap,
                                   const std::uint32_t damage,
                                   const std::uint32_t serial) {
  auto writer = header(order, 5, 5);
  writer.write_u32(window);
  writer.write_u32(pixmap);
  writer.write_u32(damage);
  writer.write_u32(serial);
  return finish(std::move(writer), 5);
}

x11::FramedRequest map_request(const x11::ByteOrder order,
                               const std::uint32_t window) {
  x11::ByteWriter writer(order);
  writer.write_u8(8);
  writer.write_u8(0);
  writer.write_u16(2);
  writer.write_u32(window);
  x11::FramedRequest request;
  request.opcode = 8;
  request.bytes = std::move(writer).take();
  request.length_units = 2;
  return request;
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

void install_output_layout(ServerState& state) {
  using namespace glasswyrm::output;
  constexpr OutputId output_id{UINT64_C(0x1122334455667788)};
  constexpr OutputModeId mode_id{9};
  OutputDescriptor descriptor;
  descriptor.id = output_id;
  descriptor.name = "SCALE-OUT";
  descriptor.connected = true;
  descriptor.mode_configurable = true;
  descriptor.scale_configurable = true;
  descriptor.transform_configurable = true;
  descriptor.primary_eligible = true;
  descriptor.arbitrary_headless_mode = true;
  descriptor.supported_transform_mask = kAllOutputTransformsMask;
  descriptor.modes.push_back(
      {mode_id, output_id, 800, 600, 60'000, 0, "800x600", true, true});
  OutputState output;
  output.output_id = output_id;
  output.enabled = true;
  output.mode_id = mode_id;
  output.logical_x = 0;
  output.logical_y = 0;
  output.logical_width = 640;
  output.logical_height = 480;
  output.physical_width = 800;
  output.physical_height = 600;
  output.refresh_millihertz = 60'000;
  output.scale = {5, 4};
  output.transform = OutputTransform::Rotate180;
  output.primary = true;
  output.generation = UINT64_C(0x0102030405060708);
  OutputLayout layout;
  layout.descriptors.emplace(output_id, std::move(descriptor));
  layout.states.emplace(output_id, output);
  layout.primary_output_id = output_id;
  layout.root_logical_width = 640;
  layout.root_logical_height = 480;
  layout.generation = output.generation;
  layout.enabled_output_count = 1;
  layout.output_order = {output_id};
  require(state.randr().configure_output_layout(layout),
          "install GW_SCALE output topology");
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

  constexpr std::uint32_t scaled_pixmap = 0x400010;
  constexpr std::uint32_t damage_region = 0x400011;
  constexpr std::uint32_t bad_region = 0x400012;
  require(state.resources().create_pixmap(
              owner.client_id, owner.resource_base, owner.resource_mask,
              scaled_pixmap, owned, 24, 600, 300) ==
              CreatePixmapStatus::Success,
          "create depth-24 scaled presentation pixmap");
  const std::array damage_rectangles{
      glasswyrm::geometry::Rectangle{4, 5, 8, 9}};
  require(state.resources().create_xfixes_region(
              owner.client_id, owner.resource_base, owner.resource_mask,
              damage_region, damage_rectangles) == RegionStatus::Success,
          "create bounded scaled-pixmap damage region");
  const std::array outside_rectangles{
      glasswyrm::geometry::Rectangle{599, 299, 2, 2}};
  require(state.resources().create_xfixes_region(
              owner.client_id, owner.resource_base, owner.resource_mask,
              bad_region, outside_rectangles) == RegionStatus::Success,
          "create out-of-bounds scaled-pixmap damage region");
  const auto retained = state.resources().find_pixmap(scaled_pixmap)->pixels();
  result = dispatch_request(
      state, owner,
      present_request(order, owned, scaled_pixmap, damage_region, 77));
  require(result.output.size() == 32 && u32(result.output, order, 8) == 77 &&
              u32(result.output, order, 12) == 3 &&
              result.drawable_damage.size() == 1 &&
              result.drawable_damage[0].rectangle ==
                  glasswyrm::geometry::Rectangle{1, 1, 3, 4} &&
              result.drawable_damage[0].buffer_rectangle ==
                  damage_rectangles[0],
          "PresentScaledPixmap echoes serial and separates logical/pixel damage");
  require(state.resources().find_window(owned)->scale.presentation ==
                  WindowScalePresentationState::ScaleAwareActive &&
              state.resources()
                      .find_window(owned)
                      ->scale.scaled_pixmap_storage.get() == retained &&
              state.resources().find_window(owned)->scale.presentation_serial ==
                  77,
          "PresentScaledPixmap retains shared pixmap storage and serial");
  require(state.resources().free_pixmap(scaled_pixmap) ==
                  FreePixmapStatus::Success &&
              state.resources()
                      .find_window(owned)
                      ->scale.scaled_pixmap_storage.get() == retained,
          "freeing the pixmap XID preserves retained scaled presentation");

  result = dispatch_request(
      state, owner,
      present_request(order, owned, scaled_pixmap, damage_region, 78));
  require_error(result, x11::CoreErrorCode::BadPixmap, order, scaled_pixmap,
                5);
  require(state.resources().create_pixmap(
              owner.client_id, owner.resource_base, owner.resource_mask,
              scaled_pixmap, owned, 24, 600, 300) ==
              CreatePixmapStatus::Success,
          "recreate scaled presentation pixmap");
  result = dispatch_request(
      state, owner,
      present_request(order, owned, scaled_pixmap, bad_region, 78));
  require(result.output.size() == 32 && result.output[1] == 139 &&
              u32(result.output, order, 4) == bad_region,
          "out-of-bounds pixel damage reports BadScale");
  state.resources().find_window(nested)->map_requested = true;
  result = dispatch_request(
      state, owner,
      present_request(order, owned, scaled_pixmap, 0, 78));
  require(result.output.size() == 32 && result.output[1] == 140 &&
              u32(result.output, order, 4) == owned,
          "mapped InputOutput child reports BadScaleMode");
  state.resources().find_window(nested)->map_requested = false;
  state.resources().find_window(owned)->scale.presentation =
      WindowScalePresentationState::ScaleAwareAwaitingPixmap;
  state.resources().find_window(owned)->scale.scaled_pixmap_storage.reset();
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
  require(result.protocol_events.size() == 1 &&
              std::get<GwScaleNotifyEvent>(result.protocol_events[0].event)
                      .reason_mask == 4,
          "ResetWindowBufferScale emits selected invalidation notification");
  const auto notify = encode_gw_scale_notify(
      order, 0x12345,
      std::get<GwScaleNotifyEvent>(result.protocol_events[0].event));
  require(notify.size() == 32 && notify[0] == 69 && notify[1] == 4 &&
              u16(notify, order, 2) == 0x2345 &&
              u32(notify, order, 4) == owned &&
              u32(notify, order, 12) == 3 &&
              u32(notify, order, 16) == 2 &&
              u32(notify, order, 20) == 1 &&
              u32(notify, order, 24) == 0x01020304 &&
              u32(notify, order, 28) == 0x05060708,
          "ScaleNotify has the exact fixed 32-byte wire shape");
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

  install_output_layout(state);
  result = dispatch_request(state, owner,
                            window_request(order, 2, kRandROutputId));
  require(result.output.size() == 60 && u32(result.output, order, 4) == 7 &&
              u32(result.output, order, 8) == 0x11223344 &&
              u32(result.output, order, 12) == 0x55667788 &&
              u32(result.output, order, 16) == 0 &&
              u32(result.output, order, 20) == 0 &&
              u32(result.output, order, 24) == 640 &&
              u32(result.output, order, 28) == 480 &&
              u32(result.output, order, 32) == 800 &&
              u32(result.output, order, 36) == 600 &&
              u32(result.output, order, 40) == 5 &&
              u32(result.output, order, 44) == 4 &&
              u16(result.output, order, 48) == 2 &&
              result.output[50] == 1 && result.output[51] == 1 &&
              u32(result.output, order, 52) == 0x01020304 &&
              u32(result.output, order, 56) == 0x05060708,
          "GetOutputScale reports the exact stable output-model record");
  require_error(dispatch_request(state, owner,
                                 window_request(order, 2, 0xdeadbeef)),
                x11::CoreErrorCode::BadValue, order, 0xdeadbeef, 2);

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

void test_child_mapping_invalidation(const x11::ByteOrder order) {
  ServerState state;
  constexpr std::uint32_t top = 0x400001;
  constexpr std::uint32_t child = 0x400002;
  create_window(state, 1, 0x400000, top, state.screen().root_window);
  create_window(state, 1, 0x400000, child, top);
  auto* window = state.resources().find_window(top);
  window->scale.accepted_buffer_scale = 2;
  window->scale.presentation =
      WindowScalePresentationState::ScaleAwareActive;
  window->scale.event_selections.emplace(1, 7);
  auto storage = PixelStorage::create(400, 200);
  require(storage.has_value(), "create retained child-invalidation pixels");
  window->scale.scaled_pixmap_storage =
      std::make_shared<PixelStorage>(std::move(*storage));
  const ExtensionRegistry enabled(ExtensionCapability::ScaleProtocol, {});
  DispatchContext context{1, 0x400000, 0x1fffff, 1, order, false, {},
                          &enabled};
  const auto result = dispatch_request(state, context,
                                       map_request(order, child));
  require(result.output.empty() &&
              window->scale.presentation ==
                  WindowScalePresentationState::ScaleAwareAwaitingPixmap &&
              !window->scale.scaled_pixmap_storage &&
              result.protocol_events.size() == 1 &&
              std::get<GwScaleNotifyEvent>(result.protocol_events[0].event)
                      .reason_mask == 4,
          "mapping an InputOutput child invalidates active scaled-pixmap mode");
}

void test_integrated_scale_staging(const x11::ByteOrder order) {
  ServerState state;
  constexpr std::uint32_t window = 0x400001;
  create_window(state, 1, 0x400000, window, state.screen().root_window);
  const ExtensionRegistry enabled(ExtensionCapability::ScaleProtocol, {});
  DispatchContext context{1, 0x400000, 0x1fffff, 1, order, true, {},
                          &enabled};
  const auto result = dispatch_request(
      state, context, pair_request(order, 4, window, 2));
  require(result.kind == DispatchKind::DeferredLifecycle &&
              result.deferred_scale &&
              result.deferred_scale->scale.accepted_buffer_scale == 2 &&
              result.deferred_scale->scale.presentation ==
                  WindowScalePresentationState::ScaleAwareAwaitingPixmap &&
              state.resources()
                      .find_window(window)
                      ->scale.accepted_buffer_scale == 1,
          "integrated scale mutation is staged until compositor acceptance");
}

}  // namespace

int main() {
  for (const auto order : {x11::ByteOrder::LittleEndian,
                           x11::ByteOrder::BigEndian}) {
    test_scale_state(order);
    test_profile_absence(order);
    test_child_mapping_invalidation(order);
    test_integrated_scale_staging(order);
  }
  return 0;
}
