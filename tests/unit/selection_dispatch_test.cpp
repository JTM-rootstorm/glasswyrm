#include "glasswyrmd/request_dispatcher.hpp"
#include "helpers/test_support.hpp"
#include "protocol/x11/byte_cursor.hpp"
#include "protocol/x11/core.hpp"
#include "protocol/x11/event_mask.hpp"

#include <cstdint>
#include <variant>

using namespace glasswyrm::server;
namespace x11 = gw::protocol::x11;
using gw::test::require;

namespace {

x11::FramedRequest finish(x11::ByteWriter writer, const x11::CoreOpcode opcode,
                          const std::uint8_t data = 0) {
  auto bytes = std::move(writer).take();
  x11::FramedRequest request;
  request.opcode = static_cast<std::uint8_t>(opcode);
  request.data = data;
  request.length_units = static_cast<std::uint16_t>(bytes.size() / 4U);
  request.bytes = std::move(bytes);
  return request;
}

x11::ByteWriter header(const x11::ByteOrder order,
                       const x11::CoreOpcode opcode, const std::uint16_t units,
                       const std::uint8_t data = 0) {
  x11::ByteWriter writer(order);
  writer.write_u8(static_cast<std::uint8_t>(opcode));
  writer.write_u8(data);
  writer.write_u16(units);
  return writer;
}

std::uint32_t read_u32(const std::vector<std::uint8_t>& bytes,
                       const x11::ByteOrder order, const std::size_t offset) {
  x11::ByteReader reader(std::span<const std::uint8_t>(bytes).subspan(offset),
                         order);
  std::uint32_t value = 0;
  require(reader.read_u32(value), "reply contains requested u32");
  return value;
}

WindowCreateSpec window(const std::uint32_t xid) {
  WindowCreateSpec spec;
  spec.xid = xid;
  spec.parent = 1;
  spec.width = 80;
  spec.height = 50;
  spec.window_class = WindowClass::InputOutput;
  return spec;
}

x11::FramedRequest set_owner(const x11::ByteOrder order,
                             const std::uint32_t owner,
                             const std::uint32_t selection,
                             const std::uint32_t time) {
  auto writer = header(order, x11::CoreOpcode::SetSelectionOwner, 4);
  writer.write_u32(owner);
  writer.write_u32(selection);
  writer.write_u32(time);
  return finish(std::move(writer), x11::CoreOpcode::SetSelectionOwner);
}

x11::FramedRequest convert(const x11::ByteOrder order,
                           const std::uint32_t requestor,
                           const std::uint32_t selection,
                           const std::uint32_t target,
                           const std::uint32_t property) {
  auto writer = header(order, x11::CoreOpcode::ConvertSelection, 6);
  writer.write_u32(requestor);
  writer.write_u32(selection);
  writer.write_u32(target);
  writer.write_u32(property);
  writer.write_u32(0);
  return finish(std::move(writer), x11::CoreOpcode::ConvertSelection);
}

}  // namespace

