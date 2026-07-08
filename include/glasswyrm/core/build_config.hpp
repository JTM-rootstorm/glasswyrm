#pragma once

#include <string_view>

namespace glasswyrm::core {

enum class AssemblyMode {
  Auto,
  Enabled,
  Disabled,
};

struct BuildConfig {
  std::string_view version;
  bool backend_headless;
  bool backend_drm;
  bool render_software;
  bool render_gl;
  bool render_vulkan;
  AssemblyMode assembly_mode;
  bool experimental;
};

BuildConfig build_config() noexcept;
std::string_view assembly_mode_name(AssemblyMode mode) noexcept;
std::string_view project_name() noexcept;

}  // namespace glasswyrm::core
