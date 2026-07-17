#include <xcb/xcb.h>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

struct FreeDeleter {
  void operator()(void* value) const { std::free(value); }
};
template <class T>
using XPtr = std::unique_ptr<T, FreeDeleter>;

struct Geometry {
  std::int16_t x{};
  std::int16_t y{};
  std::uint16_t width{};
  std::uint16_t height{};
};

struct SizeHints {
  std::uint32_t width_increment{};
  std::uint32_t height_increment{};
  std::uint32_t base_width{};
  std::uint32_t base_height{};
};

struct ShellSize {
  std::uint16_t rows{};
  std::uint16_t columns{};
};

void require(const bool condition, const std::string_view message) {
  if (!condition) throw std::runtime_error(std::string(message));
}

xcb_atom_t atom(xcb_connection_t* connection, const std::string_view name) {
  const auto cookie = xcb_intern_atom(connection, 0, name.size(), name.data());
  XPtr<xcb_intern_atom_reply_t> reply(
      xcb_intern_atom_reply(connection, cookie, nullptr));
  require(reply != nullptr && reply->atom != XCB_ATOM_NONE,
          "required X11 atom is unavailable");
  return reply->atom;
}

std::optional<std::string> text_property(xcb_connection_t* connection,
                                         const xcb_window_t window,
                                         const xcb_atom_t property) {
  XPtr<xcb_get_property_reply_t> reply(xcb_get_property_reply(
      connection,
      xcb_get_property(connection, 0, window, property, XCB_GET_PROPERTY_TYPE_ANY,
                       0, 1024),
      nullptr));
  if (!reply || reply->format != 8) return std::nullopt;
  const auto length = xcb_get_property_value_length(reply.get());
  const auto* bytes = static_cast<const char*>(xcb_get_property_value(reply.get()));
  return std::string(bytes, bytes + length);
}

std::optional<xcb_window_t> find_titled_window(
    xcb_connection_t* connection, const xcb_window_t parent,
    const xcb_atom_t wm_name, const xcb_atom_t net_wm_name,
    const std::string_view title) {
  for (const auto property : {net_wm_name, wm_name}) {
    const auto value = text_property(connection, parent, property);
    if (value && *value == title) return parent;
  }
  XPtr<xcb_query_tree_reply_t> tree(xcb_query_tree_reply(
      connection, xcb_query_tree(connection, parent), nullptr));
  if (!tree) return std::nullopt;
  const auto count = xcb_query_tree_children_length(tree.get());
  const auto* children = xcb_query_tree_children(tree.get());
  for (int index = 0; index < count; ++index) {
    const auto found = find_titled_window(connection, children[index], wm_name,
                                          net_wm_name, title);
    if (found) return found;
  }
  return std::nullopt;
}

Geometry geometry(xcb_connection_t* connection, const xcb_window_t window) {
  XPtr<xcb_get_geometry_reply_t> reply(xcb_get_geometry_reply(
      connection, xcb_get_geometry(connection, window), nullptr));
  require(reply != nullptr, "GetGeometry failed");
  return {reply->x, reply->y, reply->width, reply->height};
}

SizeHints size_hints(xcb_connection_t* connection, const xcb_window_t window) {
  constexpr std::uint32_t kResizeIncrement = 1U << 6U;
  constexpr std::uint32_t kBaseSize = 1U << 8U;
  const auto normal_hints = atom(connection, "WM_NORMAL_HINTS");
  const auto size_hints_atom = atom(connection, "WM_SIZE_HINTS");
  XPtr<xcb_get_property_reply_t> reply(xcb_get_property_reply(
      connection,
      xcb_get_property(connection, 0, window, normal_hints, size_hints_atom, 0,
                       18),
      nullptr));
  require(reply != nullptr && reply->format == 32 &&
              xcb_get_property_value_length(reply.get()) >= 18 * 4,
          "xterm WM_NORMAL_HINTS is incomplete");
  const auto* values = static_cast<const std::uint32_t*>(
      xcb_get_property_value(reply.get()));
  require((values[0] & kResizeIncrement) != 0 && values[9] != 0 &&
              values[10] != 0,
          "xterm resize increments are unavailable");
  const auto base_width =
      (values[0] & kBaseSize) != 0 ? values[15] : values[5];
  const auto base_height =
      (values[0] & kBaseSize) != 0 ? values[16] : values[6];
  return {values[9], values[10], base_width, base_height};
}

