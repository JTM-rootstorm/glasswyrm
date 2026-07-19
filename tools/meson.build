placeholder_tool_sources = {
  'gwctl': 'gwctl/main.cpp',
  'gwtrace': 'gwtrace/main.cpp',
  'gwbench': 'gwbench/main.cpp',
}

foreach tool_name, tool_source : placeholder_tool_sources
  executable(
    tool_name,
    files(tool_source),
    dependencies: gw_scaffold_dep,
    install: true,
  )
endforeach

output_client_lib = static_library(
  'gw-output-client',
  files(
    'output_client/connection.cpp',
    'output_client/snapshot.cpp',
    'output_client/format.cpp',
    'output_client/layout_edit.cpp',
    'output_client/vrr_edit.cpp',
    'output_client/vrr_format.cpp',
    'output_client/vrr_snapshot.cpp',
  ),
  dependencies: gwipc_dep,
  include_directories: [include_directories('.'), gw_generated_include_dir],
  install: false,
)
output_client_dep = declare_dependency(
  link_with: output_client_lib,
  dependencies: gwipc_dep,
  include_directories: [include_directories('.'), gw_generated_include_dir],
)

gwinfo_program = executable(
  'gwinfo',
  files('gwinfo/main.cpp'),
  dependencies: output_client_dep,
  install: true,
)
gwout_program = executable(
  'gwout',
  files('gwout/main.cpp'),
  dependencies: output_client_dep,
  install: true,
)

if get_option('drm_backend')
  gw_drm_probe_lib = static_library(
    'gw-drm-probe',
    files('../src/tools/drm_probe.cpp'),
    dependencies: gw_drm_api_dep,
    install: false,
  )
  gw_drm_probe_dep = declare_dependency(
    link_with: gw_drm_probe_lib,
    dependencies: gw_drm_api_dep,
  )
  gw_drm_probe_program = executable(
    'gw_drm_probe',
    files('../src/tools/drm_probe_main.cpp'),
    dependencies: gw_drm_probe_dep,
    install: false,
  )
endif
