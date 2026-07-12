#include "input/input_router.hpp"
#include "protocol/x11/event_mask.hpp"
#include "helpers/test_support.hpp"

using namespace glasswyrm;
using gw::test::require;
namespace em = gw::protocol::x11::event_mask;
namespace sm = gw::protocol::x11::state_mask;

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
  require(state.transition_button(6, true) == input::TransitionStatus::InvalidValue,
          "button range");
  require(state.transition_key(37, true) == input::TransitionStatus::Accepted &&
          state.transition_key(105, true) == input::TransitionStatus::Accepted &&
          (state.mask() & sm::Control) != 0 &&
          state.transition_key(37, false) == input::TransitionStatus::Accepted &&
          (state.mask() & sm::Control) != 0 &&
          state.transition_key(105, false) == input::TransitionStatus::Accepted &&
          (state.mask() & sm::Control) == 0, "paired modifiers remain active");
  state.reset_provider_state();
  require(state.mask() == 0, "provider reset clears buttons keys and modifiers");

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
}
