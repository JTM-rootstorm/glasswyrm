#include "helpers/test_support.hpp"
#include "helpers/x11_fake_client.hpp"
#include "helpers/x11_request_builder.hpp"
#include "integration/server_fixture.hpp"

#include <algorithm>
#include <cstdint>
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
  const auto unsupported = builder.raw(99, 0);
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

}  // namespace

int main(int argc, char** argv) {
  gw::test::require(argc == 2, "daemon path argument is required");
  gw::test::ServerProcess server(argv[1]);
  exercise_pipeline(server.socket_path());
  exercise_windows(server.socket_path(), x11::ByteOrder::LittleEndian);
  exercise_windows(server.socket_path(), x11::ByteOrder::BigEndian);
  exercise_atoms_and_properties(server.socket_path());
  return 0;
}
