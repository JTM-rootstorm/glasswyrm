#include "helpers/test_support.hpp"
#include "helpers/x11_fake_client.hpp"
#include "helpers/x11_request_builder.hpp"
#include "integration/server_fixture.hpp"
#include "protocol/x11/byte_cursor.hpp"
#include "protocol/x11/core.hpp"
#include "protocol/x11/event_mask.hpp"

#include <array>
#include <cstdint>
#include <string>

namespace {
namespace x11 = gw::protocol::x11;

struct Session {
  gw::test::X11FakeClient client;
  gw::test::X11RequestBuilder requests;
  x11::ByteOrder order;
  std::uint32_t resource_base{0};

  Session(const std::string& socket, const x11::ByteOrder byte_order)
      : client(socket), requests(byte_order), order(byte_order) {
    client.send_all(gw::test::make_setup_request(order));
    const auto setup = client.receive_setup_reply(order);
    gw::test::require(setup[0] == 1, "selection session setup succeeds");
    resource_base = gw::test::read_wire_u32(setup.data() + 12, order);
  }

  void sync() {
    client.send_all(requests.get_input_focus());
    const auto reply = client.receive_server_packet(order);
    gw::test::require(reply[0] == 1, "selection session synchronization succeeds");
  }
};

std::uint32_t intern(Session& session, const std::string& name) {
  session.client.send_all(session.requests.intern_atom(name));
  const auto reply = session.client.receive_server_packet(session.order);
  gw::test::require(reply[0] == 1, "selection atom intern succeeds");
  return gw::test::read_wire_u32(reply.data() + 8, session.order);
}

std::vector<std::uint8_t> set_owner(Session& session,
                                    const std::uint32_t owner,
                                    const std::uint32_t selection) {
  x11::ByteWriter body(session.order);
  body.write_u32(owner);
  body.write_u32(selection);
  body.write_u32(0);
  return session.requests.raw(22, 0, std::move(body).take());
}

std::vector<std::uint8_t> get_owner(Session& session,
                                    const std::uint32_t selection) {
  x11::ByteWriter body(session.order);
  body.write_u32(selection);
  return session.requests.raw(23, 0, std::move(body).take());
}

std::vector<std::uint8_t> convert(Session& session,
                                  const std::uint32_t requestor,
                                  const std::uint32_t selection,
                                  const std::uint32_t target,
                                  const std::uint32_t property) {
  x11::ByteWriter body(session.order);
  body.write_u32(requestor);
  body.write_u32(selection);
  body.write_u32(target);
  body.write_u32(property);
  body.write_u32(0);
  return session.requests.raw(24, 0, std::move(body).take());
}

std::vector<std::uint8_t> selection_notify(
    Session& session, const std::uint32_t requestor,
    const std::uint32_t selection, const std::uint32_t target,
    const std::uint32_t property) {
  x11::ByteWriter body(session.order);
  body.write_u32(requestor);
  body.write_u32(0);
  body.write_u8(static_cast<std::uint8_t>(x11::CoreEventType::SelectionNotify));
  body.write_u8(0);
  body.write_u16(0);
  body.write_u32(0);
  body.write_u32(requestor);
  body.write_u32(selection);
  body.write_u32(target);
  body.write_u32(property);
  body.write_padding(8);
  return session.requests.raw(25, 0, std::move(body).take());
}

std::vector<std::uint8_t> client_message(
    Session& session, const std::uint32_t destination,
    const std::uint32_t message_type) {
  x11::ByteWriter body(session.order);
  body.write_u32(destination);
  body.write_u32(x11::event_mask::StructureNotify);
  body.write_u8(static_cast<std::uint8_t>(x11::CoreEventType::ClientMessage));
  body.write_u8(32);
  body.write_u16(0);
  body.write_u32(destination);
  body.write_u32(message_type);
  for (std::uint32_t value = 0x10203040; value < 0x10203045; ++value)
    body.write_u32(value);
  return session.requests.raw(25, 0, std::move(body).take());
}

void require_event(const std::vector<std::uint8_t>& packet,
                   const x11::CoreEventType type, const char* message) {
  gw::test::require(packet.size() == 32 &&
                        (packet[0] & 0x7fU) ==
                            static_cast<std::uint8_t>(type),
                    message);
}

}  // namespace

