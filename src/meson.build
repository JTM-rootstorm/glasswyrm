gw_scaffold_lib = static_library(
  'gw-scaffold',
  files('scaffold/component.cpp'),
  include_directories: [gw_include_dir, gw_generated_include_dir],
)

gw_scaffold_dep = declare_dependency(
  link_with: gw_scaffold_lib,
  include_directories: [gw_include_dir, gw_generated_include_dir],
)

if get_option('glasswyrmd')
  executable(
    'glasswyrmd',
    files('glasswyrmd/main.cpp'),
    dependencies: gw_scaffold_dep,
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
