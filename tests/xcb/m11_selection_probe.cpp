#include <poll.h>
#include <xcb/xcb.h>

#include <chrono>
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

struct FreeDeleter {
  void operator()(void* value) const { std::free(value); }
};

template <typename T>
using XPtr = std::unique_ptr<T, FreeDeleter>;

struct Client {
  xcb_connection_t* connection{};
  xcb_screen_t* screen{};
  xcb_window_t window{};

  ~Client() {
    if (connection) xcb_disconnect(connection);
  }
};

[[noreturn]] void fail(const char* message) { throw std::runtime_error(message); }

void require(const bool condition, const char* message) {
  if (!condition) fail(message);
}

void checked(xcb_connection_t* connection, const xcb_void_cookie_t cookie,
             const char* message) {
  XPtr<xcb_generic_error_t> error(xcb_request_check(connection, cookie));
  require(!error, message);
}

void connect(Client& client) {
  int screen_index = 0;
  client.connection = xcb_connect(nullptr, &screen_index);
  require(client.connection && xcb_connection_has_error(client.connection) == 0,
          "XCB connection failed");
  auto screens = xcb_setup_roots_iterator(xcb_get_setup(client.connection));
  while (screen_index-- > 0) xcb_screen_next(&screens);
  require(screens.rem != 0, "XCB setup has no screen");
  client.screen = screens.data;
  client.window = xcb_generate_id(client.connection);
  constexpr std::uint32_t events = XCB_EVENT_MASK_PROPERTY_CHANGE;
  checked(client.connection,
          xcb_create_window_checked(
              client.connection, XCB_COPY_FROM_PARENT, client.window,
              client.screen->root, 0, 0, 1, 1, 0,
              XCB_WINDOW_CLASS_INPUT_OUTPUT, client.screen->root_visual,
              XCB_CW_EVENT_MASK, &events),
          "CreateWindow failed");
  require(xcb_flush(client.connection) > 0, "XCB flush failed");
}

xcb_atom_t intern(Client& client, const std::string_view name) {
  XPtr<xcb_generic_error_t> error;
  auto* raw_error = static_cast<xcb_generic_error_t*>(nullptr);
  XPtr<xcb_intern_atom_reply_t> reply(xcb_intern_atom_reply(
      client.connection,
      xcb_intern_atom(client.connection, 0,
                      static_cast<std::uint16_t>(name.size()), name.data()),
      &raw_error));
  error.reset(raw_error);
  require(reply && !error && reply->atom != XCB_ATOM_NONE, "InternAtom failed");
  return reply->atom;
}

XPtr<xcb_generic_event_t> wait_event(Client& client,
                                     const std::uint8_t expected) {
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::seconds(5);
  while (std::chrono::steady_clock::now() < deadline) {
    while (auto* raw = xcb_poll_for_event(client.connection)) {
      XPtr<xcb_generic_event_t> event(raw);
      if ((event->response_type & UINT8_C(0x7f)) == expected) return event;
    }
    pollfd descriptor{xcb_get_file_descriptor(client.connection), POLLIN, 0};
    if (::poll(&descriptor, 1, 20) < 0) fail("poll failed");
    require(xcb_connection_has_error(client.connection) == 0,
            "XCB connection failed while waiting for event");
  }
  fail("timed out waiting for X11 event");
}

void send_notify(Client& owner, const xcb_selection_request_event_t& request) {
  xcb_selection_notify_event_t notify{};
  notify.response_type = XCB_SELECTION_NOTIFY;
  notify.time = request.time;
  notify.requestor = request.requestor;
  notify.selection = request.selection;
  notify.target = request.target;
  notify.property = request.property;
  checked(owner.connection,
          xcb_send_event_checked(owner.connection, 0, request.requestor,
                                 XCB_EVENT_MASK_NO_EVENT,
                                 reinterpret_cast<const char*>(&notify)),
          "SendEvent SelectionNotify failed");
  require(xcb_flush(owner.connection) > 0, "owner flush failed");
}

xcb_selection_request_event_t receive_request(Client& owner,
                                               const xcb_atom_t selection,
                                               const xcb_atom_t target) {
  auto event = wait_event(owner, XCB_SELECTION_REQUEST);
  const auto* request =
      reinterpret_cast<const xcb_selection_request_event_t*>(event.get());
  require(request->owner == owner.window && request->selection == selection &&
              request->target == target && request->property != XCB_ATOM_NONE,
          "SelectionRequest fields do not match the conversion");
  return *request;
}

void await_notify(Client& requestor, const xcb_atom_t selection,
                  const xcb_atom_t target, const xcb_atom_t property) {
  auto event = wait_event(requestor, XCB_SELECTION_NOTIFY);
  const auto* notify =
      reinterpret_cast<const xcb_selection_notify_event_t*>(event.get());
  require(notify->requestor == requestor.window &&
              notify->selection == selection && notify->target == target &&
              notify->property == property,
          "SelectionNotify fields do not match the conversion");
}