std::vector<pid_t> child_processes(const pid_t pid) {
  std::ifstream stream("/proc/" + std::to_string(pid) + "/task/" +
                       std::to_string(pid) + "/children");
  std::vector<pid_t> children;
  pid_t child{};
  while (stream >> child) children.push_back(child);
  return children;
}

std::optional<ShellSize> shell_size(const pid_t root) {
  std::vector<pid_t> pending{root};
  while (!pending.empty()) {
    const auto pid = pending.back();
    pending.pop_back();
    const auto children = child_processes(pid);
    pending.insert(pending.end(), children.begin(), children.end());
    const auto fd_path = "/proc/" + std::to_string(pid) + "/fd/0";
    const int fd = open(fd_path.c_str(), O_RDONLY | O_NOCTTY | O_CLOEXEC);
    if (fd < 0) continue;
    winsize value{};
    const bool valid = ioctl(fd, TIOCGWINSZ, &value) == 0 && value.ws_row != 0 &&
                       value.ws_col != 0;
    close(fd);
    if (valid) return ShellSize{value.ws_row, value.ws_col};
  }
  return std::nullopt;
}

std::string json_string(const std::string_view value) {
  std::string output{"\""};
  for (const char character : value) {
    if (character == '\\' || character == '"') output.push_back('\\');
    output.push_back(character);
  }
  output.push_back('"');
  return output;
}

void write_geometry(std::ostream& stream, const Geometry value) {
  stream << "{\"x\":" << value.x << ",\"y\":" << value.y
         << ",\"width\":" << value.width << ",\"height\":"
         << value.height << '}';
}

bool same_size(const Geometry left, const Geometry right) {
  return left.width == right.width && left.height == right.height;
}

}  // namespace

