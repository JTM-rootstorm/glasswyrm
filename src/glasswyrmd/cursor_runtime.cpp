#include "glasswyrmd/server_runtime.hpp"

#include "glasswyrmd/output_scene_projection.hpp"

#include "input/input_router.hpp"

#include <cstdio>

namespace glasswyrm::server {

bool ServerRuntime::service_cursor() {
  bool real_provider = false;
  bool visible = false;
#if GW_HAS_LIBINPUT_BACKEND
  real_provider = real_input_ != nullptr;
  visible = real_provider && real_input_->active();
#endif
  const bool synthetic_provider =
      server_.options_.output_model && input_peer_ != nullptr;
  if (!cursor_presenter_ || (!real_provider && !synthetic_provider) ||
      !lifecycle_ || !bridge_->ready())
    return true;
  if (!real_provider)
    visible = input_peer_->connected();

  const auto image = current_cursor_image();
  if (!image) {
    std::fprintf(stderr, "glasswyrmd: effective cursor image is unavailable\n");
    return false;
  }
  const auto* output_layout = server_.options_.output_model
                                  ? bridge_->output_layout()
                                  : nullptr;
  const auto buffer_scale =
      output_layout
          ? cursor_buffer_scale(*output_layout, input_state_.pointer_x(),
                                input_state_.pointer_y())
          : 1U;
#if GW_HAS_LIBINPUT_BACKEND
  const bool interactive_override =
      visible && interactive_policy_ &&
      ((interactive_policy_->cursor() ==
            glasswyrm::wm::InteractionCursor::FleurMove &&
        image == move_cursor_) ||
       (interactive_policy_->cursor() ==
            glasswyrm::wm::InteractionCursor::BottomRightResize &&
        image == resize_cursor_));
#endif
  if (!cursor_dirty_ &&
      !cursor_presenter_->needs_update(
          image, input_state_.pointer_x(), input_state_.pointer_y(), visible,
          buffer_scale))
    return true;
  if (lifecycle_->phase() != CoordinatorPhase::Idle ||
      !bridge_->transaction_idle() || cursor_presenter_->in_flight() ||
      (content_presenter_ && content_presenter_->frame_in_flight()))
    return true;
  if (!cursor_presenter_->needs_update(
          image, input_state_.pointer_x(), input_state_.pointer_y(), visible,
          buffer_scale)) {
    cursor_dirty_ = false;
    cursor_force_buffer_ = false;
#if GW_HAS_LIBINPUT_BACKEND
    if (interactive_override)
      complete_interactive_cursor_publication();
#endif
    return true;
  }
  CompositorCursorSubmission submission;
  std::string error;
  if (!cursor_presenter_->prepare(
          image, input_state_.pointer_x(), input_state_.pointer_y(), visible,
          submission, error, cursor_force_buffer_, buffer_scale)) {
    std::fprintf(stderr, "glasswyrmd: cursor preparation failed: %s\n",
                 error.c_str());
    return false;
  }
  if (!bridge_->submit_cursor(submission, next_compositor_commit_++,
                              next_compositor_generation_++, error)) {
    cursor_presenter_->reject();
    std::fprintf(stderr, "glasswyrmd: cursor submission failed: %s\n",
                 error.c_str());
    return false;
  }
  cursor_dirty_ = false;
  cursor_force_buffer_ = false;
#if GW_HAS_LIBINPUT_BACKEND
  cursor_submission_interactive_ = interactive_override;
#endif
  cursor_submission_diagnostic_ = PendingCursorDiagnostic{
      image->kind, submission.surface.logical_x, submission.surface.logical_y,
      submission.surface.visible != 0, submission.buffer.has_value()};
  return true;
}

std::shared_ptr<const glasswyrm::input::CursorImage>
ServerRuntime::current_cursor_image() const noexcept {
#if GW_HAS_LIBINPUT_BACKEND
  if (interactive_policy_) {
    if (interactive_policy_->cursor() ==
        glasswyrm::wm::InteractionCursor::FleurMove)
      return move_cursor_;
    if (interactive_policy_->cursor() ==
        glasswyrm::wm::InteractionCursor::BottomRightResize)
      return resize_cursor_;
  }
#endif
  if (const auto& grab = server_.state_.grabs().pointer_grab(); grab) {
    if (grab->cursor != 0)
      if (const auto* cursor =
              server_.state_.resources().find_cursor(grab->cursor))
        return cursor->image;
    if (grab->cursor_image)
      return grab->cursor_image;
  }
  const auto target = glasswyrm::input::hit_test_deepest_viewable(
      server_.state_.resources(), input_state_.pointer_x(),
      input_state_.pointer_y());
  return server_.state_.resources().effective_cursor(target);
}

}  // namespace glasswyrm::server
