#include "input/input_router.hpp"
#include "protocol/x11/event_mask.hpp"
#include "helpers/test_support.hpp"

using namespace glasswyrm;
using gw::test::require;
namespace em = gw::protocol::x11::event_mask;
namespace sm = gw::protocol::x11::state_mask;

static server::WindowCreateSpec window(const std::uint32_t xid,
                                       const std::uint32_t parent,
                                       const server::WindowClass cls = server::WindowClass::InputOutput) {
  server::WindowCreateSpec spec;
  spec.xid = xid; spec.parent = parent; spec.x = 10; spec.y = 10;
  spec.width = 50; spec.height = 40;
  spec.border_width = cls == server::WindowClass::InputOnly ? 0 : 8;
  spec.window_class = cls;
  return spec;
}

int main() {
  input::InputState state;
  require(state.pointer_x() == 0 && state.pointer_y() == 0 && state.pointer_target() == 1 &&
          state.time() == 1 && state.mask() == 0, "input initial state");
  require(input::clamp_pointer(-4, 999, 100, 80) == std::pair<std::int32_t,std::int32_t>{0,79},
          "pointer clamp");
  require(!state.accept_time(0) && state.accept_time(1) && state.accept_time(10) &&
          !state.accept_time(9) && state.time() == 10,
          "logical time is nonzero nondecreasing and permits equality");
  require(state.transition_button(1, true) == input::TransitionStatus::Accepted &&
          state.mask() == sm::Button1 &&
          state.transition_button(1, true) == input::TransitionStatus::InvalidTransition,
          "button transition and precondition");
  require(state.transition_button(8, true) == input::TransitionStatus::Accepted &&
          (state.mask() & sm::Button5) == 0 && state.any_button_down() &&
          state.transition_button(8, false) == input::TransitionStatus::Accepted &&
          state.transition_button(10, true) == input::TransitionStatus::InvalidValue,
          "extended buttons have no core state bit and remain bounded");
  require(state.transition_key(37, true) == input::TransitionStatus::Accepted &&
          state.transition_key(105, true) == input::TransitionStatus::Accepted &&
          (state.mask() & sm::Control) != 0 &&
          state.transition_key(37, false) == input::TransitionStatus::Accepted &&
          (state.mask() & sm::Control) != 0 &&
          state.transition_key(105, false) == input::TransitionStatus::Accepted &&
          (state.mask() & sm::Control) == 0, "paired modifiers remain active");
  state.reset_provider_state();
  require(state.mask() == 0, "provider reset clears buttons keys and modifiers");
  state.set_core_modifier_mask(sm::Lock | sm::Mod4);
  require(state.mask() == (sm::Lock | sm::Mod4) &&
              state.accept_wrapping_time(0xffffffffU) &&
              state.accept_wrapping_time(1) && state.time() == 1,
          "real input accepts externally derived modifiers and timestamp wrap");
  state.reset_provider_state();

  const std::vector<input::RouteWindow> windows{
      {1, 0, 0, {{1, em::KeyPress, true}}},
      {2, 1, 0, {{2, em::ButtonRelease, true}, {3, em::ButtonRelease, true}}},
      {3, 2, em::KeyPress, {}},
  };
  auto route = input::propagate_event(windows, 3, em::KeyPress);
  require(route.event_window == 0 && route.clients.empty(), "do-not-propagate stops route");
  route = input::propagate_event(windows, 3, em::ButtonRelease);
  require(route.event_window == 2 && route.clients == std::vector<server::ClientId>{2,3},
          "propagation stops at first selecting ancestor and delivers all");
  require(input::select_direct(windows, 2, em::ButtonRelease).clients.size() == 2 &&
          input::select_direct(windows, 2, em::FocusChange).event_window == 0,
          "direct selection is exact-window only");
  require(input::motion_delivery_mask(state) == em::PointerMotion,
          "motion hint is inert without pointer selection");

  using D = gw::protocol::x11::NotifyDetail;
  require(input::crossing_details(1, 1, 2) == std::pair{D::Inferior,D::Ancestor} &&
          input::crossing_details(1, 2, 1) == std::pair{D::Ancestor,D::Inferior} &&
          input::crossing_details(1, 2, 3) == std::pair{D::Nonlinear,D::Nonlinear},
          "one-level crossing detail matrix");
  require(input::crossing_focus(1, 1, 2) && input::crossing_focus(1, 2, 2) &&
          !input::crossing_focus(1, 3, 2), "crossing focus bit");

  server::ResourceTable resources;
  const auto root = resources.screen().root_window;
  constexpr std::uint32_t base = 0x00400000U, mask = 0x001fffffU;
  require(resources.create_window(1, base, mask, window(base + 1, root)) ==
              server::CreateWindowStatus::Success &&
          resources.create_window(1, base, mask, window(base + 2, root)) ==
              server::CreateWindowStatus::Success &&
          resources.create_window(1, base, mask,
              window(base + 3, root, server::WindowClass::InputOnly)) ==
              server::CreateWindowStatus::Success &&
          resources.create_window(1, base, mask, window(base + 4, base + 1)) ==
              server::CreateWindowStatus::Success,
          "hit-test window fixture");
  auto* first = resources.find_window(base + 1);
  auto* second = resources.find_window(base + 2);
  auto* input_only = resources.find_window(base + 3);
  first->map_state = second->map_state = input_only->map_state = server::MapState::Viewable;
  first->policy_visible = second->policy_visible = input_only->policy_visible = true;
  second->x = 20; second->y = 20;
  require(input::hit_test_top_level(resources, 25, 25) == base + 2,
          "topmost overlapping direct-root InputOutput wins");
  second->policy_visible = false;
  require(input::hit_test_top_level(resources, 25, 25) == base + 1,
          "hidden top window ignored");
  first->cleanup_pending = true;
  require(input::hit_test_top_level(resources, 25, 25) == root,
          "cleanup pending, InputOnly, and child windows ignored");
  first->cleanup_pending = false; first->attributes.override_redirect = true;
  require(input::hit_test_top_level(resources, 10, 10) == base + 1 &&
          input::hit_test_top_level(resources, 9, 10) == root &&
          input::hit_test_top_level(resources, 60, 10) == root,
          "override redirect included and borders excluded");
  auto* child = resources.find_window(base + 4);
  child->map_state = server::MapState::Viewable;
  child->x = 5;
  child->y = 6;
  child->border_width = 2;
  require(input::hit_test_top_level(resources, 18, 19) == base + 1 &&
              input::hit_test_deepest_viewable(resources, 18, 19) == base + 4 &&
              input::hit_test_deepest_viewable(resources, 14, 15) == base + 1 &&
              input::managed_top_level_ancestor(resources, base + 4) == base + 1 &&
              input::managed_top_level_ancestor(resources, base + 3) == root,
          "deep pointer hit-test descends into viewable children while policy resolves the managed ancestor");
  require(input::window_ancestry(resources, base + 4) ==
              std::vector<std::uint32_t>{base + 4, base + 1, root} &&
              input::window_ancestry(resources, base + 99).empty(),
          "pointer ancestry is ordered deepest-to-root and rejects missing windows");
  auto coordinates = input::event_coordinates(resources, base + 1, base + 2, 5, 7);
  require(coordinates.event_x == -5 && coordinates.event_y == -3 && coordinates.child == 0,
          "top-level signed event coordinates");
  coordinates = input::event_coordinates(resources, base + 4, base + 4, 18, 19);
  require(coordinates.event_x == 1 && coordinates.event_y == 1 &&
              coordinates.child == 0,
          "nested event coordinates include the complete window ancestry");
  coordinates = input::event_coordinates(resources, root, base + 1, 5, 7);
  require(coordinates.event_x == 5 && coordinates.event_y == 7 && coordinates.child == base + 1,
          "root coordinates name immediate pointer child");
}
