#include "glasswyrmd/cursor_presenter.hpp"
#include "glasswyrmd/output_scene_projection.hpp"
#include "helpers/test_support.hpp"

#include <fcntl.h>
#include <memory>
#include <string>

namespace {

std::shared_ptr<const glasswyrm::input::CursorImage> cursor(
    const std::uint16_t glyph) {
  std::string error;
  auto image = glasswyrm::input::make_glyph_cursor(
      {glasswyrm::input::CursorFontIdentity::Cursor,
       glasswyrm::input::CursorFontIdentity::Cursor, glyph,
       static_cast<std::uint16_t>(glyph + 1), {0xffff, 0xffff, 0xffff},
       {0, 0, 0}},
      error);
  gw::test::require(image != nullptr, error.c_str());
  return image;
}

glasswyrm::output::OutputLayout output_layout() {
  using namespace glasswyrm::output;
  constexpr OutputId left{10};
  constexpr OutputId right{20};
  OutputLayout layout;
  layout.primary_output_id = left;
  layout.root_logical_width = 180;
  layout.root_logical_height = 100;
  layout.generation = 7;
  layout.enabled_output_count = 2;
  layout.output_order = {left, right};
  OutputState left_state;
  left_state.output_id = left;
  left_state.enabled = true;
  left_state.logical_width = 100;
  left_state.logical_height = 100;
  left_state.physical_width = 100;
  left_state.physical_height = 100;
  left_state.scale = {1, 1};
  left_state.primary = true;
  left_state.generation = layout.generation;
  layout.states.emplace(left, left_state);
  OutputState right_state;
  right_state.output_id = right;
  right_state.enabled = true;
  right_state.logical_x = 100;
  right_state.logical_width = 80;
  right_state.logical_height = 100;
  right_state.physical_width = 100;
  right_state.physical_height = 125;
  right_state.scale = {5, 4};
  right_state.generation = layout.generation;
  layout.states.emplace(right, right_state);
  return layout;
}

}  // namespace

