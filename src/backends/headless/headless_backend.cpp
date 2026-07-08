#include <glasswyrm/backends/headless.hpp>

#include <sstream>

namespace glasswyrm::backends {

HeadlessBackend::HeadlessBackend()
    : scene_(compositor::default_headless_output()) {}

const compositor::Scene& HeadlessBackend::scene() const noexcept {
  return scene_;
}

compositor::Scene& HeadlessBackend::scene() noexcept {
  return scene_;
}

std::string HeadlessBackend::describe() const {
  const auto& output = scene_.output();

  std::ostringstream stream;
  stream << output.name << " " << output.physical_size.width << "x"
         << output.physical_size.height << " scale=" << output.logical_scale;
  return stream.str();
}

}  // namespace glasswyrm::backends
