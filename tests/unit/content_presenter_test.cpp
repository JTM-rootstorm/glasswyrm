#include "glasswyrmd/content_presenter.hpp"
#include "glasswyrmd/server_state.hpp"

#include <cerrno>
#include <unistd.h>

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
  const auto original_storage = window->storage;
  const auto original_buffer =
      presenter.buffers().current(spec.xid)->buffer_id();
  auto resized = snapshot;
  resized.windows.at(spec.xid).applied_width = 40;
  CompositorSnapshotSubmission resize_submission{3, 3, {}, {}, {}, {}};
  if (!presenter.prepare_lifecycle(resized, state.resources(),
                                   resize_submission) ||
      resize_submission.buffers.size() != 1)
    return 6;
  window->storage->at(2, 3) = 0xff405060U;
  presenter.damage(spec.xid, {2, 3, 1, 1});
  presenter.reject_lifecycle();
  if (window->storage != original_storage ||
      presenter.buffers().current(spec.xid)->buffer_id() != original_buffer ||
      !presenter.has_pending_damage())
    return 7;
  CompositorSnapshotSubmission replay{4, 4, {}, {}, {}, {}};
  if (!presenter.prepare_replay(snapshot, state.resources(), replay) ||
      replay.buffers.size() != 1 ||
      replay.buffers.front().attach.buffer_id == original_buffer)
    return 8;
  presenter.accept_lifecycle(snapshot, state.resources());
  if (!presenter.release(original_buffer, GWIPC_BUFFER_RELEASE_REPLACED))
    return 9;
  window->storage->at(2, 3) = 0xff102030U;
  presenter.damage(spec.xid, {2, 3, 1, 1});
  CompositorContentSubmission content;
  if (!presenter.prepare_content(snapshot, state.resources(), 5, 5, content) ||
      content.damages.size() != 1)
    return 4;
  presenter.damage(spec.xid, {4, 5, 1, 1});
  presenter.accept_content();
  if (!presenter.has_pending_damage()) return 5;

  ServerState scaled_state;
  WindowCreateSpec scaled_spec = spec;
  scaled_spec.xid = 0x200002;
  scaled_spec.width = 10;
  scaled_spec.height = 5;
  if (scaled_state.resources().create_window(
          1, 0x200000, 0x1fffff, scaled_spec) !=
      CreateWindowStatus::Success)
    return 13;
  auto* scaled_window = scaled_state.resources().find_window(scaled_spec.xid);
  scaled_window->map_requested = true;
  scaled_window->map_state = MapState::Viewable;
  scaled_window->policy_visible = true;
  scaled_window->creation_serial = 1;
  auto scaled_pixels = PixelStorage::create(20, 10);
  if (!scaled_pixels) return 14;
  scaled_window->scale.accepted_buffer_scale = 2;
  scaled_window->scale.presentation =
      WindowScalePresentationState::ScaleAwareActive;
  scaled_window->scale.scaled_pixmap_storage =
      std::make_shared<PixelStorage>(std::move(*scaled_pixels));
  auto scaled_snapshot = scaled_state.lifecycle_snapshot();
  auto& scaled_projected = scaled_snapshot.windows.at(scaled_spec.xid);
  scaled_projected.applied_width = 10;
  scaled_projected.applied_height = 5;
  scaled_projected.policy_visible = true;
  ContentPresenter scaled_presenter;
  CompositorSnapshotSubmission scaled_submission{7, 7, {}, {}, {}, {}};
  if (!scaled_presenter.prepare_lifecycle(
          scaled_snapshot, scaled_state.resources(), scaled_submission) ||
      scaled_submission.buffers.size() != 1 ||
      scaled_submission.buffers[0].attach.width != 20 ||
      scaled_submission.buffers[0].attach.height != 10 ||
      scaled_submission.damages[0].rectangles[0].width != 10 ||
      scaled_submission.damages[0].rectangles[0].height != 5)
    return 15;
  scaled_presenter.accept_lifecycle(scaled_snapshot,
                                    scaled_state.resources());
  scaled_presenter.damage_scaled(scaled_spec.xid, {1, 0, 3, 3},
                                 {3, 1, 4, 5});
  CompositorContentSubmission scaled_content;
  if (!scaled_presenter.prepare_content(
          scaled_snapshot, scaled_state.resources(), 8, 8, scaled_content) ||
      !scaled_content.buffers.empty() || scaled_content.damages.size() != 1 ||
      scaled_content.damages[0].rectangles[0].x != 1 ||
      scaled_content.damages[0].rectangles[0].width != 3)
    return 16;
  scaled_presenter.accept_content();
  scaled_window->scale.presentation =
      WindowScalePresentationState::ScaleAwareAwaitingPixmap;
  scaled_window->scale.scaled_pixmap_storage.reset();
  scaled_presenter.damage(scaled_spec.xid, {0, 0, 10, 5});
  CompositorContentSubmission legacy_replacement;
  if (!scaled_presenter.prepare_content(scaled_snapshot,
                                        scaled_state.resources(), 9, 9,
                                        legacy_replacement) ||
      legacy_replacement.buffers.size() != 1 ||
      legacy_replacement.buffers[0].attach.width != 10 ||
      legacy_replacement.buffers[0].attach.height != 5)
    return 17;
  scaled_presenter.accept_content();

  ContentPresenter synchronized(GWIPC_SYNCHRONIZATION_EVENTFD);
  CompositorSnapshotSubmission synchronized_submission{6, 6, {}, {}, {}, {}};
  if (!synchronized.prepare_lifecycle(snapshot, state.resources(),
                                      synchronized_submission) ||
      synchronized_submission.buffers.size() != 1 ||
      synchronized_submission.damages.size() != 1 ||
      synchronized_submission.buffers.front().attach.synchronization !=
          GWIPC_SYNCHRONIZATION_EVENTFD ||
      synchronized_submission.buffers.front().synchronization_fd < 0)
    return 10;
  const int synchronization_fd =
      ::dup(synchronized_submission.buffers.front().synchronization_fd);
  if (synchronization_fd < 0) return 11;
  synchronized.cancel_lifecycle_submission();
  std::uint64_t token = 0;
  errno = 0;
  const auto count = ::read(synchronization_fd, &token, sizeof(token));
  (void)::close(synchronization_fd);
  if (count >= 0 || errno != EAGAIN) return 12;
  return 0;
}