std::vector<std::uint8_t> get_property(Client& requestor,
                                       const xcb_atom_t property,
                                       const xcb_atom_t type,
                                       const std::uint8_t format) {
  auto* raw_error = static_cast<xcb_generic_error_t*>(nullptr);
  XPtr<xcb_get_property_reply_t> reply(xcb_get_property_reply(
      requestor.connection,
      xcb_get_property(requestor.connection, 0, requestor.window, property,
                       type, 0, 1024),
      &raw_error));
  XPtr<xcb_generic_error_t> error(raw_error);
  require(reply && !error && reply->type == type && reply->format == format &&
              reply->bytes_after == 0,
          "GetProperty returned unexpected selection data");
  const auto size = xcb_get_property_value_length(reply.get());
  const auto* bytes = static_cast<const std::uint8_t*>(
      xcb_get_property_value(reply.get()));
  return {bytes, bytes + size};
}

}  // namespace

int main(int argc, char** argv) try {
  std::string output;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    if (argument == "--output" && index + 1 < argc)
      output = argv[++index];
    else
      fail("usage: m11_selection_probe [--output PATH]");
  }

  Client owner;
  Client requestor;
  connect(owner);
  connect(requestor);
  const auto clipboard = intern(owner, "CLIPBOARD");
  const auto targets = intern(owner, "TARGETS");
  const auto utf8 = intern(owner, "UTF8_STRING");
  const auto property = intern(owner, "GLASSWYRM_M11_SELECTION");

  checked(owner.connection,
          xcb_set_selection_owner_checked(owner.connection, owner.window,
                                          clipboard, XCB_CURRENT_TIME),
          "SetSelectionOwner failed");
  require(xcb_flush(owner.connection) > 0, "selection owner flush failed");
  XPtr<xcb_get_selection_owner_reply_t> owner_reply(
      xcb_get_selection_owner_reply(
          requestor.connection,
          xcb_get_selection_owner(requestor.connection, clipboard), nullptr));
  require(owner_reply && owner_reply->owner == owner.window,
          "GetSelectionOwner did not return the probe owner");

  xcb_convert_selection(requestor.connection, requestor.window, clipboard,
                        targets, property, XCB_CURRENT_TIME);
  require(xcb_flush(requestor.connection) > 0, "TARGETS conversion flush failed");
  auto request = receive_request(owner, clipboard, targets);
  const xcb_atom_t offered[] = {targets, utf8};
  checked(owner.connection,
          xcb_change_property_checked(
              owner.connection, XCB_PROP_MODE_REPLACE, request.requestor,
              request.property, XCB_ATOM_ATOM, 32, 2, offered),
          "TARGETS property write failed");
  send_notify(owner, request);
  await_notify(requestor, clipboard, targets, property);
  const auto targets_data = get_property(requestor, property, XCB_ATOM_ATOM, 32);
  require(targets_data.size() == sizeof(offered), "TARGETS payload size mismatch");

  constexpr std::string_view token = "M11_CLIPBOARD_TOKEN";
  xcb_convert_selection(requestor.connection, requestor.window, clipboard,
                        utf8, property, XCB_CURRENT_TIME);
  require(xcb_flush(requestor.connection) > 0, "UTF8_STRING conversion flush failed");
  request = receive_request(owner, clipboard, utf8);
  checked(owner.connection,
          xcb_change_property_checked(
              owner.connection, XCB_PROP_MODE_REPLACE, request.requestor,
              request.property, utf8, 8, token.size(), token.data()),
          "UTF8_STRING property write failed");
  send_notify(owner, request);
  await_notify(requestor, clipboard, utf8, property);
  const auto utf8_data = get_property(requestor, property, utf8, 8);
  require(std::string_view(reinterpret_cast<const char*>(utf8_data.data()),
                           utf8_data.size()) == token,
          "UTF8_STRING payload mismatch");

  // The interactive xterm path owns PRIMARY before this probe runs. Replace
  // it once so the live trace proves SelectionClear delivery as well as the
  // CLIPBOARD request/notify exchange above.
  XPtr<xcb_get_selection_owner_reply_t> previous_primary_reply(
      xcb_get_selection_owner_reply(
          requestor.connection,
          xcb_get_selection_owner(requestor.connection, XCB_ATOM_PRIMARY),
          nullptr));
  require(previous_primary_reply && previous_primary_reply->owner != XCB_NONE &&
              previous_primary_reply->owner != owner.window,
          "PRIMARY is not owned by the interactive xterm");
  checked(owner.connection,
          xcb_set_selection_owner_checked(owner.connection, owner.window,
                                          XCB_ATOM_PRIMARY, XCB_CURRENT_TIME),
          "PRIMARY replacement failed");
  require(xcb_flush(owner.connection) > 0, "PRIMARY replacement flush failed");
  XPtr<xcb_get_selection_owner_reply_t> primary_reply(
      xcb_get_selection_owner_reply(
          requestor.connection,
          xcb_get_selection_owner(requestor.connection, XCB_ATOM_PRIMARY),
          nullptr));
  require(primary_reply && primary_reply->owner == owner.window,
          "PRIMARY replacement owner mismatch");

  const std::string result =
      "{\"status\":\"passed\",\"selection\":\"CLIPBOARD\","
      "\"targets\":[\"TARGETS\",\"UTF8_STRING\"],"
      "\"token\":\"M11_CLIPBOARD_TOKEN\","
      "\"primary_replaced\":true}\n";
  if (output.empty()) {
    std::fwrite(result.data(), 1, result.size(), stdout);
  } else {
    std::ofstream stream(output);
    require(static_cast<bool>(stream), "cannot open output path");
    stream << result;
    require(static_cast<bool>(stream), "cannot write output path");
  }
  return 0;
} catch (const std::exception& error) {
  std::fprintf(stderr, "m11_selection_probe: %s\n", error.what());
  return 1;
}
