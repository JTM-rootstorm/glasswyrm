#include "helpers/synthetic_input_client.hpp"

#include <xcb/xcb.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
struct FreeDeleter { void operator()(void* value) const { std::free(value); } };
template <class T> using XPtr = std::unique_ptr<T, FreeDeleter>;
struct Client {
  xcb_connection_t* connection{};
  xcb_screen_t* screen{};
  xcb_window_t window{};
  xcb_gcontext_t gc{};
  std::vector<std::uint8_t> events;
  ~Client() { if (connection != nullptr) xcb_disconnect(connection); }
};

void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
void checked(xcb_connection_t* connection, xcb_void_cookie_t cookie,
             const char* message) {
  XPtr<xcb_generic_error_t> error(xcb_request_check(connection, cookie));
  require(error == nullptr, message);
}
void connect(Client& client) {
  int screen_index = 0;
  client.connection = xcb_connect(nullptr, &screen_index);
  require(client.connection != nullptr &&
              xcb_connection_has_error(client.connection) == 0,
          "XCB connection failed");
  auto screens = xcb_setup_roots_iterator(xcb_get_setup(client.connection));
  while (screen_index-- > 0) xcb_screen_next(&screens);
  require(screens.rem != 0, "XCB setup has no screen");
  client.screen = screens.data;
}
void create_window(Client& client, std::uint32_t color) {
  client.window = xcb_generate_id(client.connection);
  client.gc = xcb_generate_id(client.connection);
  constexpr std::uint32_t mask =
      XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_EXPOSURE |
      XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_LEAVE_WINDOW |
      XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_BUTTON_PRESS |
      XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_BUTTON_1_MOTION |
      XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE |
      XCB_EVENT_MASK_FOCUS_CHANGE;
  const std::uint32_t values[] = {color, mask};
  checked(client.connection,
          xcb_create_window_checked(
              client.connection, XCB_COPY_FROM_PARENT, client.window,
              client.screen->root, 0, 0, 320, 200, 0,
              XCB_WINDOW_CLASS_INPUT_OUTPUT, client.screen->root_visual,
              XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK, values),
          "CreateWindow failed");
  const std::uint32_t gc_values[] = {color};
  checked(client.connection,
          xcb_create_gc_checked(client.connection, client.gc, client.window,
                                XCB_GC_FOREGROUND, gc_values),
          "CreateGC failed");
  checked(client.connection, xcb_map_window_checked(client.connection,
                                                     client.window),
          "MapWindow failed");
  xcb_flush(client.connection);
}
void drain(Client& client) {
  while (auto* raw = xcb_poll_for_event(client.connection)) {
    XPtr<xcb_generic_event_t> event(raw);
    client.events.push_back(event->response_type & 0x7fU);
  }
}
xcb_generic_event_t* wait_for(Client& client, std::uint8_t expected) {
  for (;;) {
    auto* event = xcb_wait_for_event(client.connection);
    require(event != nullptr, "XCB event stream ended");
    const auto type = static_cast<std::uint8_t>(event->response_type & 0x7fU);
    client.events.push_back(type);
    if (type == expected) return event;
    std::free(event);
  }
}
struct Geometry { std::int16_t x{}, y{}; std::uint16_t width{}, height{}; };
Geometry geometry(Client& client) {
  XPtr<xcb_get_geometry_reply_t> reply(xcb_get_geometry_reply(
      client.connection, xcb_get_geometry(client.connection, client.window),
      nullptr));
  require(reply != nullptr, "GetGeometry failed");
  return {reply->x, reply->y, reply->width, reply->height};
}
void fill(Client& client, std::uint32_t color, xcb_rectangle_t rectangle) {
  checked(client.connection,
          xcb_change_gc_checked(client.connection, client.gc,
                                XCB_GC_FOREGROUND, &color),
          "ChangeGC failed");
  checked(client.connection,
          xcb_poly_fill_rectangle_checked(client.connection, client.window,
                                          client.gc, 1, &rectangle),
          "PolyFillRectangle failed");
  xcb_flush(client.connection);
}
}  // namespace