int main() {
  using namespace glasswyrm::server;
  using gw::test::require;
  CursorPresenter presenter;
  const auto pointer = cursor(glasswyrm::input::kCursorGlyphLeftPointer);
  CompositorCursorSubmission initial;
  std::string error;
  require(presenter.needs_update(pointer, 20, 30, true) &&
              presenter.prepare(pointer, 20, 30, true, initial, error),
          error.c_str());
  require(initial.surface.surface_id == CursorPresenter::kSurfaceId &&
              initial.surface.x11_window_id == 0 &&
              initial.surface.parent_surface_id == 0 &&
              initial.surface.output_id == 1 && initial.surface.logical_x == 20 &&
              initial.surface.logical_y == 30 && initial.surface.visible == 1 &&
              initial.surface.presentation_flags ==
                  GWIPC_SURFACE_PRESENTATION_CURSOR &&
              initial.surface.stacking > 0 && initial.buffer && initial.damage &&
              initial.buffer->attach.pixel_format ==
                  GWIPC_PIXEL_FORMAT_ARGB8888 &&
              initial.buffer->attach.alpha_semantics ==
                  GWIPC_ALPHA_PREMULTIPLIED &&
              initial.damage->rectangles.size() == 1,
          "initial cursor projection publishes a policy-free ARGB surface");
  const auto seals = ::fcntl(initial.buffer->fd, F_GET_SEALS);
  require((seals & (F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE | F_SEAL_SEAL)) ==
              (F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE | F_SEAL_SEAL),
          "cursor publication is an immutable sealed memfd");
  const auto first_buffer = initial.buffer->attach.buffer_id;
  presenter.accept();
  require(!presenter.needs_update(pointer, 20, 30, true),
          "accepted identical cursor state is coalesced");

  CompositorCursorSubmission rejected;
  require(presenter.prepare(pointer, 22, 32, true, rejected, error) &&
              !rejected.buffer && !rejected.damage,
          "position-only cursor update reuses the accepted buffer");
  presenter.reject();
  require(presenter.needs_update(pointer, 22, 32, true),
          "rejected cursor state remains dirty for replay");

  CompositorCursorSubmission retried;
  require(presenter.prepare(pointer, 22, 32, true, retried, error, true) &&
              retried.buffer && retried.damage,
          "forced rejection retry republishes immutable cursor content");
  presenter.accept();

  CompositorCursorSubmission moved;
  require(presenter.needs_update(pointer, 25, 35, true) &&
              presenter.prepare(pointer, 25, 35, true, moved, error) &&
              !moved.buffer && !moved.damage && moved.surface.logical_x == 25 &&
              moved.surface.logical_y == 35,
          "movement reuses the accepted cursor buffer");
  presenter.accept();

  CompositorCursorSubmission hidden;
  require(presenter.prepare(pointer, 25, 35, false, hidden, error) &&
              hidden.surface.visible == 0 && !hidden.buffer,
          "visibility changes reuse the accepted cursor buffer");
  presenter.accept();

  const auto resize = cursor(glasswyrm::input::kCursorGlyphBottomRightCorner);
  CompositorCursorSubmission replaced;
  require(presenter.prepare(resize, 40, 50, true, replaced, error) &&
              replaced.buffer && replaced.damage &&
              replaced.buffer->attach.buffer_id != first_buffer &&
              replaced.surface.logical_x == 26 &&
              replaced.surface.logical_y == 36,
          "effective image changes replace content and apply the hotspot");
  presenter.accept();
  require(presenter.release(first_buffer, GWIPC_BUFFER_RELEASE_REPLACED),
          "replaced cursor buffers remain owned until consumer release");
  const auto disconnected_buffer = replaced.buffer->attach.buffer_id;
  presenter.peer_disconnected();
  require(presenter.needs_update(resize, 40, 50, true),
          "peer disconnect invalidates the accepted cursor publication");
  CompositorCursorSubmission reconnected;
  require(presenter.prepare(resize, 40, 50, true, reconnected, error) &&
              reconnected.buffer && reconnected.damage &&
              reconnected.buffer->attach.buffer_id != disconnected_buffer,
          "reconnect publishes a fresh cursor buffer for the new peer");
  require(!presenter.release(disconnected_buffer,
                             GWIPC_BUFFER_RELEASE_REPLACED),
          "disconnect drops buffers that the departed peer cannot release");
  presenter.accept();

  const auto layout = output_layout();
  CursorPresenter scaled_presenter;
  CompositorCursorSubmission scaled;
  const auto scale = cursor_buffer_scale(layout, 120, 30);
  require(scale == 2 &&
              scaled_presenter.prepare(pointer, 120, 30, true, scaled, error,
                                       false, scale) &&
              scaled.surface.logical_width == pointer->width &&
              scaled.surface.logical_height == pointer->height &&
              scaled.surface.scale_numerator == 2 && scaled.buffer &&
              scaled.buffer->attach.width == pointer->width * 2 &&
              scaled.buffer->attach.height == pointer->height * 2 &&
              scaled.pointer_x == 120 && scaled.pointer_y == 30,
          "fractional output scale selects a scaled immutable cursor buffer");
  require(populate_cursor_output_state(scaled, layout) &&
              scaled.surface.output_id == 20 &&
              scaled.surface_output.state.surface_id ==
                  CursorPresenter::kSurfaceId &&
              scaled.surface_output.state.primary_output_id == 20 &&
              scaled.surface_output.state.preferred_scale_numerator == 5 &&
              scaled.surface_output.state.preferred_scale_denominator == 4 &&
              scaled.surface_output.state.client_buffer_scale == 2 &&
              scaled.surface_output.state.scale_mode ==
                  GWIPC_SURFACE_SCALE_SCALED_PIXMAP &&
              scaled.surface_output.state.layout_generation == 7 &&
              scaled.surface_output.output_ids ==
                  std::vector<std::uint64_t>({20}),
          "cursor output state follows the pointer and owns exact membership");
  scaled_presenter.accept();
  return 0;
}
