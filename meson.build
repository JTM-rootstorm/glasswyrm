project(
  'glasswyrm',
  ['cpp', 'c'],
  version: '0.2.0',
  license: 'MIT',
  default_options: [
    'cpp_std=c++20',
    'c_std=c17',
    'warning_level=2',
  ],
)

if get_option('gwm') and not get_option('libgwipc')
  error('gwm requires -Dlibgwipc=true for the Milestone 5 policy contract')
endif

if get_option('gwcomp')
  if not get_option('libgwipc')
    error('gwcomp requires -Dlibgwipc=true for the Milestone 4 contract')
  endif
  if not get_option('headless_backend')
    error('gwcomp requires -Dheadless_backend=true in Milestone 4')
  endif
  if not get_option('render_software')
    error('gwcomp requires -Drender_software=true in Milestone 4')
  endif
endif

if get_option('drm_backend')
  error('the DRM backend is not implemented before Milestone 10')
endif
if get_option('render_gl')
  error('the OpenGL renderer is not implemented')
endif
if get_option('render_vulkan')
  error('the Vulkan renderer is not implemented')
endif

gw_include_dir = include_directories('include')

if get_option('asan')
  add_project_arguments('-fsanitize=address', language: ['c', 'cpp'])
  add_project_link_arguments('-fsanitize=address', language: ['c', 'cpp'])
endif

if get_option('ubsan')
  add_project_arguments('-fsanitize=undefined', language: ['c', 'cpp'])
  add_project_link_arguments('-fsanitize=undefined', language: ['c', 'cpp'])
endif

if get_option('tsan')
  add_project_arguments('-fsanitize=thread', language: ['c', 'cpp'])
  add_project_link_arguments('-fsanitize=thread', language: ['c', 'cpp'])
endif

config_data = configuration_data()
config_data.set_quoted('GW_PROJECT_VERSION', meson.project_version())

configure_file(
  output: 'config.hpp',
  configuration: config_data,
)

gw_generated_include_dir = include_directories('.')

subdir('src')
if get_option('tools')
  subdir('tools')
endif
subdir('tests')
