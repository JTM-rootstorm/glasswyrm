#include <cassert>

#include <glasswyrm/render/software_renderer.hpp>

int main() {
  glasswyrm::render::SoftwareFramebuffer framebuffer(4, 2);
  const glasswyrm::render::Pixel blue{.r = 0, .g = 0, .b = 255, .a = 255};

  framebuffer.clear(blue);

  assert(framebuffer.width() == 4);
  assert(framebuffer.height() == 2);
  assert(framebuffer.pixels().size() == 8);

  for (const auto& pixel : framebuffer.pixels()) {
    assert(pixel.r == blue.r);
    assert(pixel.g == blue.g);
    assert(pixel.b == blue.b);
    assert(pixel.a == blue.a);
  }

  return 0;
}
