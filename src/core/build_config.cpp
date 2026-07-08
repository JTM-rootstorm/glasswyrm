#include <glasswyrm/core/build_config.hpp>

#include "config.hpp"

namespace glasswyrm::core {

namespace {

constexpr AssemblyMode parse_assembly_mode(std::string_view mode) noexcept {
  if (mode == "enabled") {
    return AssemblyMode::Enabled;
  }
  if (mode == "disabled") {
    return AssemblyMode::Disabled;
  }
  return AssemblyMode::Auto;
}

}  // namespace

BuildConfig build_config() noexcept {
  return BuildConfig{
      .version = GW_PROJECT_VERSION,
      .backend_headless = GW_BACKEND_HEADLESS,
      .backend_drm = GW_BACKEND_DRM,
      .render_software = GW_RENDER_SOFTWARE,
      .render_gl = GW_RENDER_GL,
      .render_vulkan = GW_RENDER_VULKAN,
      .assembly_mode = parse_assembly_mode(GW_ASM_MODE),
      .experimental = GW_EXPERIMENTAL,
  };
}

std::string_view assembly_mode_name(AssemblyMode mode) noexcept {
  switch (mode) {
    case AssemblyMode::Auto:
      return "auto";
    case AssemblyMode::Enabled:
      return "enabled";
    case AssemblyMode::Disabled:
      return "disabled";
  }
  return "unknown";
}

std::string_view project_name() noexcept {
  return "Glasswyrm";
}

}  // namespace glasswyrm::core
