#include "helpers/test_support.hpp"
#include "helpers/x11_fake_client.hpp"
#include "helpers/x11_request_builder.hpp"
#include "integration/server_fixture.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <exception>
#include <iostream>
#include <string>
#include <sys/socket.h>
#include <vector>

namespace {
namespace x11 = gw::protocol::x11;

struct Session {
  gw::test::X11FakeClient client;
  gw::test::X11RequestBuilder requests;
  x11::ByteOrder order;
  std::uint32_t resource_base;

  Session(const std::string& socket, x11::ByteOrder byte_order)
      : client(socket), requests(byte_order), order(byte_order), resource_base(0) {
    client.send_all(gw::test::make_setup_request(order));
    const auto setup = client.receive_setup_reply(order);
    gw::test::require(setup[0] == 1, "session setup succeeds");
    resource_base = gw::test::read_wire_u32(setup.data() + 12, order);
  }

  void sync() {
    client.send_all(requests.get_input_focus());
    const auto reply = client.receive_server_packet(order);
    gw::test::require(reply[0] == 1, "focus synchronization succeeds");
  }
};

std::uint32_t intern(Session& session, const std::string& name) {
  session.client.send_all(session.requests.intern_atom(name));
  const auto reply = session.client.receive_server_packet(session.order);
  gw::test::require(reply[0] == 1, "InternAtom returns a reply");
  return gw::test::read_wire_u32(reply.data() + 8, session.order);
}

void exercise_windows(const std::string& socket, x11::ByteOrder order) {
  Session session(socket, order);
  const std::uint32_t window = session.resource_base + 1;
  session.client.send_all(
      session.requests.create_window(window, 1, -5, 7, 320, 200));
  session.sync();

  session.client.send_all(session.requests.get_geometry(window), 1);
  const auto geometry = session.client.receive_server_packet(order);
  gw::test::require(
      geometry[0] == 1 && geometry[1] == 24 &&
          gw::test::read_wire_u32(geometry.data() + 8, order) == 1 &&
          gw::test::read_wire_u16(geometry.data() + 16, order) == 320 &&
          gw::test::read_wire_u16(geometry.data() + 18, order) == 200,
      "GetGeometry returns stored geometry");

  session.client.send_all(session.requests.query_tree(1));
  const auto tree = session.client.receive_server_packet(order);
  gw::test::require(
      tree[0] == 1 && gw::test::read_wire_u16(tree.data() + 16, order) == 1 &&
          gw::test::read_wire_u32(tree.data() + 32, order) == window,
      "QueryTree lists the created child");

  session.client.send_all(
      session.requests.create_window(window, 1, 0, 0, 10, 10));
  const auto duplicate = session.client.receive_server_packet(order);
  gw::test::require(duplicate[0] == 0 && duplicate[1] == 14,
                    "duplicate XID returns BadIDChoice");

  const std::array<std::uint32_t, 1> invalid_event{0x80000000U};
  session.client.send_all(session.requests.create_window(
      window + 1, 1, 0, 0, 10, 10, 1U << 11U, invalid_event));
  const auto bad_event = session.client.receive_server_packet(order);
  gw::test::require(bad_event[0] == 0 && bad_event[1] == 2,
                    "invalid event mask returns BadValue");

  const std::array<std::uint32_t, 1> invalid_propagation{1U << 24U};
  session.client.send_all(session.requests.create_window(
      window + 1, 1, 0, 0, 10, 10, 1U << 12U, invalid_propagation));
  const auto bad_propagation = session.client.receive_server_packet(order);
  gw::test::require(bad_propagation[0] == 0 && bad_propagation[1] == 2,
                    "invalid do-not-propagate mask returns BadValue");

  const std::array<std::uint32_t, 1> background_pixel{0};
  session.client.send_all(session.requests.create_window(
      window + 1, 1, 0, 0, 10, 10, 1U << 1U, background_pixel, 0, 2, 0,
      0));
  const auto bad_input_only = session.client.receive_server_packet(order);
  gw::test::require(bad_input_only[0] == 0 && bad_input_only[1] == 8,
                    "forbidden InputOnly attribute returns BadMatch");
  session.sync();

  session.client.send_all(session.requests.destroy_window(1));
  session.sync();
  session.client.send_all(session.requests.query_tree(1));
  const auto root_unchanged = session.client.receive_server_packet(order);
  gw::test::require(
      gw::test::read_wire_u16(root_unchanged.data() + 16, order) == 1,
      "DestroyWindow on root is a no-op");

  session.client.send_all(session.requests.destroy_window(window));
  session.sync();
  session.client.send_all(session.requests.get_geometry(window));
  const auto missing = session.client.receive_server_packet(order);
  gw::test::require(missing[0] == 0 && missing[1] == 9,
                    "destroyed window returns BadDrawable");
}

void require_error(const std::vector<std::uint8_t>& packet,
                   std::uint8_t code, const char* message) {
  gw::test::require(packet.size() == 32 && packet[0] == 0 && packet[1] == code,
                    message);
}

void exercise_window_attributes(const std::string& socket,
                                x11::ByteOrder order) {
  constexpr std::uint32_t kAllAttributes = 0x7fffU;
  constexpr std::uint32_t kButtonPress = 1U << 2U;
  constexpr std::uint32_t kExposure = 1U << 15U;
  Session owner(socket, order);
  Session observer(socket, order);
  const std::uint32_t window = owner.resource_base + 20;
  const std::uint32_t create_window = owner.resource_base + 21;
  const std::array<std::uint32_t, 15> all_values{
      1, 0x10203040U, 0, 0x50607080U, 10, 10, 2, 0xa5a5a5a5U,
      0x12345678U, 1, 1, kButtonPress, 0x204fU, 0, 0};

  owner.client.send_all(owner.requests.create_window(
      create_window, 1, 0, 0, 20, 20, kAllAttributes, all_values));
  owner.sync();
  owner.client.send_all(owner.requests.get_window_attributes(create_window));
  const auto created = owner.client.receive_server_packet(order);
  gw::test::require(
      created[0] == 1 && created[1] == 2 && created[14] == 10 &&
          created[15] == 10 && created[24] == 1 && created[27] == 1 &&
          gw::test::read_wire_u32(created.data() + 16, order) == 0xa5a5a5a5U &&
          gw::test::read_wire_u32(created.data() + 20, order) == 0x12345678U &&
          gw::test::read_wire_u32(created.data() + 32, order) == kButtonPress &&
          gw::test::read_wire_u32(created.data() + 36, order) == kButtonPress &&
          gw::test::read_wire_u16(created.data() + 40, order) == 0x204fU,
      "CreateWindow decodes every attribute bit");

  owner.client.send_all(owner.requests.create_window(window, 1, 0, 0, 20, 20));
  owner.sync();
  auto changed_values = all_values;
  changed_values[11] = kExposure;
  owner.client.send_all(owner.requests.change_window_attributes(
      window, kAllAttributes, changed_values));
  owner.sync();
  owner.client.send_all(owner.requests.get_window_attributes(window));
  const auto changed = owner.client.receive_server_packet(order);
  gw::test::require(
      changed[0] == 1 && changed[1] == 2 && changed[14] == 10 &&
          changed[15] == 10 && changed[24] == 1 && changed[27] == 1 &&
          gw::test::read_wire_u32(changed.data() + 32, order) == kExposure &&
          gw::test::read_wire_u32(changed.data() + 36, order) == kExposure &&
          gw::test::read_wire_u16(changed.data() + 40, order) == 0x204fU,
      "ChangeWindowAttributes decodes every attribute bit");

  const std::array<std::uint32_t, 2> combined{0, kButtonPress};
  owner.client.send_all(owner.requests.change_window_attributes(
      window, (1U << 9U) | (1U << 11U), combined));
  owner.sync();
  owner.client.send_all(owner.requests.get_window_attributes(window));
  const auto combined_reply = owner.client.receive_server_packet(order);
  gw::test::require(
      combined_reply[27] == 0 &&
          gw::test::read_wire_u32(combined_reply.data() + 36, order) ==
              kButtonPress,
      "combined override and event selection update succeeds");

  const std::array<std::uint32_t, 1> button{kButtonPress};
  observer.client.send_all(observer.requests.change_window_attributes(
      window, 1U << 11U, button));
  require_error(observer.client.receive_server_packet(order), 10,
                "ButtonPress selection is exclusive across clients");

  for (const auto redirect : {1U << 18U, 1U << 20U}) {
    const std::array<std::uint32_t, 1> selected{redirect};
    owner.client.send_all(owner.requests.change_window_attributes(
        window, 1U << 11U, selected));
    require_error(owner.client.receive_server_packet(order), 10,
                  "redirect selections return BadAccess");
  }

  const std::array<std::uint32_t, 2> invalid_atomic{0xdeadbeefU, 2};
  owner.client.send_all(owner.requests.change_window_attributes(
      window, (1U << 1U) | (1U << 9U), invalid_atomic));
  require_error(owner.client.receive_server_packet(order), 2,
                "invalid combined attribute returns BadValue");
  owner.client.send_all(owner.requests.get_window_attributes(window));
  const auto unchanged = owner.client.receive_server_packet(order);
  gw::test::require(
      unchanged[27] == 0 &&
          gw::test::read_wire_u32(unchanged.data() + 36, order) == kButtonPress,
      "failed attribute update does not mutate observable state");

  const std::array<std::uint32_t, 1> zero{0};
  owner.client.send_all(owner.requests.change_window_attributes(
      window, 1U << 11U, zero));
  owner.sync();
  observer.client.send_all(observer.requests.change_window_attributes(
      window, 1U << 11U, button));
  observer.sync();
  observer.client.send_all(observer.requests.get_window_attributes(window));
  const auto transferred = observer.client.receive_server_packet(order);
  gw::test::require(
      gw::test::read_wire_u32(transferred.data() + 32, order) == kButtonPress &&
          gw::test::read_wire_u32(transferred.data() + 36, order) == kButtonPress,
      "zero mask removes selection and permits a new owner");

  const std::array<std::uint32_t, 1> root_override{1};
  owner.client.send_all(owner.requests.change_window_attributes(
      1, 1U << 9U, root_override));
  require_error(owner.client.receive_server_packet(order), 8,
                "root override redirect returns BadMatch");

  const std::array<std::uint32_t, 1> unknown_value{0};
  owner.client.send_all(owner.requests.change_window_attributes(
      window, 1U << 15U, unknown_value));
  require_error(owner.client.receive_server_packet(order), 2,
                "unknown attribute mask returns BadValue");

  auto short_request = owner.requests.change_window_attributes(
      window, 1U << 1U, unknown_value);
  short_request.resize(12);
  if (order == x11::ByteOrder::LittleEndian) {
    short_request[2] = 3;
    short_request[3] = 0;
  } else {
    short_request[2] = 0;
    short_request[3] = 3;
  }
  owner.client.send_all(short_request);
  require_error(owner.client.receive_server_packet(order), 16,
                "mask/value length mismatch returns BadLength");
}

void exercise_atoms_and_properties(const std::string& socket) {
  Session little(socket, x11::ByteOrder::LittleEndian);
  Session big(socket, x11::ByteOrder::BigEndian);
  const std::uint32_t window = little.resource_base + 7;
  little.client.send_all(
      little.requests.create_window(window, 1, 0, 0, 64, 64));
  little.sync();

  const std::uint32_t property = intern(little, "GW_M2_PROPERTY");
  big.client.send_all(big.requests.intern_atom("GW_M2_PROPERTY", true));
  const auto shared_atom = big.client.receive_server_packet(big.order);
  gw::test::require(
      gw::test::read_wire_u32(shared_atom.data() + 8, big.order) == property,
      "dynamic atoms are shared across clients");

  big.client.send_all(big.requests.get_atom_name(property));
  const auto atom_name = big.client.receive_server_packet(big.order);
  const std::string decoded_name(atom_name.begin() + 32, atom_name.end());
  gw::test::require(
      gw::test::read_wire_u16(atom_name.data() + 8, big.order) == 14 &&
          decoded_name.substr(0, 14) == "GW_M2_PROPERTY",
      "GetAtomName preserves exact spelling");

  const std::vector<std::uint8_t> words_little{0x34, 0x12, 0xcd, 0xab};
  little.client.send_all(little.requests.change_property(
      0, window, property, 19, 16, words_little, 2));
  little.sync();
  big.client.send_all(big.requests.get_property(window, property, 19, 0, 16));
  const auto words_big = big.client.receive_server_packet(big.order);
  gw::test::require(words_big[1] == 16 && words_big.size() == 36 &&
                        words_big[32] == 0x12 && words_big[33] == 0x34 &&
                        words_big[34] == 0xab && words_big[35] == 0xcd,
                    "format-16 property is re-encoded for big endian");

  const std::vector<std::uint8_t> dwords_big{0x11, 0x22, 0x33, 0x44,
                                             0xaa, 0xbb, 0xcc, 0xdd};
  big.client.send_all(big.requests.change_property(
      0, window, property, 6, 32, dwords_big, 2));
  big.sync();
  little.client.send_all(
      little.requests.get_property(window, property, 6, 0, 16));
  const auto dwords_little = little.client.receive_server_packet(little.order);
  gw::test::require(
      dwords_little[1] == 32 && dwords_little.size() == 40 &&
          dwords_little[32] == 0x44 && dwords_little[33] == 0x33 &&
          dwords_little[34] == 0x22 && dwords_little[35] == 0x11 &&
          dwords_little[36] == 0xdd && dwords_little[39] == 0xaa,
      "format-32 property is re-encoded for little endian");

  little.client.send_all(little.requests.list_properties(window));
  const auto listed = little.client.receive_server_packet(little.order);
  gw::test::require(
      gw::test::read_wire_u16(listed.data() + 8, little.order) == 1 &&
          gw::test::read_wire_u32(listed.data() + 32, little.order) == property,
      "ListProperties returns the property atom");

  little.client.send_all(little.requests.delete_property(window, property));
  little.sync();
  little.client.send_all(
      little.requests.get_property(window, property, 0, 0, 16));
  const auto absent = little.client.receive_server_packet(little.order);
  gw::test::require(absent[1] == 0 &&
                        gw::test::read_wire_u32(absent.data() + 8,
                                                little.order) == 0,
                    "deleted property is absent");
}

void exercise_pipeline(const std::string& socket) {
  gw::test::X11FakeClient client(socket);
  const gw::test::X11RequestBuilder builder(x11::ByteOrder::LittleEndian);
  auto bytes = gw::test::make_setup_request(x11::ByteOrder::LittleEndian);
  const auto unsupported = builder.raw(126, 0);
  const auto focus = builder.get_input_focus();
  bytes.insert(bytes.end(), unsupported.begin(), unsupported.end());
  bytes.insert(bytes.end(), focus.begin(), focus.end());
  client.send_all(bytes, 1);
  gw::test::require(::shutdown(client.descriptor(), SHUT_WR) == 0,
                    "pipeline client half-closes its request stream");
  gw::test::require(
      client.receive_setup_reply(x11::ByteOrder::LittleEndian)[0] == 1,
      "setup remains first for pipelined input");
  const auto protocol_error =
      client.receive_server_packet(x11::ByteOrder::LittleEndian);
  const auto focus_reply =
      client.receive_server_packet(x11::ByteOrder::LittleEndian);
  gw::test::require(protocol_error[0] == 0 && protocol_error[1] == 1 &&
                        focus_reply[0] == 1,
                    "pipelined error and reply preserve order");
}

void exercise_output_cap_isolation(const std::string& socket) {
  Session abusive(socket, x11::ByteOrder::LittleEndian);
  const std::string large_name(65000, 'A');
  const auto atom = intern(abusive, large_name);
  const auto request = abusive.requests.get_atom_name(atom);
  std::vector<std::uint8_t> pipeline;
  for (int index = 0; index < 17; ++index) {
    pipeline.insert(pipeline.end(), request.begin(), request.end());
  }
  abusive.client.send_all(pipeline);
  gw::test::require(abusive.client.peer_closed(),
                    "non-reading client is closed at output cap");

  Session healthy(socket, x11::ByteOrder::BigEndian);
  healthy.sync();
}

}  // namespace

int main(int argc, char** argv) {
  gw::test::require(argc == 2, "daemon path argument is required");
  gw::test::ServerProcess server(argv[1]);
  try {
    exercise_pipeline(server.socket_path());
    exercise_windows(server.socket_path(), x11::ByteOrder::LittleEndian);
    exercise_windows(server.socket_path(), x11::ByteOrder::BigEndian);
    exercise_window_attributes(server.socket_path(),
                               x11::ByteOrder::LittleEndian);
    exercise_window_attributes(server.socket_path(),
                               x11::ByteOrder::BigEndian);
    exercise_atoms_and_properties(server.socket_path());
    exercise_output_cap_isolation(server.socket_path());
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "core-test: " << error.what() << "\nserver log:\n"
              << server.log_contents();
    return 1;
  }
}
