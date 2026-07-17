#include "glasswyrmd/server_state.hpp"

#include "glasswyrmd/ewmh.hpp"

namespace glasswyrm::server {

ServerState::ServerState(const ScreenModel screen, const bool game_compat)
    : screen_(screen), game_compat_(game_compat), resources_(screen) {
  if (game_compat_) game_compat_ = initialize_ewmh(*this);
}

}  // namespace glasswyrm::server