int main(int argc, char** argv) try {
  std::string title;
  std::string output;
  pid_t xterm_pid{};
  std::int32_t move_dx{};
  std::int32_t move_dy{};
  std::uint32_t columns{};
  std::uint32_t rows{};
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    require(index + 1 < argc, "option is missing its value");
    const std::string_view value(argv[++index]);
    if (argument == "--title") title = value;
    else if (argument == "--output") output = value;
    else if (argument == "--xterm-pid") xterm_pid = std::stoi(std::string(value));
    else if (argument == "--move-dx") move_dx = std::stoi(std::string(value));
    else if (argument == "--move-dy") move_dy = std::stoi(std::string(value));
    else if (argument == "--columns") columns = std::stoul(std::string(value));
    else if (argument == "--rows") rows = std::stoul(std::string(value));
    else throw std::runtime_error("unknown option: " + std::string(argument));
  }
  require(!title.empty() && !output.empty() && xterm_pid > 0 && columns > 0 &&
              rows > 0,
          "usage: m11_xterm_geometry_probe --title TITLE --xterm-pid PID "
          "--move-dx N --move-dy N --columns N --rows N --output PATH");

  int screen_index = 0;
  xcb_connection_t* raw = xcb_connect(nullptr, &screen_index);
  require(raw != nullptr && xcb_connection_has_error(raw) == 0,
          "XCB connection failed");
  const auto disconnect = [](xcb_connection_t* value) { xcb_disconnect(value); };
  std::unique_ptr<xcb_connection_t, decltype(disconnect)> connection(raw,
                                                                    disconnect);
  auto screens = xcb_setup_roots_iterator(xcb_get_setup(connection.get()));
  while (screen_index-- > 0) xcb_screen_next(&screens);
  require(screens.rem != 0, "XCB setup has no screen");

  const auto wm_name = atom(connection.get(), "WM_NAME");
  const auto net_wm_name = atom(connection.get(), "_NET_WM_NAME");
  const auto window = find_titled_window(connection.get(), screens.data->root,
                                         wm_name, net_wm_name, title);
  require(window.has_value(), "destination xterm title was not found");
  const auto hints = size_hints(connection.get(), *window);
  const auto initial = geometry(connection.get(), *window);
  require(initial.width == hints.base_width + 80 * hints.width_increment &&
              initial.height == hints.base_height + 24 * hints.height_increment,
          "destination xterm initial geometry is not 80x24");

  const std::uint32_t event_mask =
      XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_EXPOSURE;
  XPtr<xcb_generic_error_t> event_error(xcb_request_check(
      connection.get(), xcb_change_window_attributes_checked(
                            connection.get(), *window, XCB_CW_EVENT_MASK,
                            &event_mask)));
  require(event_error == nullptr, "could not observe destination xterm");
  xcb_flush(connection.get());

  const Geometry expected_move{
      static_cast<std::int16_t>(initial.x + move_dx),
      static_cast<std::int16_t>(initial.y + move_dy), initial.width,
      initial.height};
  const Geometry expected_resize{
      expected_move.x,
      expected_move.y,
      static_cast<std::uint16_t>(hints.base_width +
                                 columns * hints.width_increment),
      static_cast<std::uint16_t>(hints.base_height +
                                 rows * hints.height_increment)};
  std::optional<Geometry> moved;
  std::optional<Geometry> resized;
  std::uint32_t configure_notifies = 0;
  std::uint32_t resize_exposes = 0;
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::seconds(15);
  while (std::chrono::steady_clock::now() < deadline &&
         (!resized || resize_exposes == 0)) {
    XPtr<xcb_generic_event_t> event(xcb_poll_for_event(connection.get()));
    if (!event) {
      require(xcb_connection_has_error(connection.get()) == 0,
              "XCB event stream failed");
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      continue;
    }
    const auto type = event->response_type & 0x7fU;
    if (type == XCB_CONFIGURE_NOTIFY) {
      ++configure_notifies;
      const auto current = geometry(connection.get(), *window);
      if (!moved && current.x == expected_move.x &&
          current.y == expected_move.y && same_size(current, initial))
        moved = current;
      if (moved && current.x == expected_resize.x &&
          current.y == expected_resize.y &&
          current.width == expected_resize.width &&
          current.height == expected_resize.height)
        resized = current;
    } else if (type == XCB_EXPOSE && resized) {
      ++resize_exposes;
    }
  }
  require(moved.has_value(), "ConfigureNotify/GetGeometry did not prove move");
  require(resized.has_value(),
          "ConfigureNotify/GetGeometry did not prove exact resize");
  require(resize_exposes > 0, "destination xterm did not redraw after resize");

  std::optional<ShellSize> terminal;
  const auto shell_deadline = std::chrono::steady_clock::now() +
                              std::chrono::seconds(5);
  do {
    terminal = shell_size(xterm_pid);
    if (terminal && terminal->columns == columns && terminal->rows == rows)
      break;
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
  } while (std::chrono::steady_clock::now() < shell_deadline);
  require(terminal && terminal->columns == columns && terminal->rows == rows,
          "shell PTY dimensions do not match resized xterm");

  std::ofstream stream(output);
  require(stream.good(), "could not open geometry evidence output");
  stream << "{\n  \"schema\": 1,\n  \"status\": \"passed\",\n"
         << "  \"title\": " << json_string(title) << ",\n"
         << "  \"window_id\": " << *window << ",\n"
         << "  \"initial\": ";
  write_geometry(stream, initial);
  stream << ",\n  \"moved\": ";
  write_geometry(stream, *moved);
  stream << ",\n  \"resized\": ";
  write_geometry(stream, *resized);
  stream << ",\n  \"size_hints\": {\"base_width\":" << hints.base_width
         << ",\"base_height\":" << hints.base_height
         << ",\"width_increment\":" << hints.width_increment
         << ",\"height_increment\":" << hints.height_increment << "},\n"
         << "  \"shell\": {\"columns\":" << terminal->columns
         << ",\"rows\":" << terminal->rows << "},\n"
         << "  \"configure_notify_count\": " << configure_notifies << ",\n"
         << "  \"resize_expose_count\": " << resize_exposes << "\n}\n";
  require(stream.good(), "could not write geometry evidence output");
  return 0;
} catch (const std::exception& error) {
  std::fprintf(stderr, "m11_xterm_geometry_probe: %s\n", error.what());
  return 1;
}
