#pragma once

#include "m13_scale_client_hold.hpp"
#include "protocol/x11/byte_cursor.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace gw::test::m13 {

inline constexpr std::string_view kUsage =
    "Usage: m13_scale_client --display :N [--socket-dir DIR] "
    "[--byte-order little|big] [--move-x X] [--timeout-ms MS] "
    "[--result PATH] [--initial-hold-ms 1..60000 --initial-ready-file PATH] "
    "[--hold-ms 1..60000 --ready-file PATH]\n";

struct ClientOptions {
  std::string socket_dir{"/tmp/.X11-unix"};
  std::string display;
  std::string result_path;
  std::int16_t move_x{700};
  int timeout_ms{5000};
  EvidenceHold initial_hold;
  EvidenceHold moved_hold;
  protocol::x11::ByteOrder order{protocol::x11::ByteOrder::LittleEndian};
  bool self_test{};
  bool help{};
};

[[nodiscard]] bool parse_options(int argc, char** argv,
                                 ClientOptions& options);
void self_test_options();

}  // namespace gw::test::m13
