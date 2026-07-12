gw_scaffold_lib = static_library(
  'gw-scaffold',
  files('scaffold/component.cpp'),
  include_directories: [gw_include_dir, gw_generated_include_dir],
)

gw_x11_setup_lib = static_library(
  'gw-x11-setup',
  files(
    'protocol/x11/setup.cpp',
    'protocol/x11/request.cpp',
    'protocol/x11/reply.cpp',
  ),
  include_directories: include_directories('.'),
)

gw_x11_setup_dep = declare_dependency(
  link_with: gw_x11_setup_lib,
  include_directories: include_directories('.'),
)

gw_scaffold_dep = declare_dependency(
  link_with: gw_scaffold_lib,
  include_directories: [gw_include_dir, gw_generated_include_dir],
)

if get_option('gwcomp')
gw_compositor_geometry_lib = static_library(
  'gwcomp-geometry',
  files(
    'compositor/rectangle.cpp',
    'compositor/damage_region.cpp',
    'compositor/scene.cpp',
    'compositor/buffer.cpp',
  ),
  include_directories: [include_directories('.'), gw_include_dir],
)
gw_compositor_geometry_dep = declare_dependency(
  link_with: gw_compositor_geometry_lib,
  include_directories: [include_directories('.'), gw_include_dir],
)

gw_render_software_lib = static_library(
  'gwcomp-render-software',
  files(
    'render/software/blend.cpp',
    'render/software/renderer.cpp',
  ),
  dependencies: gw_compositor_geometry_dep,
  include_directories: include_directories('.'),
)
gw_render_software_dep = declare_dependency(
  link_with: gw_render_software_lib,
  dependencies: gw_compositor_geometry_dep,
  include_directories: include_directories('.'),
)

if get_option('headless_backend')
  gw_headless_lib = static_library(
    'gw-headless',
    files(
      'backends/headless/output.cpp',
      'backends/headless/frame_dump.cpp',
    ),
    include_directories: include_directories('.'),
  )
  gw_headless_dep = declare_dependency(
    link_with: gw_headless_lib,
    include_directories: include_directories('.'),
  )
endif
endif

if get_option('libgwipc')
  gwipc_cpp_args = ['-DGWIPC_BUILDING_LIBRARY', '-fvisibility-inlines-hidden']
  if get_option('ipc_trace')
    gwipc_cpp_args += '-DGWIPC_TRACE'
  endif
  gwipc_wire_sources = files(
    'ipc/wire/envelope.cpp',
    'ipc/wire/control.cpp',
    'ipc/wire/compositor_contract.cpp',
  )
  gwipc_private_include = include_directories('.')
  gwipc_wire_lib = static_library(
    'gwipc-wire',
    gwipc_wire_sources,
    include_directories: gwipc_private_include,
    gnu_symbol_visibility: 'hidden',
    cpp_args: ['-fvisibility-inlines-hidden'],
    pic: true,
    install: false,
  )
  gwipc_wire_dep = declare_dependency(
    link_with: gwipc_wire_lib,
    include_directories: gwipc_private_include,
  )

  gwipc_library = shared_library(
    'gwipc',
    files(
      'ipc/public_api.cpp',
      'ipc/contract_api.cpp',
      'ipc/message.cpp',
      'ipc/endpoint.cpp',
      'ipc/connection.cpp',
    ),
    link_whole: gwipc_wire_lib,
    include_directories: [
      gw_include_dir,
      gwipc_private_include,
      gw_generated_include_dir,
    ],
    cpp_args: gwipc_cpp_args,
    link_args: [
      '-Wl,--version-script=' + meson.current_source_dir() / 'ipc/gwipc.map',
    ],
    gnu_symbol_visibility: 'hidden',
    version: '0.2.0',
    soversion: '0',
    install: true,
  )
  gwipc_dep = declare_dependency(
    link_with: gwipc_library,
    include_directories: gw_include_dir,
  )

  install_headers(
    '../include/glasswyrm/ipc.h',
    '../include/glasswyrm/ipc.hpp',
    subdir: 'glasswyrm',
  )
  install_headers(
    '../include/glasswyrm/ipc/version.h',
    '../include/glasswyrm/ipc/types.h',
    '../include/glasswyrm/ipc/listener.h',
    '../include/glasswyrm/ipc/connection.h',
    '../include/glasswyrm/ipc/message.h',
    '../include/glasswyrm/ipc/contracts.h',
    subdir: 'glasswyrm/ipc',
  )

  pkgconfig = import('pkgconfig')
  pkgconfig.generate(
    gwipc_library,
    filebase: 'gwipc',
    name: 'gwipc',
    description: 'Glasswyrm versioned local IPC contracts',
    version: '0.2.0',
  )
endif

if get_option('glasswyrmd')
  gw_server_state_lib = static_library(
    'gw-server-state',
    files(
      'glasswyrmd/property.cpp',
      'glasswyrmd/resource_table.cpp',
      'glasswyrmd/atom_table.cpp',
      'glasswyrmd/server_state.cpp',
      'glasswyrmd/request_dispatcher.cpp',
    ),
    dependencies: gw_x11_setup_dep,
    include_directories: include_directories('.'),
  )
  gw_server_state_dep = declare_dependency(
    link_with: gw_server_state_lib,
    dependencies: gw_x11_setup_dep,
    include_directories: include_directories('.'),
  )
  gw_server_core_lib = static_library(
    'gw-server-core',
    files(
      'glasswyrmd/options.cpp',
      'glasswyrmd/client_connection.cpp',
      'glasswyrmd/server.cpp',
    ),
    dependencies: gw_server_state_dep,
    include_directories: [include_directories('.'), gw_generated_include_dir],
  )
  gw_server_core_dep = declare_dependency(
    link_with: gw_server_core_lib,
    dependencies: gw_server_state_dep,
    include_directories: [include_directories('.'), gw_generated_include_dir],
  )
  glasswyrmd_program = executable(
    'glasswyrmd',
    files('glasswyrmd/main.cpp'),
    dependencies: gw_server_core_dep,
    install: true,
  )
endif

if get_option('gwm')
  executable(
    'gwm',
    files('gwm/main.cpp'),
    dependencies: gw_scaffold_dep,
    install: true,
  )
endif

if get_option('gwcomp')
  gwcomp_options_lib = static_library(
    'gwcomp-options',
    files('gwcomp/options.cpp'),
    include_directories: [include_directories('.'), gw_generated_include_dir],
  )
  gwcomp_options_dep = declare_dependency(
    link_with: gwcomp_options_lib,
    include_directories: [include_directories('.'), gw_generated_include_dir],
  )
  executable(
    'gwcomp',
    files('gwcomp/main.cpp'),
    dependencies: [gwcomp_options_dep, gwipc_dep, gw_compositor_geometry_dep,
                   gw_render_software_dep, gw_headless_dep],
    install: true,
  )
endif
