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
