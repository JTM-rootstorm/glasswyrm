#include "glasswyrmd/subtree_compositor.hpp"
#include "helpers/test_support.hpp"

namespace {
glasswyrm::server::WindowCreateSpec make_window(
    const std::uint32_t xid, const std::uint32_t parent, const std::int16_t x,
    const std::int16_t y, const std::uint16_t width,
    const std::uint16_t height) {
  glasswyrm::server::WindowCreateSpec result;
  result.xid = xid;
  result.parent = parent;
  result.x = x;
  result.y = y;
  result.width = width;
  result.height = height;
  result.depth = 24;
  result.window_class = glasswyrm::server::WindowClass::InputOutput;
  return result;
}
}  // namespace

int main() {
  using namespace glasswyrm::server;
  constexpr std::uint32_t base = 0x00400000;
  constexpr std::uint32_t mask = 0x001fffff;
  ResourceTable resources;
  gw::test::require(
      resources.create_window(1, base, mask,
                              make_window(base + 1, 1, 0, 0, 8, 6)) ==
              CreateWindowStatus::Success &&
          resources.create_window(
              1, base, mask,
              make_window(base + 2, base + 1, 2, 1, 5, 4)) ==
              CreateWindowStatus::Success &&
          resources.create_window(
              1, base, mask,
              make_window(base + 3, base + 1, 4, 2, 3, 3)) ==
              CreateWindowStatus::Success &&
          resources.create_window(
              1, base, mask,
              make_window(base + 4, base + 2, 2, 1, 4, 3)) ==
              CreateWindowStatus::Success,
      "create subtree");
  auto* top = resources.find_window(base + 1);
  auto* lower = resources.find_window(base + 2);
  auto* upper = resources.find_window(base + 3);
  auto* clipped_grandchild = resources.find_window(base + 4);
  for (auto* window : {top, lower, upper, clipped_grandchild}) {
    window->map_requested = true;
    window->map_state = MapState::Viewable;
    auto storage = PixelStorage::create(window->width, window->height);
    gw::test::require(storage.has_value(), "create backing");
    window->storage =
        std::make_shared<PixelStorage>(std::move(*storage));
  }
  top->storage->fill({0, 0, 8, 6}, 0x00101010U);
  lower->storage->fill({0, 0, 5, 4}, 0x00ff0000U);
  upper->storage->fill({0, 0, 3, 3}, 0x0000ff00U);
  clipped_grandchild->storage->fill({0, 0, 4, 3}, 0x000000ffU);

  const auto composed = compose_top_level_subtree(resources, base + 1);
  gw::test::require(composed.has_value(), "compose subtree");
  gw::test::require(composed->at(0, 0) == 0xff101010U,
                    "top-level backing remains");
  gw::test::require(composed->at(2, 1) == 0xffff0000U,
                    "mapped child overlays parent");
  gw::test::require(composed->at(4, 2) == 0xff00ff00U,
                    "higher sibling wins");
  gw::test::require(composed->at(4, 3) == 0xff00ff00U,
                    "sibling remains above grandchild");
  gw::test::require(composed->at(6, 3) == 0xff00ff00U,
                    "grandchild is clipped and covered");

  upper->map_state = MapState::Unmapped;
  const auto revealed = compose_top_level_subtree(resources, base + 1);
  gw::test::require(revealed.has_value() &&
                        revealed->at(4, 3) == 0xff0000ffU,
                    "unmapping sibling reveals descendant pixels");
  gw::test::require(revealed->at(7, 4) == 0xff101010U,
                    "grandchild clips to ancestor interior");

  lower->attributes.background_source = BackgroundSource::Pixel;
  lower->attributes.background_pixel = 0x00123456U;
  LocalConfigure resize;
  resize.width = 6;
  resize.height = 5;
  gw::test::require(
      resources.configure_local(base + 2, resize) ==
              LocalLifecycleStatus::Success &&
          lower->width == 6 && lower->height == 5 && lower->storage &&
          lower->storage->width() == 6 && lower->storage->height() == 5,
      "local configure resizes child backing with geometry");
  gw::test::require(lower->storage->at(0, 0) == 0xffff0000U &&
                        lower->storage->at(5, 4) == 0xff123456U,
                    "child backing resize preserves overlap and background");
  const auto resized = compose_top_level_subtree(resources, base + 1);
  gw::test::require(resized.has_value() &&
                        resized->at(7, 5) == 0xff123456U,
                    "resized child composes through its complete geometry");

  const auto storage_before_failure = lower->storage;
  LocalConfigure oversized;
  oversized.width = 65535;
  oversized.height = 65535;
  gw::test::require(
      resources.configure_local(base + 2, oversized) ==
              LocalLifecycleStatus::BadAlloc &&
          lower->width == 6 && lower->height == 5 &&
          lower->storage == storage_before_failure,
      "failed child backing resize leaves geometry and storage atomic");
  return 0;
}
