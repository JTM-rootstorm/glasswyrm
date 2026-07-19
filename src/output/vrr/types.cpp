#include "output/vrr/types.hpp"

namespace glasswyrm::output::vrr {

bool valid_policy_mode(const PolicyMode mode) noexcept {
  return mode >= PolicyMode::Off && mode <= PolicyMode::AlwaysEligible;
}

bool valid_window_preference(const WindowPreference preference) noexcept {
  return preference >= WindowPreference::Default &&
         preference <= WindowPreference::Prefer;
}

} // namespace glasswyrm::output::vrr
