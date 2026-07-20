#include "glasswyrmd/compositor_buffer_replay.hpp"
#include "tests/helpers/test_support.hpp"

using glasswyrm::server::CompositorContentSubmission;
using glasswyrm::server::CompositorSnapshotSubmission;
using glasswyrm::server::compositor_buffer_replay::retired_buffer_ids;
using gw::test::require;

namespace {

gwipc_surface_upsert surface(const std::uint64_t id) {
  gwipc_surface_upsert value{};
  value.struct_size = sizeof(value);
  value.surface_id = id;
  return value;
}

CompositorSnapshotSubmission::Buffer buffer(const std::uint64_t surface_id,
                                            const std::uint64_t buffer_id) {
  CompositorSnapshotSubmission::Buffer value;
  value.attach.struct_size = sizeof(value.attach);
  value.attach.surface_id = surface_id;
  value.attach.buffer_id = buffer_id;
  return value;
}

void test_complete_snapshot_retirement() {
  CompositorSnapshotSubmission replay;
  replay.surfaces = {surface(1), surface(2), surface(3)};
  replay.buffers = {buffer(1, 101), buffer(2, 102), buffer(3, 103)};

  CompositorSnapshotSubmission initial;
  initial.surfaces = {surface(4)};
  initial.buffers = {buffer(4, 104)};
  require(retired_buffer_ids(initial, {}).empty(),
          "an initial attachment does not release itself");

  CompositorSnapshotSubmission next;
  next.surfaces = {surface(1), surface(2)};
  next.buffers = {buffer(2, 202)};
  require(retired_buffer_ids(next, replay) ==
              std::set<std::uint64_t>({102, 103}),
          "complete snapshot releases only replaced and removed buffers");

  CompositorSnapshotSubmission retained;
  retained.surfaces = replay.surfaces;
  require(retired_buffer_ids(retained, replay).empty(),
          "retained attachments do not produce releases");
}

void test_incremental_retirement() {
  CompositorSnapshotSubmission replay;
  replay.surfaces = {surface(1), surface(2)};
  replay.buffers = {buffer(1, 101), buffer(2, 102)};

  CompositorContentSubmission initial;
  initial.buffers = {buffer(3, 103)};
  require(retired_buffer_ids(initial, replay).empty(),
          "incremental initial attachment does not release itself");

  CompositorContentSubmission replacement;
  replacement.buffers = {buffer(1, 201), buffer(2, 102)};
  require(retired_buffer_ids(replacement, replay) ==
              std::set<std::uint64_t>({101}),
          "incremental update releases only a replaced prior buffer");
}

}  // namespace

int main() {
  test_complete_snapshot_retirement();
  test_incremental_retirement();
}
