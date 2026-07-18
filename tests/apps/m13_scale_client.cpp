#include "helpers/x11_fake_client.hpp"
#include "helpers/x11_request_builder.hpp"
#include "m13_scale_client_hold.hpp"
#include "m13_scale_client_options.hpp"
#include "protocol/x11/byte_cursor.hpp"
#include "protocol/x11/setup.hpp"

#include <array>
#include <algorithm>
#include <cstdint>
#include <exception>
#include <fstream>
#include <functional>
#include <iostream>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
namespace x11 = gw::protocol::x11;
using Options = gw::test::m13::ClientOptions;

constexpr std::uint16_t kWindowWidth = 320;
constexpr std::uint16_t kWindowHeight = 240;
constexpr std::uint32_t kClientScale = 2;
constexpr auto kUsage = gw::test::m13::kUsage;

void require(const bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}


std::vector<std::uint8_t> request(
    const x11::ByteOrder order, const std::uint8_t opcode,
    const std::uint8_t data,
    const std::function<void(x11::ByteWriter&)>& append = {}) {
  x11::ByteWriter writer(order);
  writer.write_u8(opcode);
  writer.write_u8(data);
  writer.write_u16(0);
  if (append) append(writer);
  while ((writer.size() & 3U) != 0) writer.write_u8(0);
  auto bytes = std::move(writer).take();
  require(bytes.size() / 4U <= UINT16_MAX, "request exceeds core X11 length");
  const auto units = static_cast<std::uint16_t>(bytes.size() / 4U);
  if (order == x11::ByteOrder::LittleEndian) {
    bytes[2] = static_cast<std::uint8_t>(units);
    bytes[3] = static_cast<std::uint8_t>(units >> 8U);
  } else {
    bytes[2] = static_cast<std::uint8_t>(units >> 8U);
    bytes[3] = static_cast<std::uint8_t>(units);
  }
  return bytes;
}

std::uint16_t u16(const std::vector<std::uint8_t>& bytes,
                  const std::size_t offset, const x11::ByteOrder order) {
  require(offset + 2 <= bytes.size(), "truncated CARD16");
  return gw::test::read_wire_u16(bytes.data() + offset, order);
}

std::uint32_t u32(const std::vector<std::uint8_t>& bytes,
                  const std::size_t offset, const x11::ByteOrder order) {
  require(offset + 4 <= bytes.size(), "truncated CARD32");
  return gw::test::read_wire_u32(bytes.data() + offset, order);
}

std::uint32_t setup_root(const std::vector<std::uint8_t>& reply,
                         const x11::ByteOrder order) {
  require(reply.size() >= 40 && reply[0] == 1 && reply[28] != 0,
          "invalid X11 setup success");
  const auto vendor_length = u16(reply, 24, order);
  const auto vendor_padded = (static_cast<std::size_t>(vendor_length) + 3U) &
                             ~std::size_t{3};
  const auto screen_offset = 40U + vendor_padded +
                             static_cast<std::size_t>(reply[29]) * 8U;
  require(screen_offset + 4U <= reply.size(),
          "truncated X11 setup screen record");
  return u32(reply, screen_offset, order);
}

struct WindowScale {
  std::uint32_t primary_output{};
  std::uint32_t preferred_numerator{1};
  std::uint32_t preferred_denominator{1};
  std::uint32_t accepted_scale{1};
  std::uint16_t mode{};
  std::uint64_t generation{};
  std::vector<std::uint32_t> memberships;
};

WindowScale parse_window_scale(const std::vector<std::uint8_t>& reply,
                               const x11::ByteOrder order,
                               const std::uint32_t window) {
  require(reply.size() >= 40 && reply[0] == 1 && u32(reply, 8, order) == window,
          "invalid GetWindowScale reply");
  WindowScale result;
  result.primary_output = u32(reply, 12, order);
  result.preferred_numerator = u32(reply, 16, order);
  result.preferred_denominator = u32(reply, 20, order);
  result.accepted_scale = u32(reply, 24, order);
  result.mode = u16(reply, 28, order);
  const auto count = u16(reply, 30, order);
  result.generation =
      (static_cast<std::uint64_t>(u32(reply, 32, order)) << 32U) |
      u32(reply, 36, order);
  require(reply.size() == 40U + static_cast<std::size_t>(count) * 4U,
          "GetWindowScale membership length mismatch");
  result.memberships.reserve(count);
  for (std::uint16_t index = 0; index < count; ++index)
    result.memberships.push_back(u32(reply, 40U + index * 4U, order));
  return result;
}

