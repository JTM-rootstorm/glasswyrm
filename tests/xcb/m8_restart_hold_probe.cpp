#include "helpers/synthetic_input_client.hpp"

#include <xcb/xcb.h>

#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

namespace {
struct FreeDeleter { void operator()(void* value) const { std::free(value); } };
template <class T> using XPtr = std::unique_ptr<T, FreeDeleter>;
void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
void checked(xcb_connection_t* connection, xcb_void_cookie_t cookie,
             const char* message) {
  XPtr<xcb_generic_error_t> error(xcb_request_check(connection, cookie));
  require(error == nullptr, message);
}
struct XClient {
  xcb_connection_t* connection{};
  xcb_screen_t* screen{};
  xcb_window_t window{};
  xcb_gcontext_t gc{};
  ~XClient() { if (connection != nullptr) xcb_disconnect(connection); }
};
void connect(XClient& client) {
  int index = 0; client.connection = xcb_connect(nullptr, &index);
  require(client.connection != nullptr &&
              xcb_connection_has_error(client.connection) == 0,
          "XCB connection failed");
  auto screens = xcb_setup_roots_iterator(xcb_get_setup(client.connection));
  while (index-- > 0) xcb_screen_next(&screens);
  require(screens.rem != 0, "XCB screen missing"); client.screen = screens.data;
}
void create(XClient& client, std::uint32_t color) {
  client.window = xcb_generate_id(client.connection);
  client.gc = xcb_generate_id(client.connection);
  const std::uint32_t values[] = {
      color, XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_EXPOSURE |
                 XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE |
                 XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE |
                 XCB_EVENT_MASK_FOCUS_CHANGE};
  checked(client.connection,
          xcb_create_window_checked(client.connection, XCB_COPY_FROM_PARENT,
              client.window, client.screen->root, 0, 0, 300, 180, 0,
              XCB_WINDOW_CLASS_INPUT_OUTPUT, client.screen->root_visual,
              XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK, values),
          "CreateWindow failed");
  checked(client.connection,
          xcb_create_gc_checked(client.connection, client.gc, client.window,
                                XCB_GC_FOREGROUND, &color), "CreateGC failed");
  checked(client.connection, xcb_map_window_checked(client.connection,
                                                     client.window),
          "MapWindow failed");
  xcb_flush(client.connection);
  XPtr<xcb_get_input_focus_reply_t> sync(xcb_get_input_focus_reply(
      client.connection, xcb_get_input_focus(client.connection), nullptr));
  require(sync != nullptr, "initial XCB sync failed");
  while (auto* event = xcb_poll_for_event(client.connection)) std::free(event);
}
struct Geometry { std::int16_t x{}, y{}; };
Geometry geometry(XClient& client) {
  XPtr<xcb_get_geometry_reply_t> reply(xcb_get_geometry_reply(
      client.connection, xcb_get_geometry(client.connection, client.window),
      nullptr));
  require(reply != nullptr, "GetGeometry failed"); return {reply->x, reply->y};
}
void paint(XClient& client, std::uint32_t color, std::int16_t x) {
  checked(client.connection, xcb_change_gc_checked(
      client.connection, client.gc, XCB_GC_FOREGROUND, &color),
      "ChangeGC failed");
  const xcb_rectangle_t rectangle{x, 24, 96, 48};
  checked(client.connection, xcb_poly_fill_rectangle_checked(
      client.connection, client.window, client.gc, 1, &rectangle),
      "paint failed");
  xcb_flush(client.connection);
}
}  // namespace

int main(int argc, char** argv) try {
  std::string input_socket, control_dir;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    if (index + 1 < argc && argument == "--display") ++index;
    else if (index + 1 < argc && argument == "--input-socket")
      input_socket = argv[++index];
    else if (index + 1 < argc && argument == "--control-dir")
      control_dir = argv[++index];
    else throw std::runtime_error("invalid command line");
  }
  require(!input_socket.empty() && !control_dir.empty(), "missing option");
  std::filesystem::create_directories(control_dir);
  XClient a, b; connect(a); connect(b); create(a, 0x002050d0U);
  create(b, 0x0020a050U);
  const auto gb = geometry(b);
  gw::test::SyntheticInputClient input(input_socket);
  std::uint64_t id = 1;
  const auto initial = input.barrier(id++);
  std::uint32_t time = initial.time_ms + 1;
  (void)input.motion(id++, time++, gb.x + 20, gb.y + 20);
  const auto click = input.button(id++, time++, 1, true);
  (void)input.button(id++, time++, 1, false);
  (void)input.key(id++, time++, 56, true);
  (void)input.key(id++, time++, 56, false);
  (void)input.barrier(id++);
  require(click.focus_window == b.window, "pre-restart focus mismatch");
  paint(a, 0x00d04040U, 12); paint(b, 0x00d040d0U, 20);
  std::ofstream(control_dir + "/ready").put('\n');
  for (int attempt = 0; attempt != 1200 &&
       !std::filesystem::exists(control_dir + "/continue"); ++attempt)
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  require(std::filesystem::exists(control_dir + "/continue"),
          "continue timed out");
  const auto replay = input.barrier(id++);
  require(replay.focus_window == b.window && replay.pointer_window == b.window,
          "input state did not survive restart");
  XPtr<xcb_get_input_focus_reply_t> focus(xcb_get_input_focus_reply(
      b.connection, xcb_get_input_focus(b.connection), nullptr));
  require(focus != nullptr && focus->focus == b.window,
          "X11 focus did not survive restart");
  (void)input.key(id++, time++, 56, true);
  (void)input.key(id++, time++, 56, false);
  paint(b, 0x00ffffffU, 132);
  const auto final = input.barrier(id++);
  std::ofstream result(control_dir + "/result.json");
  result << "{\"completed\":true,\"same_x11_connections\":true,"
            "\"same_input_connection\":true,\"focus_window\":"
         << final.focus_window << ",\"pointer_window\":"
         << final.pointer_window << ",\"post_restart_input\":true}\n";
  return result ? 0 : 1;
} catch (const std::exception& error) {
  std::fprintf(stderr, "m8_restart_hold_probe: %s\n", error.what());
  return 1;
}
