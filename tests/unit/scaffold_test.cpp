#include <glasswyrm/scaffold/component.hpp>

int main() {
  using glasswyrm::scaffold::Component;
  using glasswyrm::scaffold::component_info;

  if (component_info(Component::Server).executable != "glasswyrmd" ||
      component_info(Component::WindowManager).executable != "gwm" ||
      component_info(Component::Compositor).executable != "gwcomp" ||
      component_info(Component::ControlTool).executable != "gwctl" ||
      component_info(Component::InfoTool).executable != "gwinfo" ||
      component_info(Component::TraceTool).executable != "gwtrace" ||
      component_info(Component::OutputTool).executable != "gwout" ||
      component_info(Component::BenchmarkTool).executable != "gwbench") {
    return 1;
  }

  return 0;
}