int main() {
  for (const auto order : {x11::ByteOrder::LittleEndian,
                           x11::ByteOrder::BigEndian}) {
    constexpr std::uint32_t first_base = 0x00400000;
    constexpr std::uint32_t second_base = 0x00600000;
    constexpr std::uint32_t mask = 0x001fffff;
    constexpr std::uint32_t primary = 1;
    ServerState state;
    const auto target = state.atoms().intern("UTF8_STRING", false).atom;
    const auto property = state.atoms().intern("GW_SELECTION", false).atom;
    const auto message_type = state.atoms().intern("GW_MESSAGE", false).atom;
    require(state.resources().create_window(10, first_base, mask,
                                            window(first_base + 1)) ==
                    CreateWindowStatus::Success &&
                state.resources().create_window(20, second_base, mask,
                                                window(second_base + 1)) ==
                    CreateWindowStatus::Success,
            "selection clients own valid requestor windows");
    DispatchContext first{10, first_base, mask, 1, order, false,
                          InputSnapshot{0, 0, 0, 1, 100}};
    DispatchContext second{20, second_base, mask, 2, order, false,
                           InputSnapshot{0, 0, 0, 1, 101}};

    auto result = dispatch_request(
        state, first, set_owner(order, first_base + 1, primary, 0));
    require(result.output.empty() && result.protocol_events.empty() &&
                state.selections().owner(primary)->window == first_base + 1,
            "SetSelectionOwner records CurrentTime ownership");
    result = dispatch_request(
        state, second, set_owner(order, second_base + 1, primary, 101));
    require(result.protocol_events.size() == 1 &&
                result.protocol_events[0].client == 10 &&
                std::get<x11::SelectionClearEvent>(
                    result.protocol_events[0].event)
                        .owner == first_base + 1,
            "owner replacement routes SelectionClear to prior client");

    auto get = header(order, x11::CoreOpcode::GetSelectionOwner, 2);
    get.write_u32(primary);
    result = dispatch_request(
        state, first,
        finish(std::move(get), x11::CoreOpcode::GetSelectionOwner));
    require(result.output.size() == 32 &&
                read_u32(result.output, order, 8) == second_base + 1,
            "GetSelectionOwner returns the current owner in either byte order");

    result = dispatch_request(
        state, first,
        convert(order, first_base + 1, primary, target, property));
    require(result.protocol_events.size() == 1 &&
                result.protocol_events[0].client == 20 &&
                std::get<x11::SelectionRequestEvent>(
                    result.protocol_events[0].event)
                        .requestor == first_base + 1,
            "ConvertSelection forwards SelectionRequest to owner");
    result = dispatch_request(
        state, first,
        convert(order, first_base + 1, property, target, property));
    require(result.protocol_events.size() == 1 &&
                result.protocol_events[0].client == 10 &&
                std::get<x11::SelectionNotifyEvent>(
                    result.protocol_events[0].event)
                        .property == 0,
            "ownerless conversion immediately returns property None");

    auto send = header(order, x11::CoreOpcode::SendEvent, 11);
    send.write_u32(first_base + 1);
    send.write_u32(0);
    send.write_u8(static_cast<std::uint8_t>(x11::CoreEventType::SelectionNotify));
    send.write_u8(0);
    send.write_u16(99);
    send.write_u32(102);
    send.write_u32(first_base + 1);
    send.write_u32(primary);
    send.write_u32(target);
    send.write_u32(property);
    send.write_padding(8);
    result = dispatch_request(
        state, second, finish(std::move(send), x11::CoreOpcode::SendEvent));
    require(result.protocol_events.size() == 1 &&
                result.protocol_events[0].delivery ==
                    ProtocolEventDelivery::WindowOwner &&
                std::get<x11::SelectionNotifyEvent>(
                    result.protocol_events[0].event)
                    .synthetic,
            "SendEvent accepts and marks a validated SelectionNotify synthetic");

    auto client_message = header(order, x11::CoreOpcode::SendEvent, 11, 1);
    client_message.write_u32(first_base + 1);
    client_message.write_u32(x11::event_mask::StructureNotify);
    client_message.write_u8(
        static_cast<std::uint8_t>(x11::CoreEventType::ClientMessage));
    client_message.write_u8(32);
    client_message.write_u16(0);
    client_message.write_u32(first_base + 1);
    client_message.write_u32(message_type);
    for (std::uint32_t value = 1; value <= 5; ++value)
      client_message.write_u32(value);
    result = dispatch_request(
        state, second,
        finish(std::move(client_message), x11::CoreOpcode::SendEvent, 1));
    require(result.protocol_events.size() == 1 &&
                result.protocol_events[0].propagate &&
                std::get<std::array<std::uint32_t, 5>>(
                    std::get<x11::ClientMessageEvent>(
                        result.protocol_events[0].event)
                        .data)[4] == 5,
            "SendEvent preserves byte-ordered ClientMessage32 payloads");

    auto withdraw = header(order, x11::CoreOpcode::SendEvent, 11);
    withdraw.write_u32(state.screen().root_window);
    withdraw.write_u32(x11::event_mask::SubstructureNotify |
                       x11::event_mask::SubstructureRedirect);
    withdraw.write_u8(
        static_cast<std::uint8_t>(x11::CoreEventType::UnmapNotify));
    withdraw.write_u8(0);
    withdraw.write_u16(0);
    withdraw.write_u32(state.screen().root_window);
    withdraw.write_u32(first_base + 1);
    withdraw.write_u8(0);
    withdraw.write_padding(19);
    result = dispatch_request(
        state, first,
        finish(std::move(withdraw), x11::CoreOpcode::SendEvent));
    require(result.protocol_events.size() == 1 &&
                result.protocol_events[0].window == state.screen().root_window &&
                result.protocol_events[0].mask ==
                    (x11::event_mask::SubstructureNotify |
                     x11::event_mask::SubstructureRedirect) &&
                std::get<x11::UnmapNotifyEvent>(
                    result.protocol_events[0].event)
                    .synthetic,
            "SendEvent accepts the validated ICCCM withdraw notification");

    auto change_property = header(order, x11::CoreOpcode::ChangeProperty, 7);
    change_property.write_u32(first_base + 1);
    change_property.write_u32(property);
    change_property.write_u32(target);
    change_property.write_u8(8);
    change_property.write_padding(3);
    change_property.write_u32(4);
    for (const std::uint8_t byte : {1, 2, 3, 4})
      change_property.write_u8(byte);
    result = dispatch_request(
        state, first,
        finish(std::move(change_property), x11::CoreOpcode::ChangeProperty));
    require(result.protocol_events.size() == 1 &&
                result.protocol_events[0].mask ==
                    x11::event_mask::PropertyChange &&
                std::get<x11::PropertyNotifyEvent>(
                    result.protocol_events[0].event)
                        .state == x11::PropertyNotifyState::NewValue,
            "ChangeProperty emits PropertyNotify NewValue intent");

    auto get_property = header(order, x11::CoreOpcode::GetProperty, 6, 1);
    get_property.write_u32(first_base + 1);
    get_property.write_u32(property);
    get_property.write_u32(0);
    get_property.write_u32(0);
    get_property.write_u32(1024);
    result = dispatch_request(
        state, first,
        finish(std::move(get_property), x11::CoreOpcode::GetProperty, 1));
    require(result.output.size() == 36 && result.protocol_events.size() == 1 &&
                std::get<x11::PropertyNotifyEvent>(
                    result.protocol_events[0].event)
                        .state == x11::PropertyNotifyState::Deleted,
            "GetProperty delete emits PropertyNotify only after a full read");

    auto replace_property = header(order, x11::CoreOpcode::ChangeProperty, 7);
    replace_property.write_u32(first_base + 1);
    replace_property.write_u32(property);
    replace_property.write_u32(target);
    replace_property.write_u8(8);
    replace_property.write_padding(3);
    replace_property.write_u32(1);
    replace_property.write_u8(9);
    replace_property.write_padding(3);
    (void)dispatch_request(
        state, first,
        finish(std::move(replace_property), x11::CoreOpcode::ChangeProperty));
    auto delete_property = header(order, x11::CoreOpcode::DeleteProperty, 3);
    delete_property.write_u32(first_base + 1);
    delete_property.write_u32(property);
    result = dispatch_request(
        state, first,
        finish(std::move(delete_property), x11::CoreOpcode::DeleteProperty));
    require(result.protocol_events.size() == 1 &&
                std::get<x11::PropertyNotifyEvent>(
                    result.protocol_events[0].event)
                        .state == x11::PropertyNotifyState::Deleted,
            "DeleteProperty emits PropertyNotify for an existing value");
    auto delete_missing = header(order, x11::CoreOpcode::DeleteProperty, 3);
    delete_missing.write_u32(first_base + 1);
    delete_missing.write_u32(property);
    result = dispatch_request(
        state, first,
        finish(std::move(delete_missing), x11::CoreOpcode::DeleteProperty));
    require(result.protocol_events.empty(),
            "DeleteProperty emits no event for a missing value");

    auto malformed = header(order, x11::CoreOpcode::SendEvent, 11);
    malformed.write_u32(first_base + 1);
    malformed.write_u32(0);
    malformed.write_u8(static_cast<std::uint8_t>(x11::CoreEventType::Expose));
    malformed.write_padding(31);
    result = dispatch_request(
        state, first,
        finish(std::move(malformed), x11::CoreOpcode::SendEvent));
    require(result.output.size() == 32 &&
                result.output[1] ==
                    static_cast<std::uint8_t>(x11::CoreErrorCode::BadValue),
            "SendEvent rejects unapproved structural event forgery");

    (void)state.cleanup_client(20);
    require(!state.selections().owner(primary),
            "client cleanup releases all selection ownership");
  }
}
