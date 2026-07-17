#include "glasswyrmd/composite_state.hpp"
#include "tests/helpers/test_support.hpp"

#include <cstdint>

namespace {

using glasswyrm::server::CompositeRedirectMode;
using glasswyrm::server::CompositeRedirectScope;
using glasswyrm::server::CompositeRedirectStatus;
using glasswyrm::server::CompositeState;
using gw::test::require;

constexpr auto kDirect = CompositeRedirectScope::Direct;
constexpr auto kSubtree = CompositeRedirectScope::Subtree;
constexpr auto kAutomatic = CompositeRedirectMode::Automatic;
constexpr auto kManual = CompositeRedirectMode::Manual;

void test_validation_is_atomic() {
  CompositeState state;
  require(state.redirect(1, 0, kDirect, kManual) ==
              CompositeRedirectStatus::InvalidWindow,
          "None cannot be redirected");
  require(state.redirect(1, 10,
                         static_cast<CompositeRedirectScope>(0xFFU), kManual) ==
              CompositeRedirectStatus::InvalidScope,
          "unknown redirect scope is rejected");
  require(state.redirect(1, 10, kDirect,
                         static_cast<CompositeRedirectMode>(0xFFU)) ==
              CompositeRedirectStatus::InvalidMode,
          "unknown redirect mode is rejected");
  require(state.entry_count() == 0,
          "validation failures do not create redirect entries");
}

void test_manual_ownership_and_scopes() {
  CompositeState state;
  require(state.redirect(1, 20, kDirect, kManual) ==
              CompositeRedirectStatus::Success,
          "manual direct redirect records its owner");
  require(state.redirect(1, 20, kDirect, kManual) ==
              CompositeRedirectStatus::Success &&
              state.manual_owner(20, kDirect) == 1,
          "same-owner manual redirect is idempotent");
  require(state.redirect(2, 20, kDirect, kManual) ==
              CompositeRedirectStatus::ManualConflict &&
              state.manual_owner(20, kDirect) == 1,
          "conflicting manual direct redirect preserves prior owner");
  require(state.redirect(2, 20, kSubtree, kManual) ==
              CompositeRedirectStatus::Success &&
              state.manual_owner(20, kSubtree) == 2,
          "direct and subtree ownership are independent");
  require(state.entry_count() == 2,
          "direct and subtree redirects use distinct entries");

  require(state.unredirect(2, 20, kDirect, kManual) ==
              CompositeRedirectStatus::NotOwner &&
              state.manual_owner(20, kDirect) == 1,
          "non-owner manual unredirect preserves state");
  require(state.unredirect(1, 20, kDirect, kManual) ==
              CompositeRedirectStatus::Success &&
              !state.redirected(20, kDirect),
          "manual owner can unredirect its direct entry");
  require(state.redirected(20, kSubtree),
          "direct unredirect cannot change subtree state");
}

void test_automatic_coexistence_and_mode_matching() {
  CompositeState state;
  require(state.redirect(1, 30, kDirect, kAutomatic) ==
              CompositeRedirectStatus::Success &&
              state.redirect(2, 30, kDirect, kAutomatic) ==
                  CompositeRedirectStatus::Success,
          "automatic redirects coexist across clients");
  require(state.automatic_owner_count(30, kDirect) == 2,
          "automatic redirect owners are deduplicated and counted");
  require(state.redirect(1, 30, kDirect, kAutomatic) ==
              CompositeRedirectStatus::Success &&
              state.automatic_owner_count(30, kDirect) == 2,
          "same-client automatic redirect is idempotent");
  require(state.redirect(3, 30, kDirect, kManual) ==
              CompositeRedirectStatus::Success &&
              state.owns(3, 30, kDirect, kManual),
          "one manual owner may coexist with automatic owners");

  require(state.unredirect(4, 30, kDirect, kAutomatic) ==
              CompositeRedirectStatus::NotOwner &&
              state.automatic_owner_count(30, kDirect) == 2,
          "automatic unredirect validates client ownership");
  require(state.unredirect(1, 30, kDirect, kManual) ==
              CompositeRedirectStatus::NotOwner &&
              state.manual_owner(30, kDirect) == 3,
          "manual unredirect validates mode-specific ownership");
  require(state.unredirect(1, 30, kDirect, kAutomatic) ==
              CompositeRedirectStatus::Success &&
              state.automatic_owner_count(30, kDirect) == 1 &&
              state.redirected(30, kDirect),
          "automatic owner removes only its own redirect");
}

void test_client_cleanup() {
  CompositeState state;
  require(state.redirect(1, 40, kDirect, kManual) ==
              CompositeRedirectStatus::Success &&
              state.redirect(1, 40, kDirect, kAutomatic) ==
                  CompositeRedirectStatus::Success &&
              state.redirect(2, 40, kDirect, kAutomatic) ==
                  CompositeRedirectStatus::Success &&
              state.redirect(1, 41, kSubtree, kAutomatic) ==
                  CompositeRedirectStatus::Success,
          "client cleanup fixture redirects are created");
  require(state.remove_client(1) == 3,
          "client cleanup reports every removed ownership claim");
  require(!state.owns(1, 40, kDirect, kManual) &&
              !state.owns(1, 40, kDirect, kAutomatic) &&
              !state.redirected(41, kSubtree),
          "client cleanup removes direct and subtree ownership");
  require(state.owns(2, 40, kDirect, kAutomatic) &&
              state.entry_count() == 1,
          "client cleanup preserves unrelated automatic owner");
  require(state.remove_client(99) == 0,
          "unknown client cleanup is a no-op");
}

void test_window_cleanup() {
  CompositeState state;
  require(state.redirect(1, 50, kDirect, kAutomatic) ==
              CompositeRedirectStatus::Success &&
              state.redirect(2, 50, kSubtree, kManual) ==
                  CompositeRedirectStatus::Success &&
              state.redirect(3, 51, kDirect, kManual) ==
                  CompositeRedirectStatus::Success,
          "window cleanup fixture redirects are created");
  require(state.remove_window(50) == 2,
          "window cleanup removes direct and subtree entries");
  require(!state.redirected(50, kDirect) &&
              !state.redirected(50, kSubtree),
          "window cleanup clears all scopes for the destroyed window");
  require(state.owns(3, 51, kDirect, kManual) && state.entry_count() == 1,
          "window cleanup preserves unrelated window redirects");
  require(state.remove_window(999) == 0,
          "unknown window cleanup is a no-op");
}

}  // namespace

int main() {
  test_validation_is_atomic();
  test_manual_ownership_and_scopes();
  test_automatic_coexistence_and_mode_matching();
  test_client_cleanup();
  test_window_cleanup();
  return 0;
}
