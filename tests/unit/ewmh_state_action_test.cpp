#include "glasswyrmd/ewmh.hpp"
#include "tests/unit/ewmh_test_support.hpp"

namespace {

using namespace glasswyrm::server;
namespace x11 = gw::protocol::x11;
using ewmh_test::client_message;
using ewmh_test::create_window;
using ewmh_test::property_values;
using gw::test::require;

void test_state_actions_and_maximize_pair(const x11::ByteOrder order) {
  ServerState state(kScreenModel, true);
  constexpr std::uint32_t window = 0x400010;
  create_window(state, 7, window);
  DispatchContext context{1, 0x400000, 0x1fffff, 32, order, false};
  const auto root = state.screen().root_window;
  const auto net_state = state.atoms().find("_NET_WM_STATE").value();
  const auto fullscreen =
      state.atoms().find("_NET_WM_STATE_FULLSCREEN").value();
  const auto above = state.atoms().find("_NET_WM_STATE_ABOVE").value();
  const auto vertical =
      state.atoms().find("_NET_WM_STATE_MAXIMIZED_VERT").value();
  const auto horizontal =
      state.atoms().find("_NET_WM_STATE_MAXIMIZED_HORZ").value();

  auto result = dispatch_request(
      state, context,
      client_message(order, root, window, net_state,
                     {1, fullscreen, above, 1, 0}));
  require(result.output.empty() &&
              state.resources().find_window(window)->fullscreen_requested &&
              state.resources().find_window(window)->above_requested &&
              property_values(state, window, net_state) ==
                  std::vector<std::uint32_t>({fullscreen, above}),
          "_NET_WM_STATE Add commits two known atoms");

  result = dispatch_request(
      state, context,
      client_message(order, root, window, net_state,
                     {0, fullscreen, 0, 1, 0}));
  require(result.output.empty() &&
              !state.resources().find_window(window)->fullscreen_requested &&
              state.resources().find_window(window)->above_requested &&
              property_values(state, window, net_state) ==
                  std::vector<std::uint32_t>({above}),
          "_NET_WM_STATE Remove clears only the requested state");

  result = dispatch_request(
      state, context,
      client_message(order, root, window, net_state,
                     {2, fullscreen, above, 1, 0}));
  require(result.output.empty() &&
              state.resources().find_window(window)->fullscreen_requested &&
              !state.resources().find_window(window)->above_requested &&
              property_values(state, window, net_state) ==
                  std::vector<std::uint32_t>({fullscreen}),
          "_NET_WM_STATE Toggle flips two known states independently");

  (void)dispatch_request(
      state, context,
      client_message(order, root, window, net_state,
                     {0, fullscreen, 0, 1, 0}));
  (void)dispatch_request(
      state, context,
      client_message(order, root, window, net_state,
                     {1, vertical, 0, 1, 0}));
  require(!state.resources().find_window(window)->maximized_requested,
          "one maximize axis does not request maximization");
  (void)dispatch_request(
      state, context,
      client_message(order, root, window, net_state,
                     {1, horizontal, 0, 1, 0}));
  require(state.resources().find_window(window)->maximized_requested &&
              state.resources().find_window(window)->saved_normal_geometry,
          "paired maximize atoms enter one geometry transition");
  (void)dispatch_request(
      state, context,
      client_message(order, root, window, net_state,
                     {0, vertical, 0, 1, 0}));
  require(!state.resources().find_window(window)->maximized_requested &&
              !state.resources().find_window(window)->saved_normal_geometry,
          "removing either maximize axis leaves the transition");
  (void)dispatch_request(
      state, context,
      client_message(order, root, window, net_state,
                     {2, vertical, horizontal, 1, 0}));
  require(!state.resources().find_window(window)->maximized_requested &&
              property_values(state, window, net_state) ==
                  std::vector<std::uint32_t>({vertical}),
          "paired Toggle evaluates each maximize atom from its own state");
  (void)dispatch_request(
      state, context,
      client_message(order, root, window, net_state,
                     {2, horizontal, 0, 1, 0}));
  require(state.resources().find_window(window)->maximized_requested,
          "Toggle completes the paired maximize transition");
}

}  // namespace

int main() {
  for (const auto order : {x11::ByteOrder::LittleEndian,
                           x11::ByteOrder::BigEndian})
    test_state_actions_and_maximize_pair(order);
  return 0;
}
