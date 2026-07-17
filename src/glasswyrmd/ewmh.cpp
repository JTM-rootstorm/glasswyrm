#include "glasswyrmd/ewmh.hpp"

#include "glasswyrmd/server_state.hpp"
#include "protocol/x11/atoms.hpp"

#include <algorithm>
#include <array>
#include <span>
#include <string_view>

namespace glasswyrm::server {
namespace {

constexpr std::array kGameAtoms{
    "_NET_SUPPORTED", "_NET_SUPPORTING_WM_CHECK", "_NET_WM_NAME",
    "_NET_CLIENT_LIST", "_NET_CLIENT_LIST_STACKING", "_NET_ACTIVE_WINDOW",
    "_NET_CURRENT_DESKTOP", "_NET_NUMBER_OF_DESKTOPS",
    "_NET_DESKTOP_GEOMETRY", "_NET_DESKTOP_VIEWPORT", "_NET_WORKAREA",
    "_NET_WM_STATE", "_NET_WM_STATE_FULLSCREEN",
    "_NET_WM_STATE_MAXIMIZED_VERT", "_NET_WM_STATE_MAXIMIZED_HORZ",
    "_NET_WM_STATE_ABOVE", "_NET_WM_STATE_FOCUSED", "_NET_WM_WINDOW_TYPE",
    "_NET_WM_WINDOW_TYPE_NORMAL", "_NET_WM_WINDOW_TYPE_UTILITY",
    "_NET_WM_WINDOW_TYPE_TOOLTIP", "_NET_WM_WINDOW_TYPE_POPUP_MENU",
    "_NET_WM_BYPASS_COMPOSITOR", "_NET_WM_PID", "_NET_CLOSE_WINDOW",
    "_MOTIF_WM_HINTS", "WM_PROTOCOLS", "WM_DELETE_WINDOW", "WM_TAKE_FOCUS",
    "UTF8_STRING"};

std::uint32_t atom(const ServerState& state, const std::string_view name) {
  return state.atoms().find(name).value_or(0);
}

void replace(ServerState& state, const std::uint32_t window,
             const std::string_view name, const std::uint32_t type,
             PropertyData data) {
  const auto property = atom(state, name);
  if (property != 0)
    (void)state.resources().change_property(
        window, property, Property{type, std::move(data)},
        PropertyMode::Replace);
}

const std::vector<std::uint32_t>* values32(const WindowResource& window,
                                           const std::uint32_t property) {
  const auto found = window.properties.find(property);
  return found == window.properties.end()
             ? nullptr
             : std::get_if<std::vector<std::uint32_t>>(&found->second.data);
}

bool contains(const std::vector<std::uint32_t>* values,
              const std::uint32_t value) {
  return values && std::find(values->begin(), values->end(), value) !=
                       values->end();
}

std::uint32_t clamp_hint(const std::uint32_t value) {
  return std::min<std::uint32_t>(value, UINT16_MAX);
}

void synchronize_window_state(ServerState& state, const std::uint32_t xid,
                              const WindowResource& window) {
  std::vector<std::uint32_t> values;
  if (window.fullscreen_requested)
    values.push_back(atom(state, "_NET_WM_STATE_FULLSCREEN"));
  if (window.maximized_requested) {
    values.push_back(atom(state, "_NET_WM_STATE_MAXIMIZED_VERT"));
    values.push_back(atom(state, "_NET_WM_STATE_MAXIMIZED_HORZ"));
  }
  if (window.above_requested)
    values.push_back(atom(state, "_NET_WM_STATE_ABOVE"));
  if (window.focused)
    values.push_back(atom(state, "_NET_WM_STATE_FOCUSED"));
  replace(state, xid, "_NET_WM_STATE", 4, std::move(values));
}

}  // namespace

bool initialize_ewmh(ServerState& state) {
  for (const auto name : kGameAtoms)
    if (state.atoms().intern(name, false).status != InternAtomStatus::Success)
      return false;
  if (!state.resources().create_server_proxy_window(kEwmhSupportingWindow))
    return false;

  const auto atom_type = std::uint32_t{4};
  const auto cardinal = std::uint32_t{6};
  const auto window_type = std::uint32_t{33};
  std::vector<std::uint32_t> supported;
  supported.reserve(kGameAtoms.size());
  for (const auto name : kGameAtoms) {
    const auto value = atom(state, name);
    if (value != 0) supported.push_back(value);
  }
  replace(state, state.screen().root_window, "_NET_SUPPORTED", atom_type,
          std::move(supported));
  replace(state, state.screen().root_window, "_NET_SUPPORTING_WM_CHECK",
          window_type, std::vector<std::uint32_t>{kEwmhSupportingWindow});
  replace(state, kEwmhSupportingWindow, "_NET_SUPPORTING_WM_CHECK",
          window_type, std::vector<std::uint32_t>{kEwmhSupportingWindow});
  const std::string_view name{"Glasswyrm"};
  replace(state, kEwmhSupportingWindow, "_NET_WM_NAME",
          atom(state, "UTF8_STRING"),
          std::vector<std::uint8_t>(name.begin(), name.end()));
  replace(state, state.screen().root_window, "_NET_NUMBER_OF_DESKTOPS",
          cardinal, std::vector<std::uint32_t>{1});
  replace(state, state.screen().root_window, "_NET_CURRENT_DESKTOP", cardinal,
          std::vector<std::uint32_t>{0});
  replace(state, state.screen().root_window, "_NET_DESKTOP_GEOMETRY", cardinal,
          std::vector<std::uint32_t>{state.screen().width_pixels,
                                     state.screen().height_pixels});
  replace(state, state.screen().root_window, "_NET_DESKTOP_VIEWPORT", cardinal,
          std::vector<std::uint32_t>{0, 0});
  replace(state, state.screen().root_window, "_NET_WORKAREA", cardinal,
          std::vector<std::uint32_t>{0, 0, state.screen().width_pixels,
                                     state.screen().height_pixels});
  synchronize_ewmh_root_properties(state);
  return true;
}

void synchronize_ewmh_root_properties(ServerState& state) {
  if (!state.game_compat()) return;
  std::vector<std::uint32_t> clients;
  std::vector<std::uint32_t> stacking;
  const auto* root = state.resources().find_window(state.screen().root_window);
  if (!root) return;
  for (const auto xid : root->children) {
    const auto* window = state.resources().find_window(xid);
    if (!window || !state.resources().is_policy_candidate(xid)) continue;
    clients.push_back(xid);
    synchronize_window_state(state, xid, *window);
    if (window->policy_visible) stacking.push_back(xid);
  }
  const auto window_type = std::uint32_t{33};
  replace(state, state.screen().root_window, "_NET_CLIENT_LIST", window_type,
          std::move(clients));
  replace(state, state.screen().root_window, "_NET_CLIENT_LIST_STACKING",
          window_type, std::move(stacking));
  replace(state, state.screen().root_window, "_NET_ACTIVE_WINDOW", window_type,
          std::vector<std::uint32_t>{
              state.focused_window() == state.screen().root_window
                  ? 0U
                  : state.focused_window()});
}

bool ewmh_property_is_protected(const ServerState& state,
                                const std::uint32_t window,
                                const std::uint32_t property) {
  if (!state.game_compat()) return false;
  if (window == kEwmhSupportingWindow) return true;
  if (window != state.screen().root_window) return false;
  for (const auto name : {"_NET_SUPPORTED", "_NET_SUPPORTING_WM_CHECK",
                          "_NET_CLIENT_LIST", "_NET_CLIENT_LIST_STACKING",
                          "_NET_ACTIVE_WINDOW", "_NET_CURRENT_DESKTOP",
                          "_NET_NUMBER_OF_DESKTOPS", "_NET_DESKTOP_GEOMETRY",
                          "_NET_DESKTOP_VIEWPORT", "_NET_WORKAREA"})
    if (property == atom(state, name)) return true;
  return false;
}

bool ewmh_property_affects_policy(const ServerState& state,
                                  const std::uint32_t property) {
  if (!state.game_compat()) return false;
  for (const auto name : {"_NET_WM_STATE", "_NET_WM_WINDOW_TYPE",
                          "_MOTIF_WM_HINTS", "_NET_WM_BYPASS_COMPOSITOR",
                          "WM_TRANSIENT_FOR", "WM_NORMAL_HINTS", "WM_HINTS"})
    if (property == atom(state, name)) return true;
  return false;
}

bool interpret_ewmh_window(ServerState& state, const std::uint32_t xid) {
  auto* window = state.resources().find_window(xid);
  if (!window || !state.resources().is_policy_candidate(xid)) return false;
  const bool was_transitioned =
      window->fullscreen_requested || window->maximized_requested;
  const auto states = values32(*window, atom(state, "_NET_WM_STATE"));
  window->fullscreen_requested =
      contains(states, atom(state, "_NET_WM_STATE_FULLSCREEN"));
  window->maximized_requested =
      contains(states, atom(state, "_NET_WM_STATE_MAXIMIZED_VERT")) &&
      contains(states, atom(state, "_NET_WM_STATE_MAXIMIZED_HORZ"));
  window->above_requested =
      contains(states, atom(state, "_NET_WM_STATE_ABOVE"));
  const bool transitioned =
      window->fullscreen_requested || window->maximized_requested;
  if (!was_transitioned && transitioned) {
    window->saved_normal_geometry = SavedWindowGeometry{
        window->requested_x, window->requested_y, window->requested_width,
        window->requested_height, window->requested_border_width};
  } else if (was_transitioned && !transitioned &&
             window->saved_normal_geometry) {
    const auto saved = *window->saved_normal_geometry;
    window->requested_x = saved.x;
    window->requested_y = saved.y;
    window->requested_width = saved.width;
    window->requested_height = saved.height;
    window->requested_border_width = saved.border_width;
    window->saved_normal_geometry.reset();
  }

  window->policy_window_type = PolicyWindowType::Normal;
  const auto types = values32(*window, atom(state, "_NET_WM_WINDOW_TYPE"));
  if (contains(types, atom(state, "_NET_WM_WINDOW_TYPE_UTILITY")) ||
      contains(types, atom(state, "_NET_WM_WINDOW_TYPE_TOOLTIP")) ||
      contains(types, atom(state, "_NET_WM_WINDOW_TYPE_POPUP_MENU")))
    window->policy_window_type = PolicyWindowType::Utility;

  window->decoration_preference = PolicyDecoration::Unknown;
  if (const auto motif = values32(*window, atom(state, "_MOTIF_WM_HINTS"));
      motif && motif->size() >= 3 && ((*motif)[0] & 2U) != 0)
    window->decoration_preference = (*motif)[2] == 0
                                        ? PolicyDecoration::False
                                        : PolicyDecoration::True;

  const auto bypass =
      values32(*window, atom(state, "_NET_WM_BYPASS_COMPOSITOR"));
  window->bypass_compositor = bypass && !bypass->empty() && (*bypass)[0] == 1;
  const auto transient = values32(*window, 68);
  window->transient_for = transient && transient->size() == 1
                              ? (*transient)[0]
                              : 0;

  window->minimum_width = window->minimum_height = 0;
  window->maximum_width = window->maximum_height = 0;
  if (const auto hints = values32(*window, 40); hints && hints->size() >= 9) {
    if (((*hints)[0] & (1U << 4)) != 0) {
      window->minimum_width = clamp_hint((*hints)[5]);
      window->minimum_height = clamp_hint((*hints)[6]);
    }
    if (((*hints)[0] & (1U << 5)) != 0) {
      window->maximum_width = clamp_hint((*hints)[7]);
      window->maximum_height = clamp_hint((*hints)[8]);
    }
  }
  window->input_requested = true;
  window->attention_requested = false;
  if (const auto hints = values32(*window, 35); hints && hints->size() >= 2) {
    if (((*hints)[0] & 1U) != 0) window->input_requested = (*hints)[1] != 0;
    window->attention_requested = ((*hints)[0] & (1U << 8)) != 0;
  }
  return true;
}

}  // namespace glasswyrm::server