int main(int argc, char** argv) {
  gw::test::require(argc == 2, "glasswyrmd path is provided");
  gw::test::ServerProcess server(argv[1]);
  Session owner(server.socket_path(), x11::ByteOrder::LittleEndian);
  Session requestor(server.socket_path(), x11::ByteOrder::BigEndian);
  const auto owner_window = owner.resource_base + 1;
  const auto requestor_window = requestor.resource_base + 1;
  owner.client.send_all(
      owner.requests.create_window(owner_window, 1, 0, 0, 64, 64));
  requestor.client.send_all(
      requestor.requests.create_window(requestor_window, 1, 0, 0, 64, 64));
  owner.sync();
  requestor.sync();

  constexpr std::uint32_t primary = 1;
  const auto target = intern(owner, "UTF8_STRING");
  const auto property = intern(requestor, "GW_SELECTION_DATA");
  const auto message_type = intern(owner, "GW_CLIENT_MESSAGE");
  owner.client.send_all(set_owner(owner, owner_window, primary));
  owner.sync();
  requestor.client.send_all(get_owner(requestor, primary));
  auto packet = requestor.client.receive_server_packet(requestor.order);
  gw::test::require(packet[0] == 1 &&
                        gw::test::read_wire_u32(packet.data() + 8,
                                                requestor.order) == owner_window,
                    "GetSelectionOwner observes cross-client ownership");

  requestor.client.send_all(
      convert(requestor, requestor_window, primary, target, property));
  packet = owner.client.receive_server_packet(owner.order);
  require_event(packet, x11::CoreEventType::SelectionRequest,
                "ConvertSelection reaches the owner");
  gw::test::require(
      gw::test::read_wire_u32(packet.data() + 8, owner.order) == owner_window &&
          gw::test::read_wire_u32(packet.data() + 12, owner.order) ==
              requestor_window &&
          gw::test::read_wire_u32(packet.data() + 24, owner.order) == property,
      "SelectionRequest preserves the conversion fields");

  const std::array<std::uint32_t, 1> property_mask{
      x11::event_mask::PropertyChange};
  requestor.client.send_all(requestor.requests.change_window_attributes(
      requestor_window, 1U << 11U, property_mask));
  requestor.sync();
  const std::array<std::uint8_t, 4> contents{0x47, 0x57, 0x4d, 0x31};
  owner.client.send_all(owner.requests.change_property(
      0, requestor_window, property, target, 8, contents, contents.size()));
  packet = requestor.client.receive_server_packet(requestor.order);
  require_event(packet, x11::CoreEventType::PropertyNotify,
                "selection property write emits PropertyNotify");
  gw::test::require(
      gw::test::read_wire_u32(packet.data() + 4, requestor.order) ==
              requestor_window &&
          packet[16] == 0,
      "PropertyNotify reports NewValue in recipient byte order");

  owner.client.send_all(selection_notify(owner, requestor_window, primary,
                                         target, property));
  packet = requestor.client.receive_server_packet(requestor.order);
  require_event(packet, x11::CoreEventType::SelectionNotify,
                "owner SendEvent reaches requestor");
  gw::test::require((packet[0] & 0x80U) != 0 &&
                        gw::test::read_wire_u32(packet.data() + 20,
                                                requestor.order) == property,
                    "SelectionNotify is synthetic and byte-order correct");

  requestor.client.send_all(requestor.requests.get_property(
      requestor_window, property, 0, 0, 16, true));
  packet = requestor.client.receive_server_packet(requestor.order);
  gw::test::require(packet[0] == 1 && packet.size() == 36,
                    "requestor reads the transferred property");
  packet = requestor.client.receive_server_packet(requestor.order);
  require_event(packet, x11::CoreEventType::PropertyNotify,
                "property read deletion emits PropertyNotify");
  gw::test::require(packet[16] == 1,
                    "PropertyNotify reports deletion after a full read");

  const std::array<std::uint32_t, 1> structure_mask{
      x11::event_mask::StructureNotify};
  owner.client.send_all(owner.requests.change_window_attributes(
      owner_window, 1U << 11U, structure_mask));
  owner.sync();
  requestor.client.send_all(
      client_message(requestor, owner_window, message_type));
  packet = owner.client.receive_server_packet(owner.order);
  require_event(packet, x11::CoreEventType::ClientMessage,
                "masked ClientMessage reaches the selecting client");
  gw::test::require(
      (packet[0] & 0x80U) != 0 && packet[1] == 32 &&
          gw::test::read_wire_u32(packet.data() + 8, owner.order) ==
              message_type &&
          gw::test::read_wire_u32(packet.data() + 28, owner.order) ==
              0x10203044,
      "ClientMessage32 is synthetic and converted to recipient byte order");

  requestor.client.send_all(set_owner(requestor, requestor_window, primary));
  requestor.sync();
  packet = owner.client.receive_server_packet(owner.order);
  require_event(packet, x11::CoreEventType::SelectionClear,
                "selection replacement clears the previous owner");

  requestor.client.send_all(requestor.requests.destroy_window(requestor_window));
  requestor.sync();
  owner.client.send_all(get_owner(owner, primary));
  packet = owner.client.receive_server_packet(owner.order);
  gw::test::require(packet[0] == 1 &&
                        gw::test::read_wire_u32(packet.data() + 8,
                                                owner.order) == 0,
                    "destroying the owner window clears ownership");

  owner.client.send_all(
      convert(owner, owner_window, primary, target, property));
  packet = owner.client.receive_server_packet(owner.order);
  require_event(packet, x11::CoreEventType::SelectionNotify,
                "conversion without owner immediately notifies requestor");
  gw::test::require(gw::test::read_wire_u32(packet.data() + 20, owner.order) == 0,
                    "ownerless SelectionNotify uses property None");
}
