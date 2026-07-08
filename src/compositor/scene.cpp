#include <glasswyrm/compositor/scene.hpp>

namespace glasswyrm::compositor {

Scene::Scene(HeadlessOutput output) : output_(output) {}

const HeadlessOutput& Scene::output() const noexcept {
  return output_;
}

bool Scene::is_headless() const noexcept {
  return output_.name == "headless-0";
}

std::uint64_t Scene::frame_number() const noexcept {
  return frame_number_;
}

void Scene::mark_frame_presented() noexcept {
  ++frame_number_;
}

HeadlessOutput default_headless_output() noexcept {
  return HeadlessOutput{
      .name = "headless-0",
      .physical_size = Extent{.width = 1280, .height = 720},
      .logical_scale = 1.0F,
  };
}

}  // namespace glasswyrm::compositor
