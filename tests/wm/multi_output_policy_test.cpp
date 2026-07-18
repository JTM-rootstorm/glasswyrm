#include "wm/multi_output_policy.hpp"
#include "wm/policy_engine.hpp"
#include "wm/transaction.hpp"

#include <cstdio>
#include <cstdlib>

namespace {
using namespace glasswyrm::wm;

[[noreturn]] void fail(const char* message) {
  std::fprintf(stderr, "multi-output policy test: %s\n", message);
  std::exit(1);
}

void require(const bool condition, const char* message) {
  if (!condition) fail(message);
}

OutputContext output(const std::uint64_t id, const std::int32_t x,
                     const std::uint32_t work_height,
                     const bool primary) {
  OutputContext value;
  value.output_id = id;
  value.logical = {x, 0, 800, 600};
  value.work = {x, 0, 800, work_height};
  value.enabled = true;
  value.primary = primary;
  return value;
}

RawWindow window(const std::uint32_t id, const std::uint64_t serial) {
  RawWindow value;
  value.window_id = id;
  value.parent_window_id = 1;
  value.requested_width = 200;
  value.requested_height = 100;
  value.window_type = WindowType::Normal;
  value.wants_map = true;
  value.creation_serial = serial;
  value.map_serial = serial;
  return value;
}

RawState state() {
  RawState raw;
  raw.complete = true;
  raw.has_context = true;
  raw.context = {1, 1, 10, 0, 0, 1600, 600, 0};
  raw.outputs.emplace(10, output(10, 0, 560, true));
  raw.outputs.emplace(20, output(20, 800, 600, false));
  return raw;
}

void placement_and_cascade() {
  auto raw = state();
  raw.windows.emplace(1'001, window(1'001, 1));
  raw.windows.emplace(1'002, window(1'002, 2));
  raw.windows.emplace(1'003, window(1'003, 3));
  raw.windows.emplace(1'004, window(1'004, 4));
  raw.output_hints.emplace(1'003, WindowOutputHint{1'003, 0, 20, 0});
  raw.output_hints.emplace(1'004, WindowOutputHint{1'004, 0, 20, 0});
  const auto evaluated = evaluate(raw, 1);
  require(evaluated && evaluated.policy.windows.at(1'001).output_id == 10 &&
              evaluated.policy.windows.at(1'001).final_x == 0 &&
              evaluated.policy.windows.at(1'002).final_x == 32,
          "new windows cascade on the primary output");
  require(evaluated.policy.windows.at(1'003).output_id == 20 &&
              evaluated.policy.windows.at(1'003).final_x == 800 &&
              evaluated.policy.windows.at(1'004).final_x == 832,
          "preferred-output cascade state is independent");
}

void intersection_and_ties() {
  auto raw = state();
  auto crossing = window(1'001, 1);
  crossing.geometry_serial = 1;
  crossing.requested_x = 700;
  crossing.requested_width = 300;
  raw.windows.emplace(crossing.window_id, crossing);
  auto evaluated = evaluate(raw, 2);
  require(evaluated && evaluated.policy.windows.at(1'001).output_id == 20 &&
              evaluated.policy.windows.at(1'001).final_x == 700 &&
              evaluated.policy.windows.at(1'001).final_width == 300,
          "largest intersection assigns a spanning window without resizing it");

  raw.windows.at(1'001).requested_width = 200;
  raw.output_hints.emplace(1'001, WindowOutputHint{1'001, 10, 20, 0});
  evaluated = evaluate(raw, 3);
  require(evaluated && evaluated.policy.windows.at(1'001).output_id == 10,
          "an intersection tie first retains the previous output");
  raw.output_hints.at(1'001).previous_output_id = 99;
  evaluated = evaluate(raw, 4);
  require(evaluated && evaluated.policy.windows.at(1'001).output_id == 20,
          "an intersection tie next uses the preferred output");
  raw.output_hints.at(1'001).preferred_output_id = 99;
  evaluated = evaluate(raw, 5);
  require(evaluated && evaluated.policy.windows.at(1'001).output_id == 10,
          "an intersection tie falls back to the primary output");
}

void transients_and_override_redirect() {
  auto raw = state();
  raw.windows.emplace(1'001, window(1'001, 1));
  raw.output_hints.emplace(1'001, WindowOutputHint{1'001, 0, 20, 0});
  auto dialog = window(1'002, 2);
  dialog.transient_for = 1'001;
  dialog.window_type = WindowType::Dialog;
  dialog.requested_width = 100;
  dialog.requested_height = 60;
  raw.windows.emplace(dialog.window_id, dialog);
  auto bypass = window(1'003, 3);
  bypass.override_redirect = true;
  bypass.requested_x = 760;
  bypass.requested_y = 550;
  bypass.requested_width = 300;
  raw.windows.emplace(bypass.window_id, bypass);
  const auto evaluated = evaluate(raw, 6);
  require(evaluated && evaluated.policy.windows.at(1'002).output_id == 20 &&
              evaluated.policy.windows.at(1'002).final_x == 850,
          "a managed transient inherits and centers on its parent output");
  require(evaluated.policy.windows.at(1'003).output_id == 20 &&
              evaluated.policy.windows.at(1'003).final_x == 760 &&
              evaluated.policy.windows.at(1'003).final_y == 550,
          "override-redirect assignment preserves unrestricted geometry");
}

void movement_and_visibility() {
  auto raw = state();
  auto moving = window(1'001, 1);
  moving.geometry_serial = 1;
  moving.requested_x = 100;
  raw.windows.emplace(moving.window_id, moving);
  auto evaluated = evaluate(raw, 7);
  require(evaluated && evaluated.policy.windows.at(1'001).output_id == 10,
          "a persisted window begins on its intersected output");
  InteractivePolicy interaction;
  InteractionBegin begin;
  begin.kind = InteractionKind::Move;
  begin.target = 1'001;
  begin.button = 1;
  begin.pointer = {100, 100};
  begin.applied_geometry = {100, 0, 200, 100};
  begin.managed = true;
  begin.visible = true;
  begin.direct_root = true;
  begin.input_output = true;
  require(interaction.begin(begin).accepted,
          "interactive movement begins for a visible managed window");
  interaction.motion({900, 100});
  const auto moved = interaction.take_geometry_request();
  require(moved && moved->x == 900 && moved->width == 200,
          "interactive movement changes position without changing logical size");
  raw.windows.at(1'001).requested_x = moved->x;
  raw.windows.at(1'001).requested_y = moved->y;
  raw.windows.at(1'001).requested_width = moved->width;
  raw.windows.at(1'001).requested_height = moved->height;
  raw.windows.at(1'001).geometry_serial = 2;
  evaluated = evaluate(raw, 8);
  require(evaluated && evaluated.policy.windows.at(1'001).output_id == 20 &&
              evaluated.policy.windows.at(1'001).final_width == 200,
          "cross-output movement preserves logical size");

  raw.windows.at(1'001).requested_x = 3'000;
  raw.output_hints.emplace(1'001, WindowOutputHint{1'001, 20, 0, 0});
  evaluated = evaluate(raw, 9);
  require(evaluated && evaluated.policy.windows.at(1'001).output_id == 20 &&
              evaluated.policy.windows.at(1'001).final_x == 1'599 &&
              evaluated.policy.windows.at(1'001).final_width == 200,
          "an offscreen managed window retains one pixel without resizing");
}

void fullscreen_maximize_and_layout_changes() {
  auto raw = state();
  auto left = window(1'001, 1);
  left.fullscreen_requested = true;
  auto right = window(1'002, 2);
  right.maximized_requested = true;
  raw.windows.emplace(left.window_id, left);
  raw.windows.emplace(right.window_id, right);
  raw.output_hints.emplace(1'001, WindowOutputHint{1'001, 10, 0, 0});
  raw.output_hints.emplace(1'002, WindowOutputHint{1'002, 20, 0, 0});
  auto evaluated = evaluate(raw, 10);
  require(evaluated && evaluated.policy.windows.at(1'001).final_width == 800 &&
              evaluated.policy.windows.at(1'001).final_height == 560 &&
              evaluated.policy.windows.at(1'002).final_x == 800 &&
              evaluated.policy.windows.at(1'002).final_height == 600,
          "fullscreen and maximize use each assigned output work rectangle");

  raw.windows.at(1'001).fullscreen_requested = false;
  raw.windows.at(1'001).geometry_serial = 1;
  raw.windows.at(1'001).requested_x = 900;
  evaluated = evaluate(raw, 11);
  require(evaluated && evaluated.policy.windows.at(1'001).output_id == 20 &&
              evaluated.policy.windows.at(1'001).final_x == 900 &&
              evaluated.policy.windows.at(1'001).final_width == 200,
          "leaving fullscreen restores normal geometry and reassigns output");
  raw.windows.at(1'001).fullscreen_requested = true;

  OutputContext disabled;
  disabled.output_id = 10;
  raw.outputs.at(10) = disabled;
  raw.outputs.at(20).primary = true;
  raw.context = {1, 1, 20, 0, 0, 1'600, 600, 0};
  evaluated = evaluate(raw, 12);
  require(evaluated && evaluated.policy.windows.at(1'001).output_id == 20 &&
              evaluated.policy.windows.at(1'001).final_x == 800,
          "disabling the previous output deterministically migrates fullscreen");

  raw = state();
  raw.outputs.at(10).primary = false;
  raw.outputs.at(20).primary = true;
  raw.context.output_id = 20;
  raw.windows.emplace(1'001, window(1'001, 1));
  evaluated = evaluate(raw, 13);
  require(evaluated && evaluated.policy.windows.at(1'001).output_id == 20 &&
              evaluated.policy.windows.at(1'001).final_x == 800,
          "new placement follows a changed primary output");
}

void validation_and_v3_hash() {
  auto raw = state();
  raw.windows.emplace(1'001, window(1'001, 1));
  raw.output_hints.emplace(1'001, WindowOutputHint{1'001, 10, 20, 0});
  const auto first = evaluate(raw, 42);
  const auto replay = evaluate(raw, 42);
  require(first && replay && first.policy.hash == replay.policy.hash,
          "an identical reconnect snapshot reproduces its v3 policy hash");
  require(first.policy.hash == UINT64_C(0x27dd02889862e36c),
          "canonical v3 policy payload hash matches its known vector");
  require(interactive_policy_hash(first.policy, InteractiveBindings{}) ==
              UINT64_C(0xddec5cbf3fe2cdd6),
          "canonical interactive v3 hash matches its known vector");
  raw.output_hints.at(1'001).preferred_output_id = 10;
  require(evaluate(raw, 42).policy.hash != first.policy.hash,
          "window output hints participate in the v3 hash");

  raw = state();
  raw.outputs.at(20).logical.x = 700;
  require(evaluate(raw, 1).error == EvaluationError::InvalidContext,
          "overlapping enabled output records are rejected");
  raw = state();
  raw.outputs.at(20).scale_numerator = 2;
  raw.outputs.at(20).scale_denominator = 2;
  require(evaluate(raw, 1).error == EvaluationError::InvalidContext,
          "unreduced output scale records are rejected");

  raw = state();
  raw.outputs.at(20).scale_numerator = 5;
  raw.outputs.at(20).scale_denominator = 4;
  require(static_cast<bool>(evaluate(raw, 1)),
          "a reduced fractional scale within one through four is valid");
  raw.outputs.at(20).scale_numerator = 1;
  raw.outputs.at(20).scale_denominator = 2;
  require(evaluate(raw, 1).error == EvaluationError::InvalidContext,
          "an output scale below one is rejected");

  raw = state();
  raw.outputs.clear();
  raw.outputs.emplace(10, output(10, 20'000, 600, true));
  raw.outputs.emplace(20, output(20, 28'000, 600, false));
  raw.context = {1, 1, 10, 0, 0, 28'800, 600, 0};
  require(static_cast<bool>(evaluate(raw, 1)),
          "a valid root wider than the historical work-area limit is valid");
  raw.context.work_x = 20'000;
  raw.context.work_width = 8'800;
  require(evaluate(raw, 1).error == EvaluationError::InvalidContext,
          "a nonzero first output origin does not shift root coordinates");
}

void transaction_records() {
  const auto raw = state();
  const auto managed = window(1'001, 1);
  const WindowOutputHint hint{1'001, 10, 20, 0};
  Transaction transaction;
  require(transaction.begin_snapshot() && transaction.upsert(raw.context) &&
              transaction.upsert(raw.outputs.at(10)) &&
              transaction.upsert(raw.outputs.at(20)) &&
              transaction.upsert(hint) && transaction.upsert(managed) &&
              transaction.end_snapshot(),
          "transactions stage output records and hints in any item order");
  const auto evaluated = transaction.commit(1);
  require(evaluated && evaluated.policy.outputs.size() == 2 &&
              evaluated.policy.output_hints.at(1'001).preferred_output_id == 20,
          "an accepted transaction retains its multi-output policy records");
  require(transaction.remove(1'001) &&
              !transaction.pending().output_hints.contains(1'001),
          "removing a window also removes its stale output hint");
}

}  // namespace

int main() {
  placement_and_cascade();
  intersection_and_ties();
  transients_and_override_redirect();
  movement_and_visibility();
  fullscreen_maximize_and_layout_changes();
  validation_and_v3_hash();
  transaction_records();
  return 0;
}
