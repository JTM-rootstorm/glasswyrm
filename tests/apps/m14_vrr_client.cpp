#include "m14_vrr_client_options.hpp"
#include "m14_vrr_client_support.hpp"

#include <xcb/xcb.h>
#include <xcb/xcbext.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <sys/uio.h>
#include <thread>
#include <time.h>
#include <unistd.h>

namespace {

using gw::test::m14::ClientMode;
using gw::test::m14::ClientOptions;
using gw::test::m14::ClientState;
using gw::test::m14::ClientPreference;
using gw::test::m14::EventfdDamageProducer;

xcb_extension_t gw_vrr_extension{"GW_VRR", 0};

void require(const bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

void checked(xcb_connection_t *connection, const xcb_void_cookie_t cookie,
             const char *message) {
  auto *error = xcb_request_check(connection, cookie);
  if (!error)
    return;
  std::free(error);
  throw std::runtime_error(message);
}

xcb_atom_t atom(xcb_connection_t *connection, const std::string_view name) {
  const auto cookie = xcb_intern_atom(
      connection, false, static_cast<std::uint16_t>(name.size()), name.data());
  auto *reply = xcb_intern_atom_reply(connection, cookie, nullptr);
  require(reply != nullptr && reply->atom != XCB_ATOM_NONE,
          "required X11 atom is unavailable");
  const auto value = reply->atom;
  std::free(reply);
  return value;
}

void set_borderless(xcb_connection_t *connection, const xcb_window_t window) {
  struct MotifHints {
    std::uint32_t flags;
    std::uint32_t functions;
    std::uint32_t decorations;
    std::int32_t input_mode;
    std::uint32_t status;
  };
  const MotifHints hints{UINT32_C(1) << 1U, 0, 0, 0, 0};
  const auto motif = atom(connection, "_MOTIF_WM_HINTS");
  checked(connection,
          xcb_change_property_checked(connection, XCB_PROP_MODE_REPLACE, window,
                                      motif, motif, 32, 5, &hints),
          "could not set deterministic borderless hints");
}

void request_fullscreen(xcb_connection_t *connection,
                        const xcb_screen_t &screen, const xcb_window_t window) {
  const auto wm_state = atom(connection, "_NET_WM_STATE");
  const auto fullscreen = atom(connection, "_NET_WM_STATE_FULLSCREEN");
  xcb_client_message_event_t event{};
  event.response_type = XCB_CLIENT_MESSAGE;
  event.format = 32;
  event.window = window;
  event.type = wm_state;
  event.data.data32[0] = 1;
  event.data.data32[1] = fullscreen;
  checked(connection,
          xcb_send_event_checked(connection, false, screen.root,
                                 XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT |
                                     XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY,
                                 reinterpret_cast<const char *>(&event)),
          "could not request deterministic fullscreen transition");
}

struct VrrRequest {
  std::uint8_t major_opcode{};
  std::uint8_t minor_opcode{};
  std::uint16_t length{};
  std::uint32_t first{};
  std::uint32_t second{};
};

static_assert(sizeof(VrrRequest) == 12);

void *vrr_reply(xcb_connection_t *connection, const std::uint8_t minor,
                const std::uint32_t first, const std::uint32_t second,
                const std::uint8_t words = 2) {
  VrrRequest request{};
  request.minor_opcode = minor;
  request.first = first;
  request.second = second;
  const xcb_protocol_request_t protocol{2, &gw_vrr_extension, minor, 0};
  std::array<iovec, 4> parts{};
  parts[2].iov_base = &request;
  parts[2].iov_len = 4U + static_cast<std::size_t>(words) * 4U;
  parts[3].iov_base = nullptr;
  parts[3].iov_len = -sizeof(request) & 3U;
  const auto sequence = xcb_send_request(connection, XCB_REQUEST_CHECKED,
                                         parts.data() + 2, &protocol);
  require(sequence != 0, "could not send GW_VRR request");
  xcb_generic_error_t *error = nullptr;
  auto *reply = xcb_wait_for_reply(connection, sequence, &error);
  if (error) {
    std::free(error);
    std::free(reply);
    throw std::runtime_error("GW_VRR request returned an X11 error");
  }
  require(reply != nullptr, "GW_VRR request returned no reply");
  return reply;
}

struct VrrEvidence {
  bool selected{};
  std::uint32_t replies{};
  std::uint32_t events{};
  std::uint32_t change_mask{};
  std::uint64_t reason_mask{};
};

void select_vrr_events(xcb_connection_t *connection,
                       const xcb_window_t window) {
  VrrRequest request{};
  request.minor_opcode = 1;
  request.first = window;
  request.second = 7;
  const xcb_protocol_request_t protocol{2, &gw_vrr_extension, 1, 1};
  std::array<iovec, 4> parts{};
  parts[2].iov_base = &request;
  parts[2].iov_len = sizeof(request);
  parts[3].iov_base = nullptr;
  parts[3].iov_len = 0;
  const auto sequence = xcb_send_request(connection, XCB_REQUEST_CHECKED,
                                         parts.data() + 2, &protocol);
  require(sequence != 0, "could not select GW_VRR events");
  checked(connection, {sequence}, "GW_VRR SelectInput failed");
}

void observe_vrr_event(xcb_connection_t *connection, VrrEvidence &evidence,
                       const bool required) {
  for (unsigned attempt = 0; attempt < 200; ++attempt) {
    auto *event = xcb_poll_for_event(connection);
    if (!event) {
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
      continue;
    }
    if ((event->response_type & 0x7fU) == 70U) {
      const auto *bytes = reinterpret_cast<const std::uint8_t *>(event);
      ++evidence.events;
      evidence.change_mask |= bytes[1] & 7U;
      std::uint32_t high{}, low{};
      std::memcpy(&high, bytes + 16, sizeof(high));
      std::memcpy(&low, bytes + 20, sizeof(low));
      evidence.reason_mask = (static_cast<std::uint64_t>(high) << 32U) | low;
      std::free(event);
      return;
    }
    std::free(event);
  }
  require(!required, "GW_VRR notification was not observed");
}

void set_preference(xcb_connection_t *connection, const xcb_window_t window,
                    const gw::test::m14::ClientPreference preference,
                    VrrEvidence &evidence, const bool event_required = true) {
  const auto *extension = xcb_get_extension_data(connection, &gw_vrr_extension);
  require(extension && extension->present, "GW_VRR is unavailable");
  auto *version = static_cast<std::uint8_t *>(vrr_reply(connection, 0, 0, 1));
  std::uint32_t major{}, minor{};
  std::memcpy(&major, version + 8, sizeof(major));
  std::memcpy(&minor, version + 12, sizeof(minor));
  std::free(version);
  require(major == 0 && minor >= 1, "GW_VRR 0.1 was not negotiated");

  auto *changed =
      static_cast<std::uint8_t *>(vrr_reply(
          connection, 3, window, static_cast<std::uint16_t>(preference)));
  std::uint16_t accepted{};
  std::memcpy(&accepted, changed + 8, sizeof(accepted));
  std::free(changed);
  require(accepted == static_cast<std::uint16_t>(preference),
          "GW_VRR preference was not accepted");
  ++evidence.replies;
  observe_vrr_event(connection, evidence, event_required);
  auto *queried = static_cast<std::uint8_t *>(vrr_reply(connection, 2, window, 0, 1));
  std::uint16_t current{};
  std::memcpy(&current, queried + 12, sizeof(current));
  std::free(queried);
  require(current == static_cast<std::uint16_t>(preference),
          "GW_VRR preference query disagreed with reply");
  auto *state = static_cast<std::uint8_t *>(vrr_reply(connection, 4, window, 0, 1));
  std::uint32_t high{}, low{};
  std::memcpy(&high, state + 24, sizeof(high));
  std::memcpy(&low, state + 28, sizeof(low));
  evidence.reason_mask = (static_cast<std::uint64_t>(high) << 32U) | low;
  std::free(state);
}

void put_pixels(xcb_connection_t *connection, const xcb_drawable_t drawable,
                const xcb_gcontext_t gc, const std::uint16_t width,
                const std::uint16_t height, const std::int16_t x,
                const std::int16_t y, const std::vector<std::uint32_t> &pixels,
                const std::uint8_t depth) {
  require(pixels.size() == static_cast<std::size_t>(width) * height,
          "pixel rectangle size mismatch");
  checked(connection,
          xcb_put_image_checked(
              connection, XCB_IMAGE_FORMAT_Z_PIXMAP, drawable, gc, width,
              height, x, y, 0, depth,
              static_cast<std::uint32_t>(pixels.size() * sizeof(pixels[0])),
              reinterpret_cast<const std::uint8_t *>(pixels.data())),
          "core PutImage failed");
}

void synchronize(xcb_connection_t *connection) {
  const auto cookie = xcb_get_input_focus(connection);
  auto *reply = xcb_get_input_focus_reply(connection, cookie, nullptr);
  require(reply != nullptr, "X11 synchronization failed");
  std::free(reply);
}

void service_bounded_repaints(
    xcb_connection_t *connection, const ClientOptions &options,
    const xcb_window_t window, const xcb_gcontext_t gc,
    const std::uint16_t width, const std::uint16_t height,
    const std::uint8_t depth) {
  if (options.repaint_count == 0) {
    if (options.hold_ms != 0)
      std::this_thread::sleep_for(std::chrono::milliseconds(options.hold_ms));
    return;
  }
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(options.hold_ms);
  std::uint32_t completed{};
  while (completed < options.repaint_count &&
         std::chrono::steady_clock::now() < deadline) {
    if (::unlink(options.repaint_trigger.c_str()) == 0) {
      put_pixels(connection, window, gc, width, height, 0, 0,
                 gw::test::m14::deterministic_pattern(width, height), depth);
      require(xcb_flush(connection) > 0,
              "could not flush requested bounded repaint");
      synchronize(connection);
      ++completed;
      continue;
    }
    require(errno == ENOENT, "could not consume bounded repaint trigger");
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  require(completed == options.repaint_count,
          "bounded repaint trigger count was not satisfied");
}

int run_client(const ClientOptions &options) {
  int preferred_screen{};
  auto *connection = xcb_connect(options.display.c_str(), &preferred_screen);
  require(connection && xcb_connection_has_error(connection) == 0,
          "could not connect to the requested X11 display");
  try {
    const auto &setup = *xcb_get_setup(connection);
    auto iterator = xcb_setup_roots_iterator(&setup);
    for (int index = 0; index < preferred_screen && iterator.rem > 0; ++index)
      xcb_screen_next(&iterator);
    require(iterator.rem > 0 && iterator.data, "X11 screen is unavailable");
    const auto &screen = *iterator.data;

    const bool borderless = options.mode == ClientMode::Borderless;
    const bool fullscreen = options.mode == ClientMode::Fullscreen ||
                            options.mode == ClientMode::Cadence;
    const auto width =
        borderless ? screen.width_in_pixels
                   : std::min<std::uint16_t>(640, screen.width_in_pixels);
    const auto height =
        borderless ? screen.height_in_pixels
                   : std::min<std::uint16_t>(480, screen.height_in_pixels);
    require(width >= gw::test::m14::kDamageWidth &&
                height >= gw::test::m14::kDamageHeight,
            "X11 screen is too small for the bounded pattern");

    const auto window = xcb_generate_id(connection);
    const std::array values{
        screen.black_pixel,
        static_cast<std::uint32_t>(XCB_EVENT_MASK_EXPOSURE |
                                   XCB_EVENT_MASK_STRUCTURE_NOTIFY)};
    checked(connection,
            xcb_create_window_checked(
                connection, screen.root_depth, window, screen.root, 0, 0, width,
                height, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT, screen.root_visual,
                XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK, values.data()),
            "could not create direct-root managed window");
    const auto title =
        std::string("Glasswyrm M14 VRR ") +
        std::string(gw::test::m14::client_mode_name(options.mode));
    checked(connection,
            xcb_change_property_checked(
                connection, XCB_PROP_MODE_REPLACE, window, XCB_ATOM_WM_NAME,
                XCB_ATOM_STRING, 8, static_cast<std::uint32_t>(title.size()),
                title.data()),
            "could not name VRR client window");
    if (borderless)
      set_borderless(connection, window);

    const auto gc = xcb_generate_id(connection);
    checked(connection,
            xcb_create_gc_checked(connection, gc, window, 0, nullptr),
            "could not create deterministic pattern GC");
    checked(connection, xcb_map_window_checked(connection, window),
            "could not map VRR client window");
    checked(connection,
            xcb_set_input_focus_checked(connection, XCB_INPUT_FOCUS_PARENT,
                                        window, XCB_CURRENT_TIME),
            "could not focus VRR client window");
    if (fullscreen)
      request_fullscreen(connection, screen, window);
    const auto pattern_width =
        std::min<std::uint16_t>(width, gw::test::m14::kPatternWidth);
    const auto pattern_height =
        std::min<std::uint16_t>(height, gw::test::m14::kPatternHeight);
    put_pixels(
        connection, window, gc, pattern_width, pattern_height, 0, 0,
        gw::test::m14::deterministic_pattern(pattern_width, pattern_height),
        screen.root_depth);
    require(xcb_flush(connection) > 0, "could not flush initial X11 pattern");
    synchronize(connection);

    VrrEvidence vrr_evidence;
    const auto *extension = xcb_get_extension_data(connection, &gw_vrr_extension);
    require(extension && extension->present, "GW_VRR is unavailable");
    auto *version = static_cast<std::uint8_t *>(vrr_reply(connection, 0, 0, 1));
    std::uint32_t major{}, minor{};
    std::memcpy(&major, version + 8, sizeof(major));
    std::memcpy(&minor, version + 12, sizeof(minor));
    std::free(version);
    require(major == 0 && minor >= 1, "GW_VRR 0.1 was not negotiated");
    select_vrr_events(connection, window);
    vrr_evidence.selected = true;
    if (options.mode == ClientMode::Preference) {
      set_preference(connection, window, ClientPreference::Default,
                     vrr_evidence, false);
      for (const auto preference : {ClientPreference::Allow,
                                    ClientPreference::Prefer,
                                    ClientPreference::Disable}) {
        set_preference(connection, window, preference, vrr_evidence);
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
      }
    } else if (options.preference_set) {
      set_preference(connection, window, options.preference, vrr_evidence,
                     options.preference != ClientPreference::Default);
    }

    const auto interval =
        gw::test::m14::target_interval_nanoseconds(options.target_refresh_hz);
    bool eventfd_synchronized = false;
    if (options.mode == ClientMode::Cadence) {
      EventfdDamageProducer producer;
      eventfd_synchronized = true;
      timespec start_time{};
      require(::clock_gettime(CLOCK_MONOTONIC, &start_time) == 0,
              "could not read monotonic cadence clock");
      const auto start = static_cast<std::uint64_t>(start_time.tv_sec) *
                             UINT64_C(1'000'000'000) +
                         static_cast<std::uint64_t>(start_time.tv_nsec);
      for (std::uint32_t frame = 0; frame < options.frame_count; ++frame) {
        std::uint64_t deadline{};
        require(gw::test::m14::absolute_deadline(start, interval, frame,
                                                 deadline) &&
                    gw::test::m14::wait_until_monotonic(deadline),
                "absolute monotonic cadence scheduling failed");
        const auto pixels = producer.produce(frame);
        put_pixels(connection, window, gc, gw::test::m14::kDamageWidth,
                   gw::test::m14::kDamageHeight, 0, 0, pixels,
                   screen.root_depth);
        require(xcb_flush(connection) > 0, "could not flush cadence frame");
      }
      synchronize(connection);
    }

    const ClientState state{options.mode,
                            window,
                            width,
                            height,
                            options.prefer,
                            fullscreen,
                            borderless,
                            options.frame_count,
                            options.target_refresh_hz,
                            interval,
                            options.mode == ClientMode::Preference
                                ? ClientPreference::Disable
                                : options.preference,
                            vrr_evidence.selected,
                            vrr_evidence.replies,
                            vrr_evidence.events,
                            vrr_evidence.change_mask,
                            vrr_evidence.reason_mask,
                            eventfd_synchronized};
    gw::test::m14::write_client_state(options.result_path, state);
    service_bounded_repaints(connection, options, window, gc, pattern_width,
                             pattern_height, screen.root_depth);
    xcb_disconnect(connection);
    return 0;
  } catch (...) {
    xcb_disconnect(connection);
    throw;
  }
}

} // namespace

int main(const int argc, char **argv) try {
  ClientOptions options;
  if (!gw::test::m14::parse_client_options(argc, argv, options)) {
    std::cerr << gw::test::m14::kClientUsage;
    return 2;
  }
  if (options.help) {
    std::cout << gw::test::m14::kClientUsage;
    return 0;
  }
  if (options.self_test) {
    std::cout << "m14_vrr_client: use m14_vrr_client_test for self-tests\n";
    return 0;
  }
  return run_client(options);
} catch (const std::exception &error) {
  std::cerr << "m14_vrr_client: " << error.what() << '\n';
  return 1;
}
