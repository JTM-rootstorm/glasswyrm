tool_sources = {
  'gwctl': 'gwctl/main.cpp',
  'gwinfo': 'gwinfo/main.cpp',
  'gwtrace': 'gwtrace/main.cpp',
  'gwout': 'gwout/main.cpp',
  'gwbench': 'gwbench/main.cpp',
}

foreach tool_name, tool_source : tool_sources
  executable(
    tool_name,
    files(tool_source),
    dependencies: gw_scaffold_dep,
    install: true,
  )
endforeach

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