struct ScaleEvent {
  std::uint8_t reasons{};
  std::uint32_t window{};
  std::uint32_t primary_output{};
  std::uint32_t preferred_numerator{1};
  std::uint32_t preferred_denominator{1};
  std::uint32_t accepted_scale{1};
  std::uint64_t generation{};
};

ScaleEvent parse_scale_event(const std::vector<std::uint8_t>& packet,
                             const x11::ByteOrder order,
                             const std::uint8_t event_base) {
  require(packet.size() == 32 && (packet[0] & 0x7fU) == event_base,
          "invalid ScaleNotify event");
  return {packet[1],
          u32(packet, 4, order),
          u32(packet, 8, order),
          u32(packet, 12, order),
          u32(packet, 16, order),
          u32(packet, 20, order),
          (static_cast<std::uint64_t>(u32(packet, 24, order)) << 32U) |
              u32(packet, 28, order)};
}

struct OutputScale {
  std::uint64_t id{};
  std::uint32_t logical_width{}, logical_height{};
  std::uint32_t physical_width{}, physical_height{};
  std::uint32_t numerator{1}, denominator{1};
  std::uint16_t transform{};
};

OutputScale parse_output_scale(const std::vector<std::uint8_t>& reply,
                               const x11::ByteOrder order) {
  require(reply.size() == 60 && reply[0] == 1 && u32(reply, 4, order) == 7,
          "invalid GetOutputScale reply");
  return {(static_cast<std::uint64_t>(u32(reply, 8, order)) << 32U) |
              u32(reply, 12, order),
          u32(reply, 24, order), u32(reply, 28, order),
          u32(reply, 32, order), u32(reply, 36, order),
          u32(reply, 40, order), u32(reply, 44, order),
          u16(reply, 48, order)};
}

class Session {
 public:
  Session(const std::string& socket_path, const x11::ByteOrder order,
          const int timeout_ms)
      : client_(socket_path), order_(order), timeout_ms_(timeout_ms) {
    client_.send_all(gw::test::make_setup_request(order_));
    const auto setup = client_.receive_setup_reply(order_, timeout_ms_);
    require(setup.size() >= 40 && setup[0] == 1, "X11 setup failed");
    resource_base_ = u32(setup, 12, order_);
    root_ = setup_root(setup, order_);
  }

  std::uint32_t resource(const std::uint32_t offset) const {
    return resource_base_ + offset;
  }
  std::uint32_t root() const { return root_; }

  std::uint16_t send(const std::vector<std::uint8_t>& bytes) {
    client_.send_all(bytes);
    return ++sequence_;
  }

  std::vector<std::uint8_t> reply(const std::uint16_t sequence,
                                  const std::optional<std::uint8_t> event_base =
                                      std::nullopt) {
    for (;;) {
      auto packet = client_.receive_server_packet(order_, timeout_ms_);
      if (packet[0] == 1 || packet[0] == 0) {
        const auto received_sequence = u16(packet, 2, order_);
        if (received_sequence != sequence)
          throw std::runtime_error(
              "reply sequence mismatch: expected " +
              std::to_string(sequence) + ", received " +
              std::to_string(received_sequence) + ", packet type " +
              std::to_string(packet[0]) + ", detail " +
              std::to_string(packet[1]));
        require(packet[0] == 1, "server returned an X11 error");
        return packet;
      }
      if (event_base && (packet[0] & 0x7fU) == *event_base)
        pending_scale_events_.push_back(std::move(packet));
    }
  }

  void sync(const std::optional<std::uint8_t> event_base = std::nullopt) {
    const auto sequence = send(request(order_, 43, 0));
    (void)reply(sequence, event_base);
  }

  ScaleEvent scale_event(const std::uint8_t event_base) {
    if (!pending_scale_events_.empty()) {
      auto packet = std::move(pending_scale_events_.front());
      pending_scale_events_.erase(pending_scale_events_.begin());
      return parse_scale_event(packet, order_, event_base);
    }
    for (;;) {
      auto packet = client_.receive_server_packet(order_, timeout_ms_);
      if ((packet[0] & 0x7fU) == event_base)
        return parse_scale_event(packet, order_, event_base);
    }
  }

 private:
  gw::test::X11FakeClient client_;
  x11::ByteOrder order_;
  int timeout_ms_{};
  std::uint32_t resource_base_{};
  std::uint32_t root_{};
  std::uint16_t sequence_{};
  std::vector<std::vector<std::uint8_t>> pending_scale_events_;
};

