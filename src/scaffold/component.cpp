#include <glasswyrm/scaffold/component.hpp>

#include <iostream>

#include "config.hpp"

namespace glasswyrm::scaffold {

ComponentInfo component_info(Component component) noexcept {
  switch (component) {
    case Component::Server:
      return {"glasswyrmd", "X11 protocol truth"};
    case Component::WindowManager:
      return {"gwm", "window-management policy truth"};
    case Component::Compositor:
      return {"gwcomp", "composition and display authority"};
    case Component::ControlTool:
      return {"gwctl", "runtime control"};
    case Component::InfoTool:
      return {"gwinfo", "runtime diagnostics"};
    case Component::TraceTool:
      return {"gwtrace", "protocol and event tracing"};
    case Component::OutputTool:
      return {"gwout", "output configuration"};
    case Component::BenchmarkTool:
      return {"gwbench", "rendering and compositor benchmarks"};
  }

  return {"unknown", "unknown"};
}

int run_placeholder(Component component) {
  const auto info = component_info(component);

  std::cout << info.executable << " " << GW_PROJECT_VERSION << '\n';
  std::cout << "Milestone 0 repository skeleton placeholder\n";
  std::cout << "Future responsibility: " << info.responsibility << '\n';
  std::cout << "No runtime behavior is implemented.\n";
  return 0;
}

}  // namespace glasswyrm::scaffold
