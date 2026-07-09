#pragma once

#include <string_view>

namespace glasswyrm::scaffold {

enum class Component {
  Server,
  WindowManager,
  Compositor,
  ControlTool,
  InfoTool,
  TraceTool,
  OutputTool,
  BenchmarkTool,
};

struct ComponentInfo {
  std::string_view executable;
  std::string_view responsibility;
};

[[nodiscard]] ComponentInfo component_info(Component component) noexcept;
int run_placeholder(Component component);

}  // namespace glasswyrm::scaffold
