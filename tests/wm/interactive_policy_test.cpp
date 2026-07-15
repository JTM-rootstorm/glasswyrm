#include "wm/interactive_policy.hpp"
#include "wm/policy_engine.hpp"

#include "helpers/test_support.hpp"

using namespace glasswyrm::wm;
using gw::test::require;

int main() {
  const InteractiveBindings defaults;
  require(defaults.move_modifiers == kInteractiveMod1 &&
              defaults.resize_modifiers == kInteractiveMod1 &&
              defaults.close_modifiers == kInteractiveMod1 &&
              defaults.move_button == 1 && defaults.resize_button == 3 &&
              defaults.close_keysym == kInteractiveF4Keysym &&
              defaults.minimum_width == 96 && defaults.minimum_height == 64 &&
              defaults.raise_on_focus && defaults.consume_wm_bindings,
          "M11 default bindings are frozen");
  PolicyState hash_fixture;
  hash_fixture.hash = UINT64_C(0x1122334455667788);
  const auto default_hash = interactive_policy_hash(hash_fixture, defaults);
  auto changed_bindings = defaults;
  ++changed_bindings.minimum_width;
  require(default_hash != hash_fixture.hash &&
              default_hash == interactive_policy_hash(hash_fixture, defaults) &&
              default_hash !=
                  interactive_policy_hash(hash_fixture, changed_bindings),
          "v2 policy hash is stable and capability-specific to bindings");

  InteractivePolicy policy;
  InteractionBegin move{InteractionKind::Move,
                        10,
                        1,
                        {100, 100},
                        {20, 30, 640, 480},
                        true,
                        true,
                        true,
                        true};
  const auto begun = policy.begin(move);
  require(begun.accepted && begun.consume_event && begun.focus && begun.raise &&
              begun.cursor == InteractionCursor::FleurMove &&
              !policy.cursor_published(),
          "managed visible direct-root window begins move interaction");
  require(!policy.begin(move).accepted, "only one interaction may be active");
  policy.motion({110, 120});
  require(policy.take_geometry_request() ==
                  InteractiveGeometry{30, 50, 640, 480} &&
              policy.transaction_in_flight(),
          "move geometry uses initial pointer delta");
  policy.motion({140, 150});
  policy.motion({160, 180});
  require(!policy.take_geometry_request(),
          "motion coalesces while one lifecycle transaction is in flight");
  require(policy.complete_geometry({30, 50, 640, 480}) &&
              policy.last_committed() ==
                  InteractiveGeometry{30, 50, 640, 480} &&
              policy.take_geometry_request() ==
                  InteractiveGeometry{80, 110, 640, 480},
          "latest coalesced motion dispatches after accepted geometry");
  require(policy.release(1, {170, 190}) && !policy.finish_ready() &&
              policy.complete_geometry({80, 110, 640, 480}) &&
              policy.take_geometry_request() ==
                  InteractiveGeometry{90, 120, 640, 480} &&
              policy.complete_geometry({90, 120, 640, 480}) &&
              !policy.finish_ready() && policy.confirm_cursor_published() &&
              policy.finish_ready() && policy.finish() &&
              policy.kind() == InteractionKind::None &&
              policy.cursor() == InteractionCursor::None,
          "release waits for final geometry and one accepted cursor publication");

  InteractionBegin resize{InteractionKind::ResizeBottomRight,
                          11,
                          3,
                          {200, 200},
                          {10, 20, 100, 70},
                          true,
                          true,
                          true,
                          true};
  require(policy.begin(resize).accepted &&
              policy.cursor() == InteractionCursor::BottomRightResize,
          "resize interaction selects bottom-right cursor");
  policy.motion({100, 100});
  require(policy.take_geometry_request() == InteractiveGeometry{10, 20, 96, 64},
          "resize clamps to policy minimums without moving origin");
  require(policy.abort() &&
              policy.last_committed() == InteractiveGeometry{10, 20, 100, 70},
          "abort retains last fully committed geometry");

  require(policy.begin(move).accepted &&
              policy.release(1, move.pointer) && !policy.finish_ready() &&
              policy.kind() == InteractionKind::Move &&
              policy.cursor() == InteractionCursor::FleurMove &&
              policy.confirm_cursor_published() && policy.finish_ready() &&
              policy.finish(),
          "same-batch press and release retains the move cursor until accepted");

  for (auto rejected : {
           InteractionBegin{InteractionKind::Move,
                            0,
                            1,
                            {},
                            {0, 0, 1, 1},
                            true,
                            true,
                            true,
                            true},
           InteractionBegin{InteractionKind::Move,
                            1,
                            1,
                            {},
                            {0, 0, 1, 1},
                            false,
                            true,
                            true,
                            true},
           InteractionBegin{InteractionKind::Move,
                            1,
                            1,
                            {},
                            {0, 0, 1, 1},
                            true,
                            false,
                            true,
                            true},
           InteractionBegin{InteractionKind::Move,
                            1,
                            1,
                            {},
                            {0, 0, 1, 1},
                            true,
                            true,
                            false,
                            true},
       })
    require(!policy.begin(rejected).accepted,
            "invalid interaction target is rejected");

  auto close =
      evaluate_close_binding(defaults, kInteractiveMod1, kInteractiveF4Keysym,
                             20, true, false, true, 500);
  require(close.action == CloseAction::SendDeleteWindow &&
              close.consume_event && close.target == 20 &&
              close.event_time == 500,
          "Alt+F4 prefers WM_DELETE_WINDOW");
  close =
      evaluate_close_binding(defaults, kInteractiveMod1, kInteractiveF4Keysym,
                             20, true, false, false, 501);
  require(close.action == CloseAction::DestroyTopLevel,
          "close falls back to one coordinated top-level destroy");
  require(evaluate_close_binding(defaults, 0, kInteractiveF4Keysym, 20, true,
                                 false, true, 502)
                      .action == CloseAction::None &&
              evaluate_close_binding(defaults, kInteractiveMod1,
                                     kInteractiveF4Keysym, 20, true, true, true,
                                     502)
                      .action == CloseAction::None,
          "nonmatching and override-redirect close targets are ignored");
}