int main(int argc, char** argv) try {
  std::string input_socket, output;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    if (index + 1 < argc && argument == "--input-socket")
      input_socket = argv[++index];
    else if (index + 1 < argc && argument == "--output")
      output = argv[++index];
    else throw std::runtime_error(
        "usage: xcb_milestone8_probe --input-socket PATH --output PATH");
  }
  require(!input_socket.empty() && !output.empty(), "missing required option");
  Client a, b; connect(a); connect(b);
  create_window(a, 0x002050d0U);
  create_window(b, 0x0020a050U);
  xcb_get_input_focus_reply(a.connection, xcb_get_input_focus(a.connection),
                            nullptr);
  xcb_get_input_focus_reply(b.connection, xcb_get_input_focus(b.connection),
                            nullptr);
  drain(a); drain(b);
  const auto ga = geometry(a), gb = geometry(b);
  gw::test::SyntheticInputClient input(input_socket);
  std::uint64_t id = 1;
  const auto initial = input.barrier(id++);
  std::uint32_t time = initial.time_ms + 1;
  (void)input.motion(id++, time++, ga.x + 8, ga.y + 8);
  {
    XPtr<xcb_generic_event_t> event(wait_for(a, XCB_MOTION_NOTIFY));
  }
  const auto focus_a = input.button(id++, time++, 1, true);
  require(focus_a.focus_window == a.window, "click did not focus client A");
  {
    XPtr<xcb_generic_event_t> event(wait_for(a, XCB_BUTTON_PRESS));
  }
  fill(a, 0x00d04040U, {12, 12, 80, 52});
  (void)input.button(id++, time++, 1, false);
  {
    XPtr<xcb_generic_event_t> event(wait_for(a, XCB_BUTTON_RELEASE));
  }
  (void)input.key(id++, time++, 38, true);
  {
    XPtr<xcb_generic_event_t> event(wait_for(a, XCB_KEY_PRESS));
  }
  fill(a, 0x00f0d040U, {16, 80, 160, 16});
  (void)input.key(id++, time++, 38, false);
  {
    XPtr<xcb_generic_event_t> event(wait_for(a, XCB_KEY_RELEASE));
  }
  (void)input.key(id++, time++, 50, true);
  {
    XPtr<xcb_generic_event_t> event(wait_for(a, XCB_KEY_PRESS));
  }
  (void)input.key(id++, time++, 38, true);
  {
    XPtr<xcb_key_press_event_t> event(
        reinterpret_cast<xcb_key_press_event_t*>(wait_for(a, XCB_KEY_PRESS)));
    require((event->state & XCB_MOD_MASK_SHIFT) != 0, "ShiftMask missing");
  }
  (void)input.key(id++, time++, 38, false);
  {
    XPtr<xcb_generic_event_t> event(wait_for(a, XCB_KEY_RELEASE));
  }
  (void)input.key(id++, time++, 50, false);
  {
    XPtr<xcb_generic_event_t> event(wait_for(a, XCB_KEY_RELEASE));
  }
  const auto overlap_x = std::max(ga.x, gb.x) + 24;
  const auto overlap_y = std::max(ga.y, gb.y) + 24;
  (void)input.motion(id++, time++, overlap_x, overlap_y);
  {
    XPtr<xcb_generic_event_t> event(wait_for(a, XCB_LEAVE_NOTIFY));
  }
  {
    XPtr<xcb_generic_event_t> event(wait_for(b, XCB_ENTER_NOTIFY));
  }
  {
    XPtr<xcb_generic_event_t> event(wait_for(b, XCB_MOTION_NOTIFY));
  }
  const auto focus_b = input.button(id++, time++, 1, true);
  require(focus_b.focus_window == b.window, "click did not focus client B");
  {
    XPtr<xcb_generic_event_t> event(wait_for(b, XCB_BUTTON_PRESS));
  }
  fill(b, 0x00d040d0U, {20, 20, 96, 48});
  (void)input.motion(id++, time++, overlap_x + 20, overlap_y + 16);
  {
    XPtr<xcb_motion_notify_event_t> event(
        reinterpret_cast<xcb_motion_notify_event_t*>(
            wait_for(b, XCB_MOTION_NOTIFY)));
    require((event->state & XCB_BUTTON_MASK_1) != 0,
            "Button1Motion state missing");
  }
  (void)input.button(id++, time++, 1, false);
  {
    XPtr<xcb_generic_event_t> event(wait_for(b, XCB_BUTTON_RELEASE));
  }
  (void)input.key(id++, time++, 56, true);
  {
    XPtr<xcb_generic_event_t> event(wait_for(b, XCB_KEY_PRESS));
  }
  fill(b, 0x00ffffffU, {24, 88, 180, 16});
  (void)input.key(id++, time++, 56, false);
  {
    XPtr<xcb_generic_event_t> event(wait_for(b, XCB_KEY_RELEASE));
  }
  const auto final = input.barrier(id++);
  require(final.focus_window == b.window, "final focus mismatch");
  xcb_get_input_focus_reply_t* raw_focus = xcb_get_input_focus_reply(
      b.connection, xcb_get_input_focus(b.connection), nullptr);
  XPtr<xcb_get_input_focus_reply_t> focus(raw_focus);
  require(focus != nullptr && focus->focus == b.window,
          "GetInputFocus mismatch");
  drain(a); drain(b);
  std::ofstream stream(output);
  require(static_cast<bool>(stream), "cannot open output");
  stream << "{\"completed\":true,\"window_a\":" << a.window
         << ",\"window_b\":" << b.window << ",\"focus\":"
         << final.focus_window << ",\"pointer_window\":"
         << final.pointer_window << ",\"state\":" << final.state
         << ",\"events_a\":" << a.events.size() << ",\"events_b\":"
         << b.events.size() << "}\n";
  return stream ? 0 : 1;
} catch (const std::exception& error) {
  std::fprintf(stderr, "xcb_milestone8_probe: %s\n", error.what());
  return 1;
}