std::vector<std::uint8_t> checker_pixels(const x11::ByteOrder order,
                                         const std::uint16_t start_y,
                                         const std::uint16_t rows) {
  x11::ByteWriter writer(order);
  for (std::uint16_t y = 0; y < rows; ++y) {
    for (std::uint16_t x = 0; x < kWindowWidth * kClientScale; ++x) {
      const auto absolute_y = static_cast<std::uint32_t>(start_y) + y;
      const bool checker = ((x / 4U) ^ (absolute_y / 4U)) & 1U;
      const bool line = x == absolute_y ||
                        x + absolute_y == kWindowWidth * kClientScale - 1U;
      const std::uint32_t rgb = line ? UINT32_C(0x00ff4040)
                                : checker ? UINT32_C(0x001020e0)
                                          : UINT32_C(0x00e0f020);
      writer.write_u32(rgb);
    }
  }
  return std::move(writer).take();
}

std::string memberships_json(const std::vector<std::uint32_t>& values) {
  std::string result{"["};
  for (std::size_t index = 0; index < values.size(); ++index) {
    if (index != 0) result += ',';
    result += std::to_string(values[index]);
  }
  result += ']';
  return result;
}

int self_test() {
  require(kUsage.find("--initial-hold-ms 1..60000 --initial-ready-file PATH") !=
                  std::string_view::npos &&
              kUsage.find("--hold-ms 1..60000 --ready-file PATH") !=
                  std::string_view::npos,
          "evidence hold options are missing from help");
  gw::test::m13::self_test_options();
  for (const auto order : {x11::ByteOrder::LittleEndian,
                           x11::ByteOrder::BigEndian}) {
    x11::SetupReplyConfig setup_config;
    setup_config.screen.root_window = UINT32_C(0x11223344);
    for (const bool game_compat : {false, true}) {
      setup_config.game_compat = game_compat;
      const auto setup = x11::encode_setup_success(order, setup_config);
      require(setup_root(setup, order) == setup_config.screen.root_window,
              "dynamic X11 setup root parsing mismatch");
    }
    const auto present = request(order, 135, 5, [](auto& writer) {
      writer.write_u32(0x11223344);
      writer.write_u32(0x55667788);
      writer.write_u32(0);
      writer.write_u32(9);
    });
    require(present.size() == 20 && present[0] == 135 && present[1] == 5 &&
                gw::test::read_wire_u16(present.data() + 2, order) == 5 &&
                gw::test::read_wire_u32(present.data() + 4, order) ==
                    0x11223344 &&
                gw::test::read_wire_u32(present.data() + 8, order) ==
                    0x55667788,
            "PresentScaledPixmap request golden mismatch");
    std::vector<std::uint8_t> event(32);
    event[0] = 69;
    event[1] = 3;
    x11::ByteWriter fields(order);
    fields.write_u32(0x11223344);
    fields.write_u32(0x101);
    fields.write_u32(5);
    fields.write_u32(4);
    fields.write_u32(2);
    fields.write_u32(0x01020304);
    fields.write_u32(0x05060708);
    const auto encoded = std::move(fields).take();
    std::copy(encoded.begin(), encoded.end(), event.begin() + 4);
    const auto parsed = parse_scale_event(event, order, 69);
    require(parsed.reasons == 3 && parsed.window == 0x11223344 &&
                parsed.primary_output == 0x101 &&
                parsed.preferred_numerator == 5 &&
                parsed.preferred_denominator == 4 &&
                parsed.accepted_scale == 2 &&
                parsed.generation == UINT64_C(0x0102030405060708),
            "ScaleNotify parser golden mismatch");
  }
  gw::test::m13::self_test_evidence_hold();
  return 0;
}

