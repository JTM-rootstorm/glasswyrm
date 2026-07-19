tests_source_dir = meson.current_source_dir()
tests_build_dir = meson.current_build_dir()
test_source_include = include_directories('.')
test_source_root_include = [
  test_source_include,
  include_directories('..'),
]

# Runtime and compatibility tooling consumes these artifacts directly from the
# tests build directory. Their source manifests are split by subsystem, while
# target creation remains here to preserve the established artifact paths.
subdir('targets/runtime')

if host_machine.system() == 'linux'
  uinput_m11_protocol_lib = static_library(
    'uinput_m11_protocol',
    uinput_m11_protocol_sources,
    include_directories: test_source_root_include,
  )
  uinput_m11_protocol_dep = declare_dependency(
    link_with: uinput_m11_protocol_lib,
    include_directories: test_source_root_include,
  )
  gw_uinput_m11_program = executable(
    'gw_uinput_m11',
    gw_uinput_m11_sources,
    dependencies: uinput_m11_protocol_dep,
    install: false,
  )
endif

m9_fixed_time = shared_library(
  'gw_m9_fixed_time',
  m9_fixed_time_sources,
  install: false,
)

if get_option('glasswyrmd') and get_option('libgwipc')
  gwinput_m8_program = executable(
    'gwinput_m8',
    gwinput_m8_sources,
    dependencies: gwipc_dep,
    install: false,
  )
  m12_extension_stress_probe = executable(
    'm12_extension_stress_probe',
    m12_extension_stress_probe_sources,
    dependencies: gw_server_core_dep,
    include_directories: test_source_root_include,
    install: false,
  )
endif

subdir('targets/ipc')

if get_option('libgwipc')
  gwipc_wire_probe = executable(
    'gwipc_wire_probe',
    gwipc_wire_probe_sources,
    dependencies: gwipc_dep,
  )
  foreach probe_name, probe_sources : gwipc_peer_probe_sources
    set_variable(
      probe_name,
      executable(
        probe_name,
        probe_sources,
        dependencies: [gwipc_dep, gwipc_wire_dep],
        include_directories: test_source_root_include,
      ),
    )
  endforeach
endif

subdir('targets/x11')

if get_option('glasswyrmd')
  x11_fake_client_lib = static_library(
    'x11-fake-client',
    x11_fake_client_sources,
    dependencies: gw_x11_setup_dep,
    include_directories: test_source_root_include,
  )
  x11_fake_client_dep = declare_dependency(
    link_with: x11_fake_client_lib,
    dependencies: gw_x11_setup_dep,
    include_directories: test_source_root_include,
  )

  foreach probe_name, probe_sources : x11_probe_sources
    set_variable(
      probe_name,
      executable(
        probe_name,
        probe_sources,
        dependencies: x11_fake_client_dep,
        include_directories: test_source_root_include,
      ),
    )
  endforeach
  if get_option('libgwipc')
    x11_milestone8_probe = executable(
      'x11_milestone8_probe',
      x11_milestone8_probe_sources,
      dependencies: [x11_fake_client_dep, gwipc_dep],
      include_directories: test_source_root_include,
    )
  endif
endif

subdir('targets/xcb')

if get_option('glasswyrmd')
  xcb_test_dependency = dependency('xcb', required: false)
  if xcb_test_dependency.found()
    foreach probe_name, probe_sources : xcb_probe_sources
      set_variable(
        probe_name,
        executable(probe_name, probe_sources, dependencies: xcb_test_dependency),
      )
    endforeach
    if get_option('libgwipc')
      foreach probe_name, probe_sources : xcb_ipc_probe_sources
        set_variable(
          probe_name,
          executable(
            probe_name,
            probe_sources,
            dependencies: [xcb_test_dependency, gwipc_dep],
            include_directories: test_source_root_include,
          ),
        )
      endforeach
    endif
  endif
endif

subdir('manifest/support/m13_tooling')
subdir('manifest/foundation')
subdir('manifest/server')
subdir('manifest/graphics')
subdir('manifest/ipc')
subdir('manifest/x11_tools')
