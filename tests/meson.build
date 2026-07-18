tests_source_dir = meson.current_source_dir()
tests_build_dir = meson.current_build_dir()
test_source_include = include_directories('.')
test_source_root_include = [
  test_source_include,
  include_directories('..'),
]

# Runtime and compatibility tooling consumes these artifacts directly from the
# tests build directory. Keep their target definitions at this level even when
# ordinary test registrations are split into subsystem manifests.
if host_machine.system() == 'linux'
  uinput_m11_protocol_lib = static_library(
    'uinput_m11_protocol',
    files(tests_source_dir / 'helpers/uinput_m11_protocol.cpp'),
    include_directories: test_source_root_include,
  )
  uinput_m11_protocol_dep = declare_dependency(
    link_with: uinput_m11_protocol_lib,
    include_directories: test_source_root_include,
  )
  gw_uinput_m11_program = executable(
    'gw_uinput_m11',
    files(tests_source_dir / 'integration/gw_uinput_m11.cpp'),
    dependencies: uinput_m11_protocol_dep,
    install: false,
  )
endif

m9_fixed_time = shared_library(
  'gw_m9_fixed_time',
  files(tests_source_dir / 'apps/m9_fixed_time.c'),
  install: false,
)

if get_option('glasswyrmd') and get_option('libgwipc')
  gwinput_m8_program = executable(
    'gwinput_m8',
    files(tests_source_dir / 'integration/gwinput_m8.cpp'),
    dependencies: gwipc_dep,
    install: false,
  )
  m12_extension_stress_probe = executable(
    'm12_extension_stress_probe',
    files(tests_source_dir / 'integration/m12_extension_stress_probe.cpp'),
    dependencies: gw_server_core_dep,
    include_directories: test_source_root_include,
    install: false,
  )
endif

if get_option('libgwipc')
  gwipc_wire_probe = executable(
    'gwipc_wire_probe',
    files(tests_source_dir / 'integration/gwipc_wire_probe.c'),
    dependencies: gwipc_dep,
  )
  gwipc_probe_server = executable(
    'gwipc_probe_server',
    files(tests_source_dir / 'integration/gwipc_probe_server.cpp'),
    dependencies: [gwipc_dep, gwipc_wire_dep],
    include_directories: test_source_root_include,
  )
  gwipc_probe_client = executable(
    'gwipc_probe_client',
    files(tests_source_dir / 'integration/gwipc_probe_client.cpp'),
    dependencies: [gwipc_dep, gwipc_wire_dep],
    include_directories: test_source_root_include,
  )
endif

if get_option('glasswyrmd')
  x11_fake_client_lib = static_library(
    'x11-fake-client',
    files(
      tests_source_dir / 'helpers/x11_fake_client.cpp',
      tests_source_dir / 'helpers/x11_request_builder.cpp',
      tests_source_dir / 'integration/server_fixture.cpp',
    ),
    dependencies: gw_x11_setup_dep,
    include_directories: test_source_root_include,
  )
  x11_fake_client_dep = declare_dependency(
    link_with: x11_fake_client_lib,
    dependencies: gw_x11_setup_dep,
    include_directories: test_source_root_include,
  )

  x11_setup_probe = executable(
    'x11_setup_probe',
    files(tests_source_dir / 'integration/x11_setup_probe.cpp'),
    dependencies: x11_fake_client_dep,
    include_directories: test_source_root_include,
  )
  x11_milestone2_probe = executable(
    'x11_milestone2_probe',
    files(tests_source_dir / 'integration/x11_milestone2_probe.cpp'),
    dependencies: x11_fake_client_dep,
    include_directories: test_source_root_include,
  )
  x11_milestone6_probe = executable(
    'x11_milestone6_probe',
    files(tests_source_dir / 'integration/x11_milestone6_probe.cpp'),
    dependencies: x11_fake_client_dep,
    include_directories: test_source_root_include,
  )
  x11_milestone7_probe = executable(
    'x11_milestone7_probe',
    files(tests_source_dir / 'integration/x11_milestone7_probe.cpp'),
    dependencies: x11_fake_client_dep,
    include_directories: test_source_root_include,
  )
  if get_option('libgwipc')
    x11_milestone8_probe = executable(
      'x11_milestone8_probe',
      files(
        tests_source_dir / 'integration/x11_milestone8_probe.cpp',
        tests_source_dir / 'helpers/synthetic_input_client.cpp',
      ),
      dependencies: [x11_fake_client_dep, gwipc_dep],
      include_directories: test_source_root_include,
    )
  endif

  xcb_test_dependency = dependency('xcb', required: false)
  if xcb_test_dependency.found()
    m6_restart_hold_probe = executable(
      'm6_restart_hold_probe',
      tests_source_dir / 'xcb/m6_restart_hold_probe.c',
      dependencies: xcb_test_dependency,
      install: false,
    )
    xcb_setup_probe = executable(
      'xcb_setup_probe',
      files(tests_source_dir / 'xcb/xcb_setup_probe.c'),
      dependencies: xcb_test_dependency,
    )
    xcb_milestone2_probe = executable(
      'xcb_milestone2_probe',
      files(tests_source_dir / 'xcb/xcb_milestone2_probe.c'),
      dependencies: xcb_test_dependency,
    )
    xcb_milestone6_probe = executable(
      'xcb_milestone6_probe',
      files(tests_source_dir / 'xcb/xcb_milestone6_probe.c'),
      dependencies: xcb_test_dependency,
    )
    xcb_milestone7_probe = executable(
      'xcb_milestone7_probe',
      files(tests_source_dir / 'xcb/xcb_milestone7_probe.c'),
      dependencies: xcb_test_dependency,
    )
    m11_selection_probe = executable(
      'm11_selection_probe',
      files(tests_source_dir / 'xcb/m11_selection_probe.cpp'),
      dependencies: xcb_test_dependency,
      install: false,
    )
    m11_xterm_geometry_probe = executable(
      'm11_xterm_geometry_probe',
      files(tests_source_dir / 'xcb/m11_xterm_geometry_probe.cpp'),
      dependencies: xcb_test_dependency,
      install: false,
    )
    if get_option('libgwipc')
      xcb_milestone8_probe = executable(
        'xcb_milestone8_probe',
        files(
          tests_source_dir / 'xcb/xcb_milestone8_probe.cpp',
          tests_source_dir / 'helpers/synthetic_input_client.cpp',
        ),
        dependencies: [xcb_test_dependency, gwipc_dep],
        include_directories: test_source_root_include,
      )
      m8_restart_hold_probe = executable(
        'm8_restart_hold_probe',
        files(
          tests_source_dir / 'xcb/m8_restart_hold_probe.cpp',
          tests_source_dir / 'helpers/synthetic_input_client.cpp',
        ),
        dependencies: [xcb_test_dependency, gwipc_dep],
        include_directories: test_source_root_include,
      )
    endif
    m7_restart_hold_probe = executable(
      'm7_restart_hold_probe',
      files(tests_source_dir / 'xcb/m7_restart_hold_probe.c'),
      dependencies: xcb_test_dependency,
    )
  endif
endif

subdir('manifest/foundation')
subdir('manifest/server')
subdir('manifest/graphics')
subdir('manifest/ipc')
subdir('manifest/x11_tools')
