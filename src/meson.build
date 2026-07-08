gw_core_sources = files(
  'core/build_config.cpp',
  'core/server.cpp',
)

gw_protocol_sources = files(
  'protocol/x11.cpp',
)

gw_compositor_sources = files(
  'compositor/scene.cpp',
)

gw_backend_sources = files(
  'backends/headless/headless_backend.cpp',
)

gw_input_sources = files(
  'input/seat.cpp',
)

gw_render_sources = files(
  'render/software/software_renderer.cpp',
)

gw_sources = (
  gw_core_sources
  + gw_protocol_sources
  + gw_compositor_sources
  + gw_backend_sources
  + gw_input_sources
  + gw_render_sources
)

gw_lib = static_library(
  'gwcore',
  gw_sources,
  include_directories: [gw_include_dir, gw_generated_include_dir],
  cpp_args: gw_cpp_args,
)

gw_dep = declare_dependency(
  link_with: gw_lib,
  include_directories: [gw_include_dir, gw_generated_include_dir],
)

executable(
  'glasswyrmd',
  files('server/main.cpp'),
  dependencies: gw_dep,
  install: true,
)
