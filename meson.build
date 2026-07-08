project(
  'glasswyrm',
  ['cpp', 'c'],
  version: '0.1.0',
  license: 'MIT',
  default_options: [
    'cpp_std=c++20',
    'c_std=c17',
    'warning_level=2',
  ],
)

gw_include_dir = include_directories('include')

gw_cpp_args = []
if get_option('werror')
  gw_cpp_args += ['-Werror']
endif

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
config_data.set10('GW_BACKEND_HEADLESS', get_option('headless_backend'))
config_data.set10('GW_BACKEND_DRM', get_option('drm_backend'))
config_data.set10('GW_RENDER_SOFTWARE', get_option('render_software'))
config_data.set10('GW_RENDER_GL', get_option('render_gl'))
config_data.set10('GW_RENDER_VULKAN', get_option('render_vulkan'))
config_data.set_quoted('GW_ASM_MODE', get_option('asm'))
config_data.set10('GW_EXPERIMENTAL', get_option('experimental'))

configure_file(
  output: 'config.hpp',
  configuration: config_data,
)

gw_generated_include_dir = include_directories('.')

subdir('src')
subdir('tools')
subdir('tests')
