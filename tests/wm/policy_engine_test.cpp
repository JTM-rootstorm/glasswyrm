#include "wm/policy_engine.hpp"
#include "wm/transaction.hpp"

#include <cstdio>
#include <cstdlib>
#include <limits>

namespace {
using namespace glasswyrm::wm;

[[noreturn]] void fail(const char* message) {
  std::fprintf(stderr, "wm policy test: %s\n", message);
  std::exit(1);
}
void require(const bool condition, const char* message) {
  if (!condition) fail(message);
}

Context context() { return {1, 1, 7, 100, 50, 640, 480, 0}; }
RawWindow window(const std::uint32_t id, const std::uint64_t serial) {
  RawWindow value;
  value.window_id = id; value.parent_window_id = 1;
  value.requested_width = 200; value.requested_height = 100;
  value.window_type = WindowType::Normal; value.wants_map = true;
  value.creation_serial = serial; value.map_serial = serial;
  return value;
}
RawState state() {
  RawState value;
  value.complete = true; value.has_context = true; value.context = context();
  value.windows.emplace(10, window(10, 1));
  value.windows.emplace(20, window(20, 2));
  return value;
}

void validation() {
  auto raw = state();
  raw.context.work_width = 0;
  require(evaluate(raw, 1).error == EvaluationError::InvalidContext,
          "zero work area is rejected");
  raw = state(); raw.windows.at(20).creation_serial = 1;
  require(evaluate(raw, 1).error == EvaluationError::InvalidWindow,
          "duplicate creation serial is rejected");
  raw = state(); raw.windows.at(20).transient_for = 999;
  require(evaluate(raw, 1).error == EvaluationError::UnknownReference,
          "unknown transient is rejected");
  raw = state(); raw.windows.at(10).transient_for = 20;
  raw.windows.at(20).transient_for = 10;
  require(evaluate(raw, 1).error == EvaluationError::InvalidWindow,
          "transient cycle is rejected");
  raw = state(); raw.windows.at(10).border_width = maximum_border_width + 1U;
  require(evaluate(raw, 1).error == EvaluationError::InvalidWindow,
          "excessive border width is rejected");
  raw = state(); raw.windows.at(10).requested_width = maximum_window_extent + 1U;
  require(evaluate(raw, 1).error == EvaluationError::InvalidWindow,
          "global requested dimension cap is enforced");
  raw = state(); raw.context.work_x = std::numeric_limits<std::int32_t>::max();
  raw.context.work_width = 2;
  require(evaluate(raw, 1).error == EvaluationError::InvalidContext,
          "overflowing signed work extent is rejected");
  raw = state(); raw.windows.at(10).parent_window_id = 20;
  require(evaluate(raw, 1).error == EvaluationError::InvalidWindow,
          "non-root parent is rejected");
  raw = state(); raw.windows.at(10).workspace_id = 2;
  require(evaluate(raw, 1).error == EvaluationError::UnsupportedMetadata,
          "a second workspace is rejected");
  raw = state(); raw.windows.at(10).window_type = static_cast<WindowType>(99);
  require(evaluate(raw, 1).error == EvaluationError::InvalidWindow,
          "unknown internal enum values are rejected");
}

void cascade_and_determinism() {
  auto raw = state();
  const auto evaluated = evaluate(raw, 3);
  require(evaluated && evaluated.policy.windows.at(10).final_x == 100 &&
              evaluated.policy.windows.at(10).final_y == 50,
          "first ordinary window starts at work origin");
  require(evaluated.policy.windows.at(20).final_x == 132 &&
              evaluated.policy.windows.at(20).final_y == 82,
          "second ordinary window uses 32-pixel cascade");
  require(evaluated.policy.windows.at(10).stacking == 0 &&
              evaluated.policy.windows.at(20).stacking == 1 &&
              evaluated.policy.windows.at(20).focused,
          "map order determines contiguous stack and focus fallback");
  require(evaluated.policy.hash == UINT64_C(0xd52cb7f4f5d3b4d0),
          "canonical policy payload hash matches known vector");
  const auto payload = encode_policy_window_state(evaluated.policy.windows.at(10));
  require(payload.size() == 64 && payload[0] == 10 && payload[12] == 0 &&
              payload[16] == 7 && payload[24] == 100 && payload[28] == 50 &&
              payload[32] == 200 && payload[36] == 100 && payload[44] == 1 &&
              payload[46] == 1 && payload[48] == 1 && payload[49] == 0 &&
              payload[50] == 1 && payload[51] == 1 && payload[54] == 1 &&
              payload[55] == 1 && payload[63] == 0,
          "canonical state bytes match the exact policy wire payload layout");

  RawState reversed;
  reversed.complete = true; reversed.has_context = true; reversed.context = context();
  reversed.windows.emplace(20, raw.windows.at(20));
  reversed.windows.emplace(10, raw.windows.at(10));
  const auto again = evaluate(reversed, 3);
  require(again && evaluated.policy.hash == again.policy.hash &&
              evaluated.policy.output_order == again.policy.output_order,
          "input insertion order does not affect policy or hash");

  raw.windows.at(10).requested_width = 640;
  raw.windows.at(10).requested_height = 480;
  raw.windows.at(20).requested_width = 638;
  raw.windows.at(20).requested_height = 478;
  const auto wrapped = evaluate(raw, 4);
  require(wrapped && wrapped.policy.windows.at(10).final_x == 100 &&
              wrapped.policy.windows.at(10).final_y == 50 &&
              wrapped.policy.windows.at(20).final_x == 102 &&
              wrapped.policy.windows.at(20).final_y == 52,
          "full-size and narrow spans wrap cascade offsets deterministically");
}

void transients_override_and_states() {
  auto raw = state();
  auto dialog = window(30, 3);
  dialog.window_type = WindowType::Dialog; dialog.transient_for = 10;
  dialog.requested_width = 100; dialog.requested_height = 60;
  dialog.focus_serial = 4;
  raw.windows.emplace(30, dialog);
  auto override = window(40, 4);
  override.override_redirect = true; override.requested_x = -20;
  override.requested_y = 700; override.requested_width = 900;
  override.requested_height = 20; override.fullscreen_requested = true;
  override.focus_serial = 99; override.transient_for = 10;
  raw.windows.emplace(40, override);
  auto nested = window(50, 5);
  nested.window_type = WindowType::Dialog; nested.transient_for = 30;
  nested.requested_width = 40; nested.requested_height = 20;
  raw.windows.emplace(50, nested);
  const auto evaluated = evaluate(raw, 1);
  require(evaluated && evaluated.policy.windows.at(30).final_x == 150 &&
              evaluated.policy.windows.at(30).final_y == 70,
          "transient is centered over its parent");
  require(evaluated.policy.windows.at(30).stacking == 1 &&
              evaluated.policy.windows.at(30).focused,
          "transient is above parent and can take focus");
  const auto& bypass = evaluated.policy.windows.at(40);
  require(!bypass.managed && bypass.final_x == -20 && bypass.final_y == 700 &&
              bypass.final_width == 900 && !bypass.focused &&
              bypass.stacking == 4 && !bypass.decoration_eligible,
          "override-redirect preserves geometry and occupies top band without focus");
  require(evaluated.policy.windows.at(50).stacking == 2 &&
              evaluated.policy.windows.at(50).final_x == 180 &&
              evaluated.policy.windows.at(50).final_y == 90,
          "nested transients resolve geometry and depth-first stacking");

  raw.windows.at(10).wants_map = false; raw.windows.at(10).map_serial = 99;
  raw.windows.at(20).fullscreen_requested = true;
  raw.windows.at(20).maximized_requested = true;
  raw.windows.at(20).minimized_requested = true;
  const auto hidden = evaluate(raw, 2);
  require(hidden && !hidden.policy.windows.at(30).visible &&
              hidden.policy.windows.at(30).stacking == -1,
          "hidden transient parent hides descendants");
  require(hidden.policy.windows.at(20).applied_state == AppliedState::Minimized &&
              !hidden.policy.windows.at(20).visible &&
              hidden.policy.windows.at(20).fullscreen_eligible == TriState::False,
          "minimized takes precedence over fullscreen and maximize");

  raw.windows.at(20).minimized_requested = false;
  raw.windows.at(10).wants_map = true;
  raw.windows.at(10).map_serial = 99;
  const auto fullscreen = evaluate(raw, 3);
  require(fullscreen && fullscreen.policy.windows.at(20).applied_state ==
                            AppliedState::Fullscreen &&
              fullscreen.policy.windows.at(20).final_x == 100 &&
              fullscreen.policy.windows.at(20).final_width == 640 &&
              !fullscreen.policy.windows.at(20).decoration_eligible &&
              fullscreen.policy.windows.at(20).fullscreen_eligible == TriState::True &&
              fullscreen.policy.windows.at(20).direct_scanout_eligible ==
                  TriState::False &&
              fullscreen.policy.windows.at(20).stacking >
                  fullscreen.policy.windows.at(10).stacking,
          "fullscreen beats maximize, fills the work area, and tops managed windows");
}

void decoration_and_focus() {
  auto raw = state();
  raw.windows.at(10).decoration_preference = DecorationPreference::False;
  raw.windows.at(10).focus_serial = 20;
  raw.windows.at(20).window_type = WindowType::Utility;
  auto evaluated = evaluate(raw, 1);
  require(evaluated && !evaluated.policy.windows.at(10).decoration_eligible &&
              !evaluated.policy.windows.at(20).decoration_eligible &&
              evaluated.policy.windows.at(10).focused,
          "explicit decoration false and focus serial are honored");
  raw.windows.at(20).decoration_preference = DecorationPreference::True;
  raw.windows.at(10).minimized_requested = true;
  evaluated = evaluate(raw, 2);
  require(evaluated && evaluated.policy.windows.at(20).decoration_eligible &&
              evaluated.policy.windows.at(20).focused,
          "utility explicit decoration and minimized focus fallback work");
  raw.windows.at(10).wants_map = false; raw.windows.at(10).map_serial = 0;
  raw.windows.at(20).wants_map = false; raw.windows.at(20).map_serial = 0;
  evaluated = evaluate(raw, 3);
  require(evaluated && !evaluated.policy.windows.at(10).focused &&
              !evaluated.policy.windows.at(20).focused &&
              evaluated.policy.output_order == std::vector<std::uint32_t>({10, 20}),
          "no visible candidate leaves focus empty and hidden output ID-sorted");
}

void lifecycle_geometry() {
  auto raw = state();
  raw.windows.at(10).geometry_serial = 1;
  raw.windows.at(10).requested_x = 500;
  raw.windows.at(10).requested_y = 400;
  auto evaluated = evaluate(raw, 1);
  require(evaluated && evaluated.policy.windows.at(10).final_x == 500 &&
              evaluated.policy.windows.at(10).final_y == 400,
          "nonzero geometry serial preserves requested managed position");

  raw.windows.at(10).requested_x = -500;
  raw.windows.at(10).requested_y = 1000;
  raw.windows.at(10).requested_width = 900;
  raw.windows.at(10).requested_height = 900;
  evaluated = evaluate(raw, 2);
  require(evaluated && evaluated.policy.windows.at(10).final_x == 100 &&
              evaluated.policy.windows.at(10).final_y == 50 &&
              evaluated.policy.windows.at(10).final_width == 640 &&
              evaluated.policy.windows.at(10).final_height == 480,
          "persisted geometry clamps dimensions and rectangle into work area");

  raw.windows.at(10).fullscreen_requested = true;
  raw.windows.at(10).requested_x = 300;
  raw.windows.at(10).requested_width = 200;
  evaluated = evaluate(raw, 3);
  require(evaluated && evaluated.policy.windows.at(10).final_x == 100 &&
              evaluated.policy.windows.at(10).final_width == 640 &&
              evaluated.policy.windows.at(10).focused &&
              evaluated.policy.windows.at(10).direct_scanout_eligible ==
                  TriState::Unknown,
          "fullscreen overrides geometry and retains honest scanout uncertainty");

  raw = state();
  auto transient = window(30, 3);
  transient.transient_for = 10; transient.geometry_serial = 9;
  transient.requested_x = 500; transient.requested_y = 400;
  transient.requested_width = 100; transient.requested_height = 60;
  raw.windows.emplace(30, transient);
  evaluated = evaluate(raw, 4);
  require(evaluated && evaluated.policy.windows.at(30).final_x == 150 &&
              evaluated.policy.windows.at(30).final_y == 70,
          "transient centering overrides lifecycle geometry intent");

  raw = state();
  raw.windows.at(20).geometry_serial = 7;
  raw.windows.at(20).requested_x = 333;
  raw.windows.erase(10);
  evaluated = evaluate(raw, 5);
  require(evaluated && evaluated.policy.windows.at(20).final_x == 333,
          "persisted placement does not recascade after another window is removed");
}

RawState stack_state() {
  auto raw = state();
  raw.windows.emplace(30, window(30, 3));
  return raw;
}

void lifecycle_restacking() {
  auto raw = stack_state();
  raw.windows.at(10).stack_serial = 1;
  raw.windows.at(10).stack_mode = StackMode::Above;
  auto evaluated = evaluate(raw, 1);
  require(evaluated && evaluated.policy.output_order ==
                           std::vector<std::uint32_t>({20, 30, 10}),
          "Above without sibling moves to band top");

  raw = stack_state();
  raw.windows.at(30).stack_serial = 1;
  raw.windows.at(30).stack_mode = StackMode::Below;
  evaluated = evaluate(raw, 1);
  require(evaluated && evaluated.policy.output_order ==
                           std::vector<std::uint32_t>({30, 10, 20}),
          "Below without sibling moves to band bottom");

  raw = stack_state();
  raw.windows.at(10).stack_serial = 1;
  raw.windows.at(10).stack_sibling = 20;
  raw.windows.at(10).stack_mode = StackMode::Above;
  evaluated = evaluate(raw, 1);
  require(evaluated && evaluated.policy.output_order ==
                           std::vector<std::uint32_t>({20, 10, 30}),
          "Above sibling inserts immediately above in band");

  raw.windows.at(10).stack_mode = StackMode::Below;
  evaluated = evaluate(raw, 2);
  require(evaluated && evaluated.policy.output_order ==
                           std::vector<std::uint32_t>({10, 20, 30}),
          "Below sibling inserts immediately below in band");

  raw = stack_state();
  raw.windows.at(10).stack_serial = 2;
  raw.windows.at(10).stack_mode = StackMode::Above;
  raw.windows.at(30).stack_serial = 1;
  raw.windows.at(30).stack_mode = StackMode::Below;
  evaluated = evaluate(raw, 3);
  require(evaluated && evaluated.policy.output_order ==
                           std::vector<std::uint32_t>({30, 20, 10}),
          "restack operations apply in ascending stack serial order");

  raw = stack_state();
  raw.windows.at(10).override_redirect = true;
  raw.windows.at(20).override_redirect = true;
  raw.windows.at(10).stack_serial = 1;
  raw.windows.at(10).stack_mode = StackMode::Above;
  evaluated = evaluate(raw, 3);
  require(evaluated && evaluated.policy.output_order ==
                           std::vector<std::uint32_t>({30, 20, 10}),
          "override-redirect restacking remains within the top band");

  raw = stack_state();
  raw.windows.at(10).wants_map = false; raw.windows.at(10).map_serial = 0;
  raw.windows.at(10).stack_serial = 1;
  raw.windows.at(10).stack_mode = StackMode::Above;
  evaluated = evaluate(raw, 4);
  require(evaluated && evaluated.policy.output_order.back() == 10 &&
              evaluated.policy.windows.at(10).stacking == -1,
          "hidden restack intent preserves hidden output rules");
  raw.windows.at(10).wants_map = true; raw.windows.at(10).map_serial = 1;
  evaluated = evaluate(raw, 5);
  require(evaluated && evaluated.policy.output_order ==
                           std::vector<std::uint32_t>({20, 30, 10}),
          "hidden restack intent takes effect when window maps");
}

void lifecycle_restack_validation() {
  auto raw = stack_state();
  raw.windows.at(10).stack_mode = StackMode::Above;
  require(evaluate(raw, 1).error == EvaluationError::InvalidWindow,
          "stack mode without serial is rejected");
  raw = stack_state(); raw.windows.at(10).stack_serial = 1;
  require(evaluate(raw, 1).error == EvaluationError::InvalidWindow,
          "stack serial without mode is rejected");
  raw = stack_state(); raw.windows.at(10).stack_serial = 1;
  raw.windows.at(10).stack_mode = StackMode::Above;
  raw.windows.at(10).stack_sibling = 999;
  require(evaluate(raw, 1).error == EvaluationError::UnknownReference,
          "missing stack sibling is rejected as unknown reference");
  raw = stack_state(); raw.windows.at(10).stack_serial = 1;
  raw.windows.at(10).stack_mode = StackMode::Above;
  raw.windows.at(10).stack_sibling = 20;
  raw.windows.at(20).override_redirect = true;
  require(evaluate(raw, 1).error == EvaluationError::UnsupportedMetadata,
          "cross-band sibling is rejected");
  raw = stack_state(); raw.windows.at(10).transient_for = 20;
  raw.windows.at(10).stack_serial = 1;
  raw.windows.at(10).stack_mode = StackMode::Above;
  require(evaluate(raw, 1).error == EvaluationError::UnsupportedMetadata,
          "transient restack is rejected");
  raw = stack_state(); raw.windows.at(30).transient_for = 20;
  raw.windows.at(10).stack_serial = 1;
  raw.windows.at(10).stack_mode = StackMode::Above;
  raw.windows.at(10).stack_sibling = 30;
  require(evaluate(raw, 1).error == EvaluationError::UnsupportedMetadata,
          "transient stack sibling is rejected");
}

void transactions() {
  Transaction transaction;
  require(!transaction.upsert(context()), "incremental context before bootstrap is rejected");
  require(transaction.begin_snapshot() && transaction.upsert(context()) &&
              transaction.upsert(window(10, 1)) && transaction.end_snapshot(),
          "complete replacement snapshot is staged");
  auto accepted = transaction.commit(1);
  require(accepted && transaction.committed_policy().generation == 1,
          "valid initial snapshot commits atomically");
  const auto original_hash = transaction.committed_policy().hash;

  auto bad = window(20, 1);
  require(transaction.upsert(bad), "incremental update is staged");
  auto rejected = transaction.commit(2);
  require(rejected.error == EvaluationError::InvalidWindow &&
              transaction.committed_policy().hash == original_hash &&
              transaction.pending().windows.contains(20),
          "rejection preserves committed policy and corrective pending state");
  bad.creation_serial = 2;
  require(transaction.upsert(bad) && transaction.commit(2),
          "corrective update can commit");
  require(transaction.committed_raw().producer_generation == 2 &&
              transaction.commit(1).error == EvaluationError::InvalidWindow,
          "producer generation is recorded and cannot decrease");

  require(transaction.begin_snapshot() && transaction.upsert(context()) &&
              transaction.upsert(window(30, 3)) && transaction.abort_snapshot(),
          "snapshot abort restores prior pending state");
  require(transaction.pending().windows.contains(10) &&
              transaction.pending().windows.contains(20) &&
              !transaction.pending().windows.contains(30),
          "aborted replacement is invisible");

  require(transaction.begin_snapshot() && transaction.upsert(context()) &&
              transaction.upsert(window(30, 3)) && transaction.end_snapshot(),
          "replacement snapshot can finish after bootstrap");
  require(transaction.commit(3) && transaction.committed_raw().windows.size() == 1 &&
              transaction.committed_raw().windows.contains(30),
          "accepted replacement atomically replaces prior raw state");

  const auto before_preflight = transaction.committed_policy().hash;
  auto changed = transaction.pending().windows.at(30);
  changed.focus_serial = 9;
  require(transaction.upsert(changed), "preflight update is staged");
  rejected = transaction.commit(4, [](const PolicyState&) { return false; });
  require(rejected.error == EvaluationError::OutputFailure &&
              transaction.committed_policy().hash == before_preflight,
          "output preflight failure prevents promotion");
  transaction.disconnect();
  require(!transaction.pending().complete && !transaction.committed_raw().complete &&
              transaction.committed_policy().windows.empty(),
          "disconnect clears all transaction state");
}

}  // namespace

int main() {
  validation();
  cascade_and_determinism();
  transients_override_and_states();
  decoration_and_focus();
  lifecycle_geometry();
  lifecycle_restacking();
  lifecycle_restack_validation();
  transactions();
  return 0;
}
