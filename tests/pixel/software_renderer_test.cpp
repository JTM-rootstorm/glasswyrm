#include "render/software/blend.hpp"
#include "render/software/renderer.hpp"
#include "tests/helpers/test_support.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

#define GW_EXPECT(...) ::gw::test::require((__VA_ARGS__), #__VA_ARGS__)
#define GW_EXPECT_EQ(actual, ...)                                                \
  ::gw::test::require((actual) == (__VA_ARGS__), #actual " == " #__VA_ARGS__)

using gw::render::software::Pixel;

int main() {
  using namespace gw::render::software;
  GW_EXPECT_EQ(unpack_xrgb8888(0x00123456U), Pixel{0x12, 0x34, 0x56, 255});
  GW_EXPECT_EQ(unpack_argb8888(0x80102030U), Pixel{0x10, 0x20, 0x30, 0x80});
  GW_EXPECT_EQ(pack_xrgb8888(Pixel{1, 2, 3, 0}), 0xff010203U);
  GW_EXPECT(is_premultiplied(Pixel{64, 32, 1, 64}));
  GW_EXPECT(!is_premultiplied(Pixel{65, 0, 0, 64}));

  std::array<std::byte, 6> unaligned{};
  store_u32(unaligned.data() + 1, 0xaabbccddU);
  GW_EXPECT_EQ(load_u32(unaligned.data() + 1), 0xaabbccddU);

  GW_EXPECT_EQ(blend(Pixel{255, 255, 255, 255}, Pixel{0, 0, 0, 255},
                     full_opacity / 2),
               Pixel{128, 128, 128, 255});
  GW_EXPECT_EQ(blend(Pixel{128, 0, 0, 128}, Pixel{0, 0, 255, 255},
                     full_opacity),
               Pixel{128, 0, 127, 255});
  GW_EXPECT_EQ(blend(Pixel{255, 0, 0, 255}, Pixel{0, 0, 255, 255}, 0),
               Pixel{0, 0, 255, 255});

  std::array<std::byte, 16> framebuffer{};
  FramebufferView target{framebuffer, 2, 2, 8};
  GW_EXPECT_EQ(clear(target, {0, 0, 2, 2}), RenderResult::Success);
  std::array<std::byte, 4> source_bytes{};
  store_u32(source_bytes.data(), 0x80800000U);
  const ImageView source{source_bytes, 1, 1, 4,
                         PixelFormat::Argb8888Premultiplied};
  GW_EXPECT_EQ(composite(target, source, {0, 0, 1, 1}, 1, 0, full_opacity),
               RenderResult::Success);
  GW_EXPECT_EQ(load_u32(framebuffer.data() + 4), 0xff800000U);
  GW_EXPECT_EQ(load_u32(framebuffer.data()), 0xff000000U);

  store_u32(source_bytes.data(), 0x407f0000U);
  GW_EXPECT_EQ(composite(target, source, {0, 0, 1, 1}, 0, 0, full_opacity),
               RenderResult::InvalidPremultipliedPixel);
  return 0;
}
