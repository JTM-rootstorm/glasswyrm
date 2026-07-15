#include "glasswyrmd/cursor_presenter.hpp"
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
  presenter.peer_disconnected();
  return 0;
}
