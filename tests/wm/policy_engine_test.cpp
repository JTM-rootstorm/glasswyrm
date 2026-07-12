#include "wm/policy_engine.hpp"
#include "wm/transaction.hpp"

#include <cstdio>
#include <cstdlib>

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
  require(evaluated.policy.hash == UINT64_C(0x79ddf2e26c5784d8),
          "canonical policy payload hash matches known vector");

  RawState reversed;
  reversed.complete = true; reversed.has_context = true; reversed.context = context();
  reversed.windows.emplace(20, raw.windows.at(20));
  reversed.windows.emplace(10, raw.windows.at(10));
  const auto again = evaluate(reversed, 3);
  require(again && evaluated.policy.hash == again.policy.hash &&
              evaluated.policy.output_order == again.policy.output_order,
          "input insertion order does not affect policy or hash");
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
  override.focus_serial = 99;
  raw.windows.emplace(40, override);
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
              bypass.stacking == 3 && !bypass.decoration_eligible,
          "override-redirect preserves geometry and occupies top band without focus");

  raw.windows.at(10).wants_map = false; raw.windows.at(10).map_serial = 0;
  raw.windows.at(20).fullscreen_requested = true;
  raw.windows.at(20).maximized_requested = true;
  raw.windows.at(20).minimized_requested = true;
  const auto hidden = evaluate(raw, 2);
  require(hidden && !hidden.policy.windows.at(30).visible &&
              hidden.policy.windows.at(30).stacking == -1,
          "hidden transient parent hides descendants");
  require(hidden.policy.windows.at(20).applied_state == AppliedState::Minimized &&
              !hidden.policy.windows.at(20).visible,
          "minimized takes precedence over fullscreen and maximize");
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

  const auto before_preflight = transaction.committed_policy().hash;
  auto changed = transaction.pending().windows.at(20);
  changed.focus_serial = 9;
  require(transaction.upsert(changed), "preflight update is staged");
  rejected = transaction.commit(3, [](const PolicyState&) { return false; });
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
  transactions();
  return 0;
}
