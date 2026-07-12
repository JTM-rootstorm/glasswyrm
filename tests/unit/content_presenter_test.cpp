#include "glasswyrmd/content_presenter.hpp"
#include "glasswyrmd/server_state.hpp"

int main() {
  using namespace glasswyrm::server;
  ServerState state;
  const WindowCreateSpec spec{0x200001, 1, 0, 0, 32, 24, 0, 24,
                              WindowClass::InputOutput, 0, 0, 0, {}};
  if (state.resources().create_window(1, 0x200000, 0x1fffff, spec) !=
      CreateWindowStatus::Success)
    return 1;
  auto* window = state.resources().find_window(spec.xid);
  window->map_requested = true;
  window->map_state = MapState::Viewable;
  window->policy_visible = true;
  window->creation_serial = 1;
  auto snapshot = state.lifecycle_snapshot();
  auto& projected = snapshot.windows.at(spec.xid);
  projected.applied_width = 32;
  projected.applied_height = 24;
  projected.policy_visible = true;
  ContentPresenter presenter;
  CompositorSnapshotSubmission submission{2, 2, {}, {}, {}, {}};
  if (!presenter.prepare_lifecycle(snapshot, state.resources(), submission) ||
      submission.buffers.size() != 1 || submission.damages.size() != 1)
    return 2;
  presenter.accept_lifecycle(snapshot, state.resources());
  if (!window->storage || presenter.frame_in_flight()) return 3;
  window->storage->at(2, 3) = 0xff102030U;
  presenter.damage(spec.xid, {2, 3, 1, 1});
  CompositorContentSubmission content;
  if (!presenter.prepare_content(snapshot, state.resources(), 3, 3, content) ||
      content.damages.size() != 1)
    return 4;
  presenter.damage(spec.xid, {4, 5, 1, 1});
  presenter.accept_content();
  if (!presenter.has_pending_damage()) return 5;
  return 0;
}
