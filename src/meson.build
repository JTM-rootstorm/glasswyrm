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

if get_option('glasswyrmd')
  gw_server_core_lib = static_library(
    'gw-server-core',
    files(
      'glasswyrmd/options.cpp',
      'glasswyrmd/client_connection.cpp',
      'glasswyrmd/server.cpp',
    ),
    dependencies: gw_x11_setup_dep,
    include_directories: [include_directories('.'), gw_generated_include_dir],
  )
  gw_server_core_dep = declare_dependency(
    link_with: gw_server_core_lib,
    dependencies: gw_x11_setup_dep,
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
  executable(
    'gwcomp',
    files('gwcomp/main.cpp'),
    dependencies: gw_scaffold_dep,
    install: true,
  )
endif
