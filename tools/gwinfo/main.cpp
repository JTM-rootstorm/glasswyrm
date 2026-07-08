#include <iostream>

#include <glasswyrm/backends/headless.hpp>
#include <glasswyrm/core/build_config.hpp>
#include <glasswyrm/input/seat.hpp>

int main() {
  const auto config = glasswyrm::core::build_config();
  const glasswyrm::backends::HeadlessBackend headless;
  const auto seat = glasswyrm::input::default_synthetic_seat();

  std::cout << glasswyrm::core::project_name() << " " << config.version << '\n';
  std::cout << "headless backend: "
            << (config.backend_headless ? "enabled" : "disabled") << '\n';
  std::cout << "software renderer: "
            << (config.render_software ? "enabled" : "disabled") << '\n';
  std::cout << "assembly mode: "
            << glasswyrm::core::assembly_mode_name(config.assembly_mode) << '\n';
  std::cout << "default output: " << headless.describe() << '\n';
  std::cout << "synthetic input: " << seat.name << '\n';

  return 0;
}
