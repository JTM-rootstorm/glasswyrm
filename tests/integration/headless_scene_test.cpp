#include <cassert>

#include <glasswyrm/backends/headless.hpp>

int main() {
  glasswyrm::backends::HeadlessBackend backend;

  assert(backend.scene().is_headless());
  assert(backend.scene().frame_number() == 0);
  assert(backend.scene().output().physical_size.width == 1280);
  assert(backend.scene().output().physical_size.height == 720);

  backend.scene().mark_frame_presented();
  assert(backend.scene().frame_number() == 1);

  return 0;
}