int run(const Options& options) {
  Session session(options.socket_dir + "/X" + options.display, options.order,
                  options.timeout_ms);

  const auto query_extension = request(options.order, 98, 0, [](auto& writer) {
    constexpr std::string_view name{"GW_SCALE"};
    writer.write_u16(name.size());
    writer.write_padding(2);
    writer.write_bytes(std::span<const std::uint8_t>(
        reinterpret_cast<const std::uint8_t*>(name.data()), name.size()));
  });
  const auto extension_sequence = session.send(query_extension);
  const auto extension = session.reply(extension_sequence);
  require(extension.size() == 32 && extension[8] == 1,
          "GW_SCALE is not present");
  const auto major_opcode = extension[9];
  const auto event_base = extension[10];

  const auto version_sequence = session.send(
      request(options.order, major_opcode, 0, [](auto& writer) {
        writer.write_u32(0);
        writer.write_u32(1);
      }));
  const auto version = session.reply(version_sequence, event_base);
  require(u32(version, 8, options.order) == 0 &&
              u32(version, 12, options.order) == 1,
          "GW_SCALE v0.1 negotiation failed");

  const auto window = session.resource(1);
  const auto pixmap = session.resource(2);
  const auto gc = session.resource(3);
  (void)session.send(request(options.order, 1, 24, [&](auto& writer) {
    writer.write_u32(window);
    writer.write_u32(session.root());
    writer.write_u16(40);
    writer.write_u16(40);
    writer.write_u16(kWindowWidth);
    writer.write_u16(kWindowHeight);
    writer.write_u16(0);
    writer.write_u16(1);
    writer.write_u32(3);
    writer.write_u32(0);
  }));
  (void)session.send(request(options.order, 8, 0,
                             [&](auto& writer) { writer.write_u32(window); }));
  session.sync(event_base);

  (void)session.send(request(options.order, major_opcode, 1,
                             [&](auto& writer) {
                               writer.write_u32(window);
                               writer.write_u32(7);
                             }));
  session.sync(event_base);

  const auto initial_window_sequence = session.send(
      request(options.order, major_opcode, 3,
              [&](auto& writer) { writer.write_u32(window); }));
  const auto initial = parse_window_scale(
      session.reply(initial_window_sequence, event_base), options.order,
      window);
  require(initial.primary_output != 0 && !initial.memberships.empty(),
          "new window has no output membership");

  const auto output_sequence = session.send(
      request(options.order, major_opcode, 2, [&](auto& writer) {
        writer.write_u32(initial.primary_output);
      }));
  const auto output = parse_output_scale(
      session.reply(output_sequence, event_base), options.order);

  const auto set_sequence = session.send(
      request(options.order, major_opcode, 4, [&](auto& writer) {
        writer.write_u32(window);
        writer.write_u32(kClientScale);
      }));
  const auto set_reply = session.reply(set_sequence, event_base);
  require(u32(set_reply, 8, options.order) == kClientScale,
          "server rejected client buffer scale 2");

  (void)session.send(request(options.order, 53, 24, [&](auto& writer) {
    writer.write_u32(pixmap);
    writer.write_u32(window);
    writer.write_u16(kWindowWidth * kClientScale);
    writer.write_u16(kWindowHeight * kClientScale);
  }));
  (void)session.send(request(options.order, 55, 0, [&](auto& writer) {
    writer.write_u32(gc);
    writer.write_u32(pixmap);
    writer.write_u32(0);
  }));
  constexpr std::uint16_t strip_rows = 48;
  for (std::uint16_t y = 0; y < kWindowHeight * kClientScale;
       y = static_cast<std::uint16_t>(y + strip_rows)) {
    const auto rows = static_cast<std::uint16_t>(
        std::min<std::uint32_t>(strip_rows,
                                kWindowHeight * kClientScale - y));
    const auto pixels = checker_pixels(options.order, y, rows);
    (void)session.send(request(options.order, 72, 2, [&](auto& writer) {
      writer.write_u32(pixmap);
      writer.write_u32(gc);
      writer.write_u16(kWindowWidth * kClientScale);
      writer.write_u16(rows);
      writer.write_u16(0);
      writer.write_u16(y);
      writer.write_u8(0);
      writer.write_u8(24);
      writer.write_padding(2);
      writer.write_bytes(pixels);
    }));
  }

  const std::uint32_t presentation_serial = 1;
  const auto present_sequence = session.send(
      request(options.order, major_opcode, 5, [&](auto& writer) {
        writer.write_u32(window);
        writer.write_u32(pixmap);
        writer.write_u32(0);
        writer.write_u32(presentation_serial);
      }));
  const auto present_reply = session.reply(present_sequence, event_base);
  require(u32(present_reply, 8, options.order) == presentation_serial &&
              u32(present_reply, 12, options.order) == kClientScale,
          "PresentScaledPixmap acknowledgement mismatch");

  const auto geometry_sequence = session.send(
      request(options.order, 14, 0,
              [&](auto& writer) { writer.write_u32(window); }));
  const auto geometry = session.reply(geometry_sequence, event_base);
  require(u16(geometry, 16, options.order) == kWindowWidth &&
              u16(geometry, 18, options.order) == kWindowHeight,
          "scaled presentation changed logical window geometry");
  if (options.initial_hold.valid())
    gw::test::m13::hold_for_evidence(
        options.initial_hold, window, gw::test::m13::EvidenceStage::Initial);

  (void)session.send(request(options.order, 12, 0, [&](auto& writer) {
    writer.write_u32(window);
    writer.write_u16(1);
    writer.write_padding(2);
    writer.write_u32(static_cast<std::uint32_t>(
        static_cast<std::int32_t>(options.move_x)));
  }));
  session.sync(event_base);
  const auto notification = session.scale_event(event_base);
  require(notification.window == window && (notification.reasons & 3U) != 0,
          "movement did not produce preferred-scale or membership notification");

  const auto moved_sequence = session.send(
      request(options.order, major_opcode, 3,
              [&](auto& writer) { writer.write_u32(window); }));
  const auto moved = parse_window_scale(
      session.reply(moved_sequence, event_base), options.order, window);
  require(moved.accepted_scale == kClientScale && moved.mode == 2,
          "scaled presentation did not survive output movement");
  const auto moved_geometry_sequence = session.send(
      request(options.order, 14, 0,
              [&](auto& writer) { writer.write_u32(window); }));
  const auto moved_geometry =
      session.reply(moved_geometry_sequence, event_base);
  require(u16(moved_geometry, 16, options.order) == kWindowWidth &&
              u16(moved_geometry, 18, options.order) == kWindowHeight,
          "movement changed logical scaled-window geometry");
  if (options.moved_hold.valid())
    gw::test::m13::hold_for_evidence(
        options.moved_hold, window, gw::test::m13::EvidenceStage::Moved);
  (void)session.send(request(options.order, major_opcode, 6,
                             [&](auto& writer) { writer.write_u32(window); }));
  session.sync(event_base);
  const auto reset_sequence = session.send(
      request(options.order, major_opcode, 3,
              [&](auto& writer) { writer.write_u32(window); }));
  const auto reset = parse_window_scale(
      session.reply(reset_sequence, event_base), options.order, window);
  require(reset.accepted_scale == 1 && reset.mode == 1,
          "ResetWindowBufferScale did not restore legacy mode");

  (void)session.send(request(options.order, 60, 0,
                             [&](auto& writer) { writer.write_u32(gc); }));
  (void)session.send(request(options.order, 54, 0,
                             [&](auto& writer) { writer.write_u32(pixmap); }));
  (void)session.send(request(options.order, 4, 0,
                             [&](auto& writer) { writer.write_u32(window); }));

  const std::string json =
      std::string{"{\"schema\":\"glasswyrm.m13-scale-client.v1\","}
      + "\"byte_order\":\"" +
      (options.order == x11::ByteOrder::LittleEndian ? "little" : "big") +
      "\",\"window\":" + std::to_string(window) +
      ",\"logical_geometry\":{\"width\":320,\"height\":240}," +
      "\"buffer_geometry\":{\"width\":640,\"height\":480}," +
      "\"initial\":{\"primary\":" +
      std::to_string(initial.primary_output) + ",\"preferred\":[" +
      std::to_string(initial.preferred_numerator) + ',' +
      std::to_string(initial.preferred_denominator) +
      "],\"memberships\":" + memberships_json(initial.memberships) + "}," +
      "\"output\":{\"id\":" + std::to_string(output.id) +
      ",\"logical\":[" + std::to_string(output.logical_width) + ',' +
      std::to_string(output.logical_height) + "],\"physical\":[" +
      std::to_string(output.physical_width) + ',' +
      std::to_string(output.physical_height) + "],\"scale\":[" +
      std::to_string(output.numerator) + ',' +
      std::to_string(output.denominator) + "]}," +
      "\"notification\":{\"reasons\":" +
      std::to_string(notification.reasons) + ",\"primary\":" +
      std::to_string(notification.primary_output) + "}," +
      "\"moved\":{\"primary\":" + std::to_string(moved.primary_output) +
      ",\"preferred\":[" + std::to_string(moved.preferred_numerator) + ',' +
      std::to_string(moved.preferred_denominator) +
      "],\"memberships\":" + memberships_json(moved.memberships) + "}," +
      "\"present_serial\":1,\"reset_scale\":1}\n";
  if (options.result_path.empty()) {
    std::cout << json;
  } else {
    std::ofstream output_file(options.result_path,
                              std::ios::binary | std::ios::trunc);
    require(output_file.good(), "could not open result JSON path");
    output_file << json;
    require(output_file.good(), "could not write result JSON");
  }
  return 0;
}

}  // namespace

int main(const int argc, char** argv) try {
  Options options;
  if (!gw::test::m13::parse_options(argc, argv, options)) {
    std::cerr << kUsage;
    return 2;
  }
  if (options.help) {
    std::cout << kUsage;
    return 0;
  }
  if (options.self_test) return self_test();
  return run(options);
} catch (const std::exception& error) {
  std::cerr << "m13_scale_client: " << error.what() << '\n';
  return 1;
}
