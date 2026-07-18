#include "glasswyrmd/server_state.hpp"

#include "glasswyrmd/ewmh.hpp"

#include <utility>

namespace glasswyrm::server {

ServerState::ServerState(const ScreenModel screen, const bool game_compat)
    : screen_(screen), game_compat_(game_compat), resources_(screen) {
  if (game_compat_) game_compat_ = initialize_ewmh(*this);
}

bool ServerState::update_screen_geometry(const ScreenModel screen) {
  ServerState staged = *this;
  if (!staged.resources_.update_screen_geometry(screen)) return false;
  staged.screen_ = screen;
  if (staged.game_compat_) synchronize_ewmh_root_properties(staged);
  *this = std::move(staged);
  return true;
}

}  // namespace glasswyrm::server
