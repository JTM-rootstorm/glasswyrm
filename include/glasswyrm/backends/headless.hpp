#pragma once

#include <string>

#include <glasswyrm/compositor/scene.hpp>

namespace glasswyrm::backends {

class HeadlessBackend {
 public:
  HeadlessBackend();

  [[nodiscard]] const compositor::Scene& scene() const noexcept;
  [[nodiscard]] compositor::Scene& scene() noexcept;
  [[nodiscard]] std::string describe() const;

 private:
  compositor::Scene scene_;
};

}  // namespace glasswyrm::backends
