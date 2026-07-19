#include "glasswyrmd/extension_registry.hpp"
#include "glasswyrmd/extensions/gw_vrr.hpp"
#include "glasswyrmd/lifecycle_coordinator.hpp"
#include "glasswyrmd/request_dispatcher.hpp"
#include "protocol/x11/byte_cursor.hpp"
#include "protocol/x11/core.hpp"
#include "tests/helpers/test_support.hpp"

#include <span>

namespace {

using namespace glasswyrm::server;
namespace x11 = gw::protocol::x11;
using gw::test::require;

constexpr ClientId kOwner = 7;
constexpr ClientId kObserver = 9;
constexpr std::uint32_t kBase = 0x00400000;
constexpr std::uint32_t kMask = 0x001fffff;
constexpr std::uint32_t kWindow = kBase + 1;

std::uint16_t u16(const std::vector<std::uint8_t>& bytes,
                  const x11::ByteOrder order, const std::size_t offset) {
  x11::ByteReader reader(std::span<const std::uint8_t>(bytes).subspan(offset),
                         order);
  std::uint16_t value{};
  require(reader.read_u16(value), "decode GW_VRR integration u16");
  return value;
}

std::uint32_t u32(const std::vector<std::uint8_t>& bytes,
                  const x11::ByteOrder order, const std::size_t offset) {
  x11::ByteReader reader(std::span<const std::uint8_t>(bytes).subspan(offset),
                         order);
  std::uint32_t value{};
  require(reader.read_u32(value), "decode GW_VRR integration u32");
  return value;
}

x11::FramedRequest request(const x11::ByteOrder order,
                           const std::uint8_t opcode,
                           const std::uint8_t data,
                           const std::initializer_list<std::uint32_t> fields) {
  x11::ByteWriter writer(order);
  writer.write_u8(opcode);
  writer.write_u8(data);
  writer.write_u16(static_cast<std::uint16_t>(1 + fields.size()));
  for (const auto field : fields) writer.write_u32(field);
  x11::FramedRequest framed;
  framed.opcode = opcode;
  framed.data = data;
  framed.length_units = static_cast<std::uint32_t>(1 + fields.size());
  framed.bytes = std::move(writer).take();
  return framed;
}

void create_window(ServerState& state, const ClientId owner,
                   const std::uint32_t base, const std::uint32_t xid) {
  WindowCreateSpec spec;
  spec.xid = xid;
  spec.parent = state.screen().root_window;
  spec.width = 320;
  spec.height = 240;
  spec.depth = state.screen().root_depth;
  spec.window_class = WindowClass::InputOutput;
  spec.visual = state.screen().root_visual;
  spec.attributes.colormap = state.screen().default_colormap;
  require(state.resources().create_window(owner, base, kMask, spec) ==
              CreateWindowStatus::Success,
          "create direct-root GW_VRR window");
}

void test_lifecycle_completion(const x11::ByteOrder order,
                               const std::uint64_t sequence,
                               const LifecycleSnapshot& snapshot,
                               const DeferredVrrMutation& mutation) {
  std::vector<std::uint8_t> delivered;
  bool committed = false;
  const auto callbacks = [&] {
    LifecycleCallbacks value;
    value.send_policy = [](const LifecycleSnapshot&) { return true; };
    value.send_compositor = [](const LifecycleSnapshot&) { return true; };
    value.commit = [&](const LifecycleSnapshot&) {
      committed = true;
      return true;
    };
    value.complete = [&](const std::uint64_t, const bool accepted) {
      delivered = extensions::gw_vrr_lifecycle_completion(
          order, sequence, mutation, accepted);
    };
    return value;
  };
  const auto operation = [&] {
    LifecycleOperation value;
    value.token = 1;
    value.client_id = kOwner;
    value.request_sequence = sequence;
    value.kind = LifecycleOperationKind::VrrChange;
    value.window = mutation.window;
    value.proposed = snapshot;
    return value;
  };

  LifecycleCoordinator accepted(snapshot, 4, callbacks());
  require(accepted.enqueue(operation()) == EnqueueStatus::Queued &&
              delivered.empty(),
          "GW_VRR success reply remains hidden while policy is pending");
  require(accepted.policy_accepted(snapshot) && delivered.empty(),
          "GW_VRR success reply remains hidden while compositor is pending");
  require(accepted.compositor_accepted() && committed &&
              delivered.size() == 32 && delivered[0] == 1 &&
              u16(delivered, order, 2) == sequence,
          "accepted lifecycle completion releases the staged success reply");

  delivered.clear();
  committed = false;
  LifecycleCoordinator rejected(snapshot, 4, callbacks());
  require(rejected.enqueue(operation()) == EnqueueStatus::Queued &&
              delivered.empty() && rejected.policy_rejected() &&
              !committed && delivered.size() == 32 && delivered[0] == 0 &&
              delivered[1] == static_cast<std::uint8_t>(
                                    x11::CoreErrorCode::BadImplementation) &&
              u16(delivered, order, 2) == sequence &&
              u32(delivered, order, 4) == kWindow &&
              u16(delivered, order, 8) == 3 && delivered[10] == 136,
          "rejected lifecycle completion returns deterministic GW_VRR error");
}

void test_registry_and_dispatch(const x11::ByteOrder order) {
  const auto* descriptor = find_extension(ExtensionKind::GwVrr);
  require(descriptor && descriptor->name == "GW_VRR" &&
              descriptor->major_opcode == 136 &&
              descriptor->first_event == 70 && descriptor->event_count == 1 &&
              descriptor->first_error == 141 &&
              descriptor->error_count == 2 &&
              descriptor->maximum_major_version == 0 &&
              descriptor->maximum_minor_version == 1,
          "GW_VRR registry assignment is frozen");

  ServerState state;
  create_window(state, kOwner, kBase, kWindow);
  const ExtensionRegistry extensions(ExtensionCapability::VrrProtocol, {});
  DispatchContext integrated{kOwner, kBase, kMask, 3, order, true, {},
                             &extensions};

  auto result = dispatch_request(
      state, integrated,
      request(order, 136, 0, {0, 99}));
  require(result.output.size() == 32 && result.output[0] == 1,
          "central dispatcher routes GW_VRR QueryVersion");

  result = dispatch_request(
      state, integrated,
      request(order, 136, 1, {kWindow, kVrrPreferenceChanged}));
  require(result.output.empty() && state.vrr().find_window(kWindow) &&
              state.vrr()
                      .find_window(kWindow)
                      ->event_selections.at(kOwner) ==
                  kVrrPreferenceChanged,
          "central dispatcher stores GW_VRR event selection");

  result = dispatch_request(
      state, integrated,
      request(order, 136, 3,
              {kWindow,
               static_cast<std::uint32_t>(WindowVrrPreference::Prefer)}));
  require(result.kind == DispatchKind::DeferredLifecycle &&
              result.deferred_vrr && result.deferred_window == kWindow &&
              result.deferred_vrr->preference == WindowVrrPreference::Prefer &&
              result.output.empty() &&
              result.deferred_vrr->accepted_reply.size() == 32 &&
              state.vrr().find_window(kWindow)->preference ==
                  WindowVrrPreference::Default,
          "integrated SetWindowPreference defers state and its success reply");
  test_lifecycle_completion(order, integrated.sequence,
                            state.lifecycle_snapshot(), *result.deferred_vrr);

  DispatchContext local{kOwner, kBase, kMask, 4, order, false, {},
                        &extensions};
  result = dispatch_request(
      state, local,
      request(order, 136, 3,
              {kWindow,
               static_cast<std::uint32_t>(WindowVrrPreference::Allow)}));
  require(result.kind == DispatchKind::Immediate &&
              state.vrr().find_window(kWindow)->preference ==
                  WindowVrrPreference::Allow,
          "non-integrated dispatcher commits the standalone preference path");

  const ExtensionRegistry absent(ExtensionCapability::None, {});
  local.extensions = &absent;
  result = dispatch_request(state, local, request(order, 136, 0, {0, 1}));
  require(result.output.size() == 32 && result.output[0] == 0 &&
              result.output[1] ==
                  static_cast<std::uint8_t>(x11::CoreErrorCode::BadRequest),
          "GW_VRR stays absent without its independent capability");
}

void test_lifecycle_rebase_and_cleanup(const x11::ByteOrder order) {
  ServerState state;
  create_window(state, kOwner, kBase, kWindow);
  auto proposed = state.lifecycle_snapshot();
  auto committed = proposed;
  committed.windows.at(kWindow).map_requested = true;
  LifecycleOperation operation;
  operation.kind = LifecycleOperationKind::VrrChange;
  operation.window = kWindow;
  operation.proposed = proposed;
  const auto rebased = rebase_lifecycle_operation(committed, operation);
  require(rebased && rebased->windows.at(kWindow).map_requested,
          "VRR transaction rebase preserves newer lifecycle state");

  auto& tracked = state.vrr().ensure_window(kWindow);
  tracked.preference = WindowVrrPreference::Prefer;
  tracked.event_selections.emplace(kOwner, kKnownVrrEventMask);
  tracked.event_selections.emplace(kObserver, kKnownVrrEventMask);
  DispatchContext local{kOwner, kBase, kMask, 5, order};
  auto result = dispatch_request(
      state, local,
      request(order,
              static_cast<std::uint8_t>(x11::CoreOpcode::DestroyWindow), 0,
              {kWindow}));
  require(result.output.empty() && state.resources().find_window(kWindow) == nullptr &&
              state.vrr().find_window(kWindow) == nullptr,
          "local destruction removes ephemeral GW_VRR state");

  constexpr std::uint32_t kForeignBase = 0x00600000;
  constexpr std::uint32_t kForeignWindow = kForeignBase + 1;
  create_window(state, kObserver, kForeignBase, kForeignWindow);
  auto& foreign = state.vrr().ensure_window(kForeignWindow);
  foreign.event_selections.emplace(kOwner, kKnownVrrEventMask);
  foreign.event_selections.emplace(kObserver, kKnownVrrEventMask);
  (void)state.cleanup_client(kOwner);
  require(!state.vrr().find_window(kForeignWindow)->event_selections.contains(
              kOwner) &&
              state.vrr()
                  .find_window(kForeignWindow)
                  ->event_selections.contains(kObserver),
          "client cleanup removes only the departing GW_VRR subscription");
}

}  // namespace

int main() {
  test_registry_and_dispatch(x11::ByteOrder::LittleEndian);
  test_registry_and_dispatch(x11::ByteOrder::BigEndian);
  test_lifecycle_rebase_and_cleanup(x11::ByteOrder::LittleEndian);
  test_lifecycle_rebase_and_cleanup(x11::ByteOrder::BigEndian);
  return 0;
}
