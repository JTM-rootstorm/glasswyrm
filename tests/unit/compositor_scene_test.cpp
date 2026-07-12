#include "compositor/scene.hpp"
#include "tests/helpers/test_support.hpp"

#include <vector>

namespace {
using gw::compositor::Rectangle;
using gw::compositor::SceneModel;
using namespace gw::ipc::wire;

OutputUpsert output(std::uint32_t width = 100, std::uint32_t height = 80) {
  OutputUpsert value;
  value.output_id = 1;
  value.enabled = true;
  value.logical_width = value.physical_pixel_width = width;
  value.logical_height = value.physical_pixel_height = height;
  return value;
}

SurfaceUpsert surface(std::uint64_t id, std::int32_t stacking = 0) {
  SurfaceUpsert value;
  value.surface_id = id;
  value.output_id = 1;
  value.logical_x = 10;
  value.logical_y = 12;
  value.logical_width = 20;
  value.logical_height = 10;
  value.stacking = stacking;
  value.visible = true;
  return value;
}

FrameCommit frame(std::uint64_t id, std::uint64_t generation) {
  return {id, 1, generation, 0};
}

void require(bool condition, const char* message) { gw::test::require(condition, message); }

void test_snapshot_gate_and_atomic_rejection() {
  SceneModel model;
  require(!model.apply(output()), "ordinary mutation is gated before initial snapshot");
  require(!model.commit(frame(1, 1)).accepted(), "commit is gated before snapshot");
  require(model.begin_complete_snapshot(), "complete snapshot begins");
  require(model.apply(output()), "snapshot accepts output");
  auto invalid_reference = surface(3);
  invalid_reference.output_id = 99;
  require(model.apply(invalid_reference), "unresolved reference stages for commit validation");
  require(model.end_complete_snapshot(), "complete snapshot ends");
  const auto rejected = model.commit(frame(1, 1));
  require(rejected.result == FrameResult::RejectedUnknownSurface,
          "bad surface reference rejects frame");
  require(!model.committed().output, "rejection preserves empty committed state");
  require(model.pending().surfaces.contains(3), "rejection preserves pending correction state");
  invalid_reference.output_id = 1;
  require(model.apply(invalid_reference), "pending scene can be corrected");
  require(model.commit(frame(2, 2)).accepted(), "corrected frame promotes atomically");
  require(model.committed().surfaces.contains(3), "accepted scene becomes committed");
}

void test_snapshot_abort_restores_pending() {
  SceneModel model;
  require(model.begin_complete_snapshot() && model.apply(output()) &&
              model.end_complete_snapshot(), "initial snapshot completes");
  require(model.commit(frame(1, 1)).accepted(), "initial frame commits");
  require(model.begin_complete_snapshot(), "replacement snapshot begins from empty");
  require(model.pending().surfaces.empty() && !model.pending().output,
          "replacement snapshot stages an empty scene");
  model.abort_complete_snapshot();
  require(model.pending().output.has_value(), "aborted snapshot restores prior pending state");
  require(model.committed().output.has_value(), "aborted snapshot never changes committed state");
}

void test_validation_and_stacking() {
  SceneModel model;
  require(model.begin_complete_snapshot() && model.apply(output()), "snapshot starts");
  auto bad = surface(9);
  bad.opacity = kOpacityOne + 1;
  require(!model.apply(bad), "opacity above fixed-point one is rejected");
  bad = surface(9);
  bad.clipping = false;
  bad.clip_width = 1;
  require(!model.apply(bad), "disabled clipping requires zero fields");
  require(model.apply(surface(20, -2)) && model.apply(surface(7, 4)) &&
              model.apply(surface(3, 4)), "valid surfaces stage");
  require(model.end_complete_snapshot() && model.commit(frame(1, 1)).accepted(),
          "valid scene commits");
  require(model.stacking_order() == std::vector<std::uint64_t>({20, 3, 7}),
          "stacking is signed with surface-id tie break");
}

void test_old_new_bounds_and_explicit_damage() {
  SceneModel model;
  require(model.begin_complete_snapshot() && model.apply(output()) &&
              model.apply(surface(1)) && model.end_complete_snapshot(),
          "initial scene stages");
  require(model.commit(frame(1, 1)).accepted(), "initial scene commits");
  auto moved = surface(1);
  moved.logical_x = 50;
  require(model.apply(moved), "move stages");
  const auto move = model.commit(frame(2, 2));
  require(move.damage == std::vector<Rectangle>({{10, 12, 20, 10}, {50, 12, 20, 10}}),
          "move damages deterministic old and new bounds");

  require(model.apply(SurfaceDamage{1, {{-4, 2, 10, 3}}}), "surface damage stages");
  const auto content = model.commit(frame(3, 3));
  require(content.damage == std::vector<Rectangle>({{50, 14, 6, 3}}),
          "local damage clips and translates into output coordinates");
}

void test_disconnect_clears_all_state() {
  SceneModel model;
  require(model.begin_complete_snapshot() && model.apply(output()) &&
              model.end_complete_snapshot() && model.commit(frame(1, 1)).accepted(),
          "scene commits before disconnect");
  model.disconnect();
  require(!model.committed().output && !model.pending().output &&
              !model.initial_snapshot_received(), "disconnect clears both scenes and gate");
}
} // namespace

int main() {
  test_snapshot_gate_and_atomic_rejection();
  test_snapshot_abort_restores_pending();
  test_validation_and_stacking();
  test_old_new_bounds_and_explicit_damage();
  test_disconnect_clears_all_state();
}
