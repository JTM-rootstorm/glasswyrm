scaffold_test = executable(
  'scaffold_test',
  files('unit/scaffold_test.cpp'),
  dependencies: gw_scaffold_dep,
)

test('scaffold', scaffold_test)

if get_option('glasswyrmd') and get_option('gwm') and get_option('gwcomp')
  session_launcher_test = executable(
    'session_launcher_test',
    files('unit/session_launcher_test.cpp'),
    dependencies: [gw_session_launcher_dep, dependency('threads')],
    include_directories: include_directories('..'),
  )
  test('session-launcher', session_launcher_test, timeout: 10)
  test('session-launcher-help', glasswyrm_session_program, args: ['--help'],
       depends: glasswyrm_session_program, timeout: 10)
endif

if host_machine.system() == 'linux'
  uinput_m11_protocol_lib = static_library(
    'uinput_m11_protocol',
    files('helpers/uinput_m11_protocol.cpp'),
    include_directories: include_directories('..'),
  )
  uinput_m11_protocol_dep = declare_dependency(
    link_with: uinput_m11_protocol_lib,
    include_directories: include_directories('..'),
  )
  gw_uinput_m11_program = executable(
    'gw_uinput_m11',
    files('integration/gw_uinput_m11.cpp'),
    dependencies: uinput_m11_protocol_dep,
    install: false,
  )
  test('gw-uinput-m11-help', gw_uinput_m11_program, args: ['--help'],
       timeout: 10)
  uinput_m11_protocol_test = executable(
    'uinput_m11_protocol_test',
    files('unit/uinput_m11_protocol_test.cpp'),
    dependencies: uinput_m11_protocol_dep,
  )
  test('gw-uinput-m11-protocol', uinput_m11_protocol_test, timeout: 10)
endif

x11_event_test = executable(
  'x11_event_test',
  files('protocol/x11_event_test.cpp'),
  dependencies: gw_x11_setup_dep,
)
test('x11-structural-events', x11_event_test, timeout: 10)

x11_input_event_test = executable(
  'x11_input_event_test',
  files('protocol/x11_input_event_test.cpp'),
  dependencies: gw_x11_setup_dep,
  include_directories: include_directories('..'),
)
test('x11-input-events', x11_input_event_test, timeout: 10)

x11_exposure_event_test = executable(
  'x11_exposure_event_test',
  files('protocol/x11_exposure_event_test.cpp'),
  dependencies: gw_x11_setup_dep,
  include_directories: include_directories('..'),
)
test('x11-exposure-events', x11_exposure_event_test, timeout: 10)

m7_fixture_validate = find_program('tools/m7_fixture_validate.sh')
test(
  'm7-fixture-checksums',
  m7_fixture_validate,
  args: [meson.project_source_root() / 'tests/fixtures/m7'],
  timeout: 10,
)
m8_fixture_validate = find_program('tools/m8_fixture_validate.sh')
test(
  'm8-fixture-checksums',
  m8_fixture_validate,
  args: [meson.project_source_root() / 'tests/fixtures/m8'],
  timeout: 10,
)
m9_fixture_validate = find_program('tools/m9_fixture_validate.sh')
test(
  'm9-fixture-checksums',
  m9_fixture_validate,
  args: [meson.project_source_root()],
  timeout: 10,
)
m9_trace_summarize_test = find_program('compat/m9/m9_trace_summarize_test.py')
test(
  'm9-trace-summarize',
  m9_trace_summarize_test,
  args: [meson.current_source_dir() / 'compat/m9/m9_trace_summarize'],
  timeout: 10,
)
m11_trace_summarize_test = find_program('compat/m11/m11_trace_summarize_test.py')
test(
  'm11-trace-summarize',
  m11_trace_summarize_test,
  args: [meson.current_source_dir() / 'compat/m11/m11_trace_summarize'],
  timeout: 10,
)
m11_fixture_validate_test = find_program('tools/m11_fixture_validate_test.py')
test(
  'm11-fixture-validator',
  m11_fixture_validate_test,
  args: [meson.current_source_dir() / 'tools/m11_fixture_validate.py'],
  timeout: 10,
)
test(
  'm11-fixture-checksum',
  find_program('tools/m11_fixture_validate.py'),
  args: [meson.project_source_root() / 'tests/fixtures/m11'],
  timeout: 10,
)

x11_lifecycle_request_test = executable(
  'x11_lifecycle_request_test',
  files('protocol/x11_lifecycle_request_test.cpp'),
  dependencies: gw_x11_setup_dep,
)
test('x11-lifecycle-request-decode', x11_lifecycle_request_test, timeout: 10)

test_source_root_include = include_directories('..')

m9_fixed_time = shared_library(
  'gw_m9_fixed_time',
  files('apps/m9_fixed_time.c'),
  install: false,
)
m9_fixed_time_test = executable(
  'm9_fixed_time_test',
  files('apps/m9_fixed_time_test.cpp'),
)
m9_fixed_time_preload = m9_fixed_time.full_path()
m9_fixed_time_environment = []
if get_option('asan')
  m9_fixed_time_preload = run_command(
    'cc', '-print-file-name=libasan.so', check: true,
  ).stdout().strip() + ':' + m9_fixed_time_preload
  # LeakSanitizer cannot inspect a process whose libc time entry points are
  # intentionally interposed. AddressSanitizer and UBSan remain active.
  m9_fixed_time_environment += ['ASAN_OPTIONS=detect_leaks=0:halt_on_error=1:abort_on_error=1']
endif
m9_fixed_time_environment += ['LD_PRELOAD=' + m9_fixed_time_preload]
test(
  'm9-fixed-time',
  m9_fixed_time_test,
  env: m9_fixed_time_environment,
  depends: m9_fixed_time,
  timeout: 10,
)

m9_app_runner = executable(
  'm9_app_runner',
  files('apps/m9_app_runner.cpp'),
  install: false,
)
m9_runner_fixture = find_program('apps/m9_runner_fixture.sh')
test(
  'm9-app-runner',
  m9_app_runner,
  args: [
    '--expected-version', '1.2.3',
    '--evidence', meson.current_build_dir() / 'm9-runner-evidence',
    '--result', meson.current_build_dir() / 'm9-runner-result.json',
    '--timeout-ms', '1000', '--', m9_runner_fixture,
    meson.current_build_dir() / 'm9-runner-evidence',
  ],
  timeout: 10,
)

if get_option('gwm')
  gwm_options_test = executable(
    'gwm_options_test',
    files('unit/gwm_options_test.cpp'),
    dependencies: gwm_options_dep,
    include_directories: test_source_root_include,
  )
  test('gwm-options', gwm_options_test, timeout: 10)

  wm_policy_test = executable(
    'wm_policy_test',
    files('wm/policy_engine_test.cpp'),
    dependencies: gw_wm_policy_dep,
    include_directories: test_source_root_include,
  )
  test('wm-policy', wm_policy_test, timeout: 10)
  interactive_policy_test = executable(
    'interactive_policy_test',
    files('wm/interactive_policy_test.cpp'),
    dependencies: gw_wm_policy_dep,
    include_directories: test_source_root_include,
  )
  test('interactive-policy', interactive_policy_test, timeout: 10)
  if get_option('glasswyrmd')
    interactive_close_test = executable(
      'interactive_close_test',
      files('wm/interactive_close_test.cpp'),
      dependencies: [gw_wm_policy_dep, gw_server_state_dep],
      include_directories: test_source_root_include,
    )
    test('interactive-close', interactive_close_test, timeout: 10)
  endif

  gwm_process_test = executable(
    'gwm_process_test',
    files('integration/gwm_process_test.cpp'),
    dependencies: gwipc_dep,
  )
  test(
    'gwm-process',
    gwm_process_test,
    args: [gwm_program.full_path()],
    depends: gwm_program,
    timeout: 15,
  )
  test('gwm-help', gwm_program, args: ['--help'], depends: gwm_program,
       timeout: 10)
  test('gwm-version', gwm_program, args: ['--version'], depends: gwm_program,
       timeout: 10)
  test('gwm-m5-producer-help', gwm_m5_producer_program, args: ['--help'],
       depends: gwm_m5_producer_program, timeout: 10)

  gwm_scenario_matrix_test = executable(
    'gwm_scenario_matrix_test',
    files('integration/gwm_scenario_matrix_test.cpp'),
  )
  test(
    'gwm-scenario-matrix',
    gwm_scenario_matrix_test,
    args: [gwm_program.full_path(), gwm_m5_producer_program.full_path(),
           meson.project_source_root() / 'tests/fixtures/m5'],
    depends: [gwm_program, gwm_m5_producer_program],
    timeout: 180,
  )

  gwm_robustness_test = executable(
    'gwm_robustness_test',
    files('integration/gwm_robustness_test.cpp'),
    dependencies: gwipc_dep,
  )
  test(
    'gwm-robustness',
    gwm_robustness_test,
    args: [gwm_program.full_path()],
    depends: gwm_program,
    timeout: 30,
  )

endif

if get_option('glasswyrmd')
  if get_option('libinput_backend')
    xkb_keymap_test = executable(
      'xkb_keymap_test',
      files('unit/xkb_keymap_test.cpp'),
      dependencies: gw_xkb_keymap_dep,
      include_directories: test_source_root_include,
    )
    test('xkb-keymap', xkb_keymap_test, timeout: 10)
    repeat_timer_test = executable(
      'repeat_timer_test',
      files('unit/repeat_timer_test.cpp'),
      dependencies: gw_xkb_keymap_dep,
      include_directories: test_source_root_include,
    )
    test('repeat-timer', repeat_timer_test, timeout: 10)
    libinput_path_allowlist_test = executable(
      'libinput_path_allowlist_test',
      files('unit/libinput_path_allowlist_test.cpp'),
      dependencies: gw_xkb_keymap_dep,
      include_directories: test_source_root_include,
    )
    test('libinput-path-allowlist', libinput_path_allowlist_test, timeout: 10)
    libinput_backend_test = executable(
      'libinput_backend_test',
      files('unit/libinput_backend_test.cpp'),
      dependencies: gw_xkb_keymap_dep,
      include_directories: test_source_root_include,
    )
    test('libinput-backend', libinput_backend_test, timeout: 10)
    real_input_controller_test = executable(
      'real_input_controller_test',
      files('unit/real_input_controller_test.cpp'),
      dependencies: gw_server_core_dep,
      include_directories: test_source_root_include,
    )
    test('real-input-controller', real_input_controller_test, timeout: 10)
    real_absolute_pointer_routing_test = executable(
      'real_absolute_pointer_routing_test',
      files('integration/real_absolute_pointer_routing_test.cpp'),
      dependencies: gw_server_core_dep,
      include_directories: test_source_root_include,
    )
    test('real-absolute-pointer-routing',
         real_absolute_pointer_routing_test, timeout: 10)
  endif

  input_foundation_test = executable(
    'input_foundation_test',
    files('unit/input_foundation_test.cpp'),
    dependencies: gw_server_state_dep,
    include_directories: test_source_root_include,
  )
  test('input-foundation', input_foundation_test, timeout: 10)
  keyboard_mapping_dispatch_test = executable(
    'keyboard_mapping_dispatch_test',
    files('unit/keyboard_mapping_dispatch_test.cpp'),
    dependencies: gw_server_state_dep,
    include_directories: test_source_root_include,
  )
  test('keyboard-mapping-dispatch', keyboard_mapping_dispatch_test,
       timeout: 10)
  cursor_model_test = executable(
    'cursor_model_test',
    files('unit/cursor_model_test.cpp'),
    dependencies: gw_server_state_dep,
    include_directories: test_source_root_include,
  )
  test('cursor-model', cursor_model_test, timeout: 10)
  cursor_dispatch_test = executable(
    'cursor_dispatch_test',
    files('unit/cursor_dispatch_test.cpp'),
    dependencies: gw_server_state_dep,
    include_directories: test_source_root_include,
  )
  test('cursor-dispatch', cursor_dispatch_test, timeout: 10)
  selection_store_test = executable(
    'selection_store_test',
    files('unit/selection_store_test.cpp'),
    dependencies: gw_server_state_dep,
    include_directories: test_source_root_include,
  )
  test('selection-store', selection_store_test, timeout: 10)
  selection_dispatch_test = executable(
    'selection_dispatch_test',
    files('unit/selection_dispatch_test.cpp'),
    dependencies: gw_server_state_dep,
    include_directories: test_source_root_include,
  )
  test('selection-dispatch', selection_dispatch_test, timeout: 10)
  grab_state_test = executable(
    'grab_state_test',
    files('unit/grab_state_test.cpp'),
    dependencies: gw_server_state_dep,
    include_directories: test_source_root_include,
  )
  test('grab-state', grab_state_test, timeout: 10)
  grab_dispatch_test = executable(
    'grab_dispatch_test',
    files('unit/grab_dispatch_test.cpp'),
    dependencies: gw_server_state_dep,
    include_directories: test_source_root_include,
  )
  test('grab-dispatch', grab_dispatch_test, timeout: 10)
  drawable_core_test = executable(
    'drawable_core_test',
    files('unit/drawable_core_test.cpp'),
    dependencies: gw_server_state_dep,
    include_directories: test_source_root_include,
  )
  test('drawable-core', drawable_core_test, timeout: 10)

  subtree_compositor_test = executable(
    'subtree_compositor_test',
    files('unit/subtree_compositor_test.cpp'),
    dependencies: gw_server_state_dep,
    include_directories: test_source_root_include,
  )
  test('subtree-compositor', subtree_compositor_test, timeout: 10)
  drawable_resource_test = executable(
    'drawable_resource_test',
    files('unit/drawable_resource_test.cpp'),
    dependencies: gw_server_state_dep,
    include_directories: test_source_root_include,
  )
  test('drawable-resources', drawable_resource_test, timeout: 10)
  drawable_dispatch_test = executable(
    'drawable_dispatch_test',
    files('unit/drawable_dispatch_test.cpp'),
    dependencies: gw_server_state_dep,
    include_directories: test_source_root_include,
  )
  test('drawable-dispatch', drawable_dispatch_test, timeout: 10)
  font_dispatch_test = executable(
    'font_dispatch_test',
    files('unit/font_dispatch_test.cpp'),
    dependencies: gw_server_state_dep,
    include_directories: test_source_root_include,
  )
  test('font-dispatch', font_dispatch_test, timeout: 10)
  glasswyrmd_options_test = executable(
    'glasswyrmd_options_test',
    files('unit/glasswyrmd_options_test.cpp'),
    dependencies: gw_server_core_dep,
    include_directories: test_source_root_include,
  )
  test('glasswyrmd-options', glasswyrmd_options_test, timeout: 10)
  compatibility_trace_test = executable(
    'compatibility_trace_test',
    files('unit/compatibility_trace_test.cpp'),
    dependencies: gw_server_core_dep,
    include_directories: test_source_root_include,
  )
  test('compatibility-trace', compatibility_trace_test, timeout: 10)
  event_router_test = executable(
    'event_router_test',
    files('unit/event_router_test.cpp'),
    dependencies: gw_server_core_dep,
    include_directories: test_source_root_include,
  )
  test('event-router', event_router_test, timeout: 10)
  client_connection_deferred_test = executable(
    'client_connection_deferred_test',
    files('unit/client_connection_deferred_test.cpp'),
    dependencies: gw_server_core_dep,
    include_directories: test_source_root_include,
  )
  test('client-connection-deferred', client_connection_deferred_test,
       timeout: 10)
  if get_option('libgwipc') and get_option('gwm') and get_option('gwcomp')
    glasswyrmd_peer_bootstrap_test = executable(
      'glasswyrmd_peer_bootstrap_test',
      files('integration/glasswyrmd_peer_bootstrap_test.cpp'),
      dependencies: gw_server_core_dep,
      include_directories: test_source_root_include,
    )
    test(
      'glasswyrmd-peer-bootstrap',
      glasswyrmd_peer_bootstrap_test,
      args: [gwm_program.full_path(), gwcomp_program.full_path()],
      depends: [gwm_program, gwcomp_program],
      timeout: 15,
    )
    runtime_bridge_transaction_test = executable(
      'runtime_bridge_transaction_test',
      files('integration/runtime_bridge_transaction_test.cpp'),
      dependencies: gw_server_core_dep,
      include_directories: test_source_root_include,
    )
    test(
      'runtime-bridge-transaction',
      runtime_bridge_transaction_test,
      args: [gwm_program.full_path(), gwcomp_program.full_path()],
      depends: [gwm_program, gwcomp_program],
      timeout: 15,
    )
    glasswyrmd_integrated_startup_test = executable(
      'glasswyrmd_integrated_startup_test',
      files('integration/glasswyrmd_integrated_startup_test.cpp'),
    )
    test(
      'glasswyrmd-integrated-startup',
      glasswyrmd_integrated_startup_test,
      args: [glasswyrmd_program.full_path(), gwm_program.full_path(),
             gwcomp_program.full_path()],
      depends: [glasswyrmd_program, gwm_program, gwcomp_program],
      timeout: 20,
    )
    glasswyrmd_integrated_lifecycle_test = executable(
      'glasswyrmd_integrated_lifecycle_test',
      files(
        'integration/glasswyrmd_integrated_lifecycle_test.cpp',
        'helpers/x11_fake_client.cpp',
        'helpers/x11_request_builder.cpp',
      ),
      dependencies: [gw_x11_setup_dep],
      include_directories: test_source_root_include,
    )
    test(
      'glasswyrmd-integrated-lifecycle',
      glasswyrmd_integrated_lifecycle_test,
      args: [glasswyrmd_program.full_path(), gwm_program.full_path(),
             gwcomp_program.full_path()],
      depends: [glasswyrmd_program, gwm_program, gwcomp_program],
      timeout: 45,
    )
    glasswyrmd_input_recovery_test = executable(
      'glasswyrmd_input_recovery_test',
      files(
        'integration/glasswyrmd_input_recovery_test.cpp',
        'helpers/synthetic_input_client.cpp',
      ),
      dependencies: [gwipc_dep, gw_x11_setup_dep],
      include_directories: test_source_root_include,
    )
    test(
      'glasswyrmd-input-recovery',
      glasswyrmd_input_recovery_test,
      args: [glasswyrmd_program.full_path(), gwm_program.full_path(),
             gwcomp_program.full_path()],
      depends: [glasswyrmd_program, gwm_program, gwcomp_program],
      timeout: 30,
    )
  endif
endif

if get_option('glasswyrmd') and get_option('libgwipc')
  gwinput_m8_program = executable(
    'gwinput_m8',
    files('integration/gwinput_m8.cpp'),
    dependencies: gwipc_dep,
    install: false,
  )
  test('gwinput-m8-help', gwinput_m8_program, args: ['--help'], timeout: 10)

  m9_xeyes_path = get_option('m9_xeyes')
  m9_xclock_path = get_option('m9_xclock')
  if (m9_xeyes_path != '' and m9_xclock_path != '' and
      get_option('gwm') and get_option('gwcomp') and
      get_option('render_software'))
    m9_live_profile = find_program('apps/m9_live_profile.sh')
    foreach profile : ['xeyes', 'xclock-analog', 'xclock-digital', 'combined']
      test(
        'm9-live-' + profile,
        m9_live_profile,
        args: [profile, glasswyrmd_program, gwm_program, gwcomp_program,
               gwinput_m8_program, m9_fixed_time_preload,
               meson.project_source_root() / 'tests/compat/m9/m9_trace_summarize',
               meson.project_source_root() / 'tests/fixtures/m9',
               m9_xeyes_path, m9_xclock_path],
        depends: [glasswyrmd_program, gwm_program, gwcomp_program,
                  gwinput_m8_program, m9_fixed_time],
        is_parallel: false,
        timeout: 30,
      )
    endforeach
  endif

  published_buffer_test = executable(
    'published_buffer_test',
    files('unit/published_buffer_test.cpp'),
    dependencies: gw_server_core_dep,
    include_directories: test_source_root_include,
  )
  test('published-buffer', published_buffer_test, timeout: 10)
  content_presenter_test = executable(
    'content_presenter_test',
    files('unit/content_presenter_test.cpp'),
    dependencies: gw_server_core_dep,
    include_directories: test_source_root_include,
  )
  test('content-presenter', content_presenter_test, timeout: 10)
  cursor_presenter_test = executable(
    'cursor_presenter_test',
    files('unit/cursor_presenter_test.cpp'),
    dependencies: gw_server_core_dep,
    include_directories: test_source_root_include,
  )
  test('cursor-presenter', cursor_presenter_test, timeout: 10)
  lifecycle_projection_test = executable(
    'lifecycle_projection_test',
    files('unit/lifecycle_projection_test.cpp'),
    dependencies: gw_server_core_dep,
    include_directories: test_source_root_include,
  )
  test('lifecycle-projection', lifecycle_projection_test, timeout: 10)
endif

if get_option('gwcomp') and get_option('headless_backend')
  m6_fixture_test = executable(
    'm6_fixture_test',
    files('unit/m6_fixture_test.cpp'),
    dependencies: [gwcomp_options_dep, gwipc_dep, gw_x11_setup_dep],
    include_directories: test_source_root_include,
  )
  test(
    'm6-fixtures',
    m6_fixture_test,
    args: [meson.project_source_root() / 'tests/fixtures/m6'],
    timeout: 10,
  )
  m6_fixture_validate = find_program('tools/m6_fixture_validate.sh')
  test(
    'm6-fixture-checksums',
    m6_fixture_validate,
    args: [meson.project_source_root() / 'tests/fixtures/m6'],
    timeout: 10,
  )

  gwcomp_options_test = executable(
    'gwcomp_options_test',
    files('unit/gwcomp_options_test.cpp'),
    dependencies: gwcomp_options_dep,
    include_directories: test_source_root_include,
  )
  test('gwcomp-options', gwcomp_options_test, timeout: 10)

  gwcomp_metadata_scene_test = executable(
    'gwcomp_metadata_scene_test',
    files('unit/gwcomp_metadata_scene_test.cpp'),
    dependencies: [gwcomp_options_dep, gwipc_dep],
    include_directories: test_source_root_include,
  )
  test('gwcomp-metadata-scene', gwcomp_metadata_scene_test, timeout: 10)

  gwcomp_cursor_surface_test = executable(
    'gwcomp_cursor_surface_test',
    files('unit/gwcomp_cursor_surface_test.cpp'),
    dependencies: [gwcomp_options_dep, gwipc_dep],
    include_directories: test_source_root_include,
  )
  test('gwcomp-cursor-surface', gwcomp_cursor_surface_test, timeout: 10)

  gwcomp_metadata_process_test = executable(
    'gwcomp_metadata_process_test',
    files('integration/gwcomp_metadata_process_test.cpp'),
    dependencies: gwipc_dep,
  )
  test(
    'gwcomp-metadata-process',
    gwcomp_metadata_process_test,
    args: [gwcomp_program.full_path()],
    depends: gwcomp_program,
    timeout: 15,
  )

  gwcomp_process_test = executable(
    'gwcomp_process_test',
    files('integration/gwcomp_process_test.cpp'),
    dependencies: gwipc_dep,
    include_directories: test_source_root_include,
  )
  test(
    'gwcomp-process',
    gwcomp_process_test,
    args: [gwcomp_program.full_path()],
    depends: gwcomp_program,
    timeout: 15,
  )

  gwcomp_golden_test = executable(
    'gwcomp_golden_test',
    files('integration/gwcomp_golden_test.cpp'),
  )
  test(
    'gwcomp-golden',
    gwcomp_golden_test,
    args: [gwcomp_program.full_path(), gwcomp_m4_producer_program.full_path()],
    depends: [gwcomp_program, gwcomp_m4_producer_program],
    timeout: 15,
  )

  gwcomp_scenario_matrix_test = executable(
    'gwcomp_scenario_matrix_test',
    files('integration/gwcomp_scenario_matrix_test.cpp'),
  )
  test(
    'gwcomp-scenario-matrix',
    gwcomp_scenario_matrix_test,
    args: [gwcomp_program.full_path(), gwcomp_m4_producer_program.full_path()],
    depends: [gwcomp_program, gwcomp_m4_producer_program],
    timeout: 75,
  )

  test(
    'gwcomp-m4-producer-help',
    gwcomp_m4_producer_program,
    args: ['--help'],
    depends: gwcomp_m4_producer_program,
    timeout: 10,
  )
endif

if get_option('gwcomp')
gwcomp_session_state_test = executable(
  'gwcomp_session_state_test',
  files('unit/gwcomp_session_state_test.cpp'),
  dependencies: gwcomp_options_dep,
  include_directories: test_source_root_include,
)
test('gwcomp-session-state', gwcomp_session_state_test, timeout: 10)

gwcomp_signal_runtime_test = executable(
  'gwcomp_signal_runtime_test',
  files('unit/gwcomp_signal_runtime_test.cpp'),
  dependencies: gwcomp_options_dep,
  include_directories: test_source_root_include,
)
test('gwcomp-signal-runtime', gwcomp_signal_runtime_test, timeout: 10)

gwcomp_buffer_readiness_test = executable(
  'gwcomp_buffer_readiness_test',
  files('unit/gwcomp_buffer_readiness_test.cpp'),
  dependencies: gwcomp_options_dep,
  include_directories: test_source_root_include,
)
test('gwcomp-buffer-readiness', gwcomp_buffer_readiness_test, timeout: 10)

compositor_geometry_test = executable(
  'compositor_geometry_test',
  files('unit/compositor_geometry_test.cpp'),
  dependencies: gw_compositor_geometry_dep,
  include_directories: test_source_root_include,
)
test('compositor-geometry', compositor_geometry_test, timeout: 10)

compositor_scene_test = executable(
  'compositor_scene_test',
  files('unit/compositor_scene_test.cpp'),
  dependencies: gw_compositor_geometry_dep,
  include_directories: test_source_root_include,
)
test('compositor-scene', compositor_scene_test, timeout: 10)

compositor_buffer_test = executable(
  'compositor_buffer_test',
  files('unit/compositor_buffer_test.cpp'),
  dependencies: gw_compositor_geometry_dep,
  include_directories: test_source_root_include,
)
test('compositor-buffer', compositor_buffer_test, timeout: 10)

software_renderer_test = executable(
  'software_renderer_test',
  files('pixel/software_renderer_test.cpp'),
  dependencies: gw_render_software_dep,
  include_directories: test_source_root_include,
)
test('software-renderer', software_renderer_test, timeout: 10)

software_scene_renderer_test = executable(
  'software_scene_renderer_test',
  files('pixel/software_scene_renderer_test.cpp'),
  dependencies: [gw_render_software_dep, gw_output_dep],
  include_directories: test_source_root_include,
)
test('software-scene-renderer', software_scene_renderer_test, timeout: 10)

software_frame_test = executable(
  'software_frame_test',
  files('output/software_frame_test.cpp'),
  dependencies: gw_output_dep,
  include_directories: test_source_root_include,
)
test('software-frame', software_frame_test, timeout: 10)

frame_dump_staging_test = executable(
  'frame_dump_staging_test',
  files('output/frame_dump_staging_test.cpp'),
  dependencies: gw_output_dep,
  include_directories: test_source_root_include,
)
test('frame-dump-staging', frame_dump_staging_test, timeout: 10)

if get_option('headless_backend')
  headless_output_test = executable(
    'headless_output_test',
    files('unit/headless_output_test.cpp'),
    dependencies: gw_headless_dep,
    include_directories: test_source_root_include,
  )
  test('headless-output', headless_output_test, timeout: 10)

  headless_presenter_test = executable(
    'headless_presenter_test',
    files('output/headless_presenter_test.cpp'),
    dependencies: gw_headless_dep,
    include_directories: test_source_root_include,
  )
  test('headless-presenter', headless_presenter_test, timeout: 10)
endif
endif

if get_option('drm_backend')
  foreach drm_model_test : [
    ['drm-connector-name', 'connector_name_test.cpp'],
    ['drm-connector-selector', 'connector_selector_test.cpp'],
    ['drm-damage-copy', 'damage_copy_test.cpp'],
    ['drm-mode-selector', 'mode_selector_test.cpp'],
    ['drm-pipeline-selector', 'pipeline_selector_test.cpp'],
    ['drm-property-cache', 'property_cache_test.cpp'],
    ['drm-report-serialization', 'drm_report_serialization_test.cpp'],
    ['drm-report-staging', 'drm_report_staging_test.cpp'],
  ]
    drm_test_executable = executable(
      drm_model_test[0].underscorify(),
      files('drm' / drm_model_test[1]),
      dependencies: gw_drm_model_dep,
      include_directories: test_source_root_include,
    )
    test(drm_model_test[0], drm_test_executable, timeout: 10)
  endforeach

  drm_dumb_buffer_test = executable(
    'drm_dumb_buffer_test',
    files('drm/dumb_buffer_test.cpp'),
    dependencies: [gw_drm_model_dep, gw_output_dep],
    include_directories: test_source_root_include,
  )
  test('drm-dumb-buffer', drm_dumb_buffer_test, timeout: 10)

  drm_presentation_transaction_test = executable(
    'drm_presentation_transaction_test',
    files('drm/presentation_transaction_test.cpp'),
    dependencies: [gwcomp_options_dep, gwipc_dep,
                   gw_compositor_geometry_dep, gw_render_software_dep,
                   gw_output_dep] + gwcomp_backend_dependencies,
    include_directories: test_source_root_include,
  )
  test('drm-presentation-transaction', drm_presentation_transaction_test,
       timeout: 10)

  drm_presenter_test = executable(
    'drm_presenter_test',
    files('drm/drm_presenter_test.cpp'),
    dependencies: gw_drm_presenter_dep,
    include_directories: test_source_root_include,
  )
  test('drm-presenter', drm_presenter_test, timeout: 10)

  drm_ipc_integration_test = executable(
    'drm_ipc_integration_test',
    files(
      'drm/drm_ipc_integration_test.cpp',
      'drm/drm_ipc_test_fixture.cpp',
    ),
    dependencies: [gwcomp_options_dep, gwipc_dep, gw_drm_presenter_dep],
    include_directories: test_source_root_include,
  )
  test('drm-ipc-integration', drm_ipc_integration_test, timeout: 15)

  foreach drm_api_test : [
    ['drm-device-api', 'device_api_test.cpp'],
    ['drm-fake-events', 'fake_drm_event_test.cpp'],
    ['drm-kms-api', 'kms_api_test.cpp'],
  ]
    drm_api_test_executable = executable(
      drm_api_test[0].underscorify(),
      files('drm' / drm_api_test[1]),
      dependencies: gw_drm_api_dep,
      include_directories: test_source_root_include,
    )
    test(drm_api_test[0], drm_api_test_executable, timeout: 10)
  endforeach

  if get_option('tools')
    drm_probe_test = executable(
      'drm_probe_test',
      files('drm/drm_probe_test.cpp'),
      dependencies: gw_drm_probe_dep,
      include_directories: test_source_root_include,
    )
    test('drm-probe', drm_probe_test, timeout: 10)
  endif

  vt_session_test = executable(
    'vt_session_test',
    files('session/vt_session_test.cpp'),
    dependencies: gw_session_dep,
    include_directories: test_source_root_include,
  )
  test('vt-session', vt_session_test, timeout: 10)

  external_session_test = executable(
    'external_session_test',
    files('session/external_session_test.cpp'),
    dependencies: gw_session_dep,
    include_directories: test_source_root_include,
  )
  test('external-session', external_session_test, timeout: 10)
endif

if get_option('libgwipc')
  gwipc_sync_c_consumer = executable(
    'gwipc_sync_c_consumer',
    files('install/gwipc_sync_c_consumer.c'),
    dependencies: gwipc_dep,
    install: false,
  )
  test('gwipc-api-0.7-sync-c-consumer', gwipc_sync_c_consumer, timeout: 10)
  gwipc_sync_cpp_consumer = executable(
    'gwipc_sync_cpp_consumer',
    files('install/gwipc_sync_cpp_consumer.cpp'),
    dependencies: gwipc_dep,
    install: false,
  )
  test('gwipc-api-0.7-sync-cpp-consumer', gwipc_sync_cpp_consumer, timeout: 10)

  gwipc_session_c_consumer = executable(
    'gwipc_session_c_consumer',
    files('install/gwipc_session_c_consumer.c'),
    dependencies: gwipc_dep,
    install: false,
  )
  test('gwipc-api-0.6-session-c-consumer', gwipc_session_c_consumer,
       timeout: 10)
  gwipc_session_cpp_consumer = executable(
    'gwipc_session_cpp_consumer',
    files('install/gwipc_session_cpp_consumer.cpp'),
    dependencies: gwipc_dep,
    install: false,
  )
  test('gwipc-api-0.6-session-cpp-consumer', gwipc_session_cpp_consumer,
       timeout: 10)

  public_control_api_test = executable(
    'public_control_api_test',
    files('unit/ipc/public_control_api_test.cpp'),
    dependencies: gwipc_dep,
  )
  test('gwipc-public-control-api', public_control_api_test, timeout: 10)
  gwipc_wire_codec_test = executable(
    'gwipc_wire_codec_test',
    files('unit/ipc/wire_codec_test.cpp'),
    dependencies: gwipc_wire_dep,
    include_directories: test_source_root_include,
  )
  test('gwipc-wire-codec', gwipc_wire_codec_test, timeout: 10)
  test('gwipc-envelope', gwipc_wire_codec_test, timeout: 10)
  test('gwipc-control-codec', gwipc_wire_codec_test, timeout: 10)
  test('gwipc-snapshot-codec', gwipc_wire_codec_test, timeout: 10)

  gwipc_contract_codec_test = executable(
    'gwipc_contract_codec_test',
    files('unit/ipc/contract_codec_test.cpp'),
    dependencies: gwipc_wire_dep,
    include_directories: test_source_root_include,
  )
  test('gwipc-compositor-contract', gwipc_contract_codec_test, timeout: 10)

  gwipc_buffer_sync_validation_test = executable(
    'gwipc_buffer_sync_validation_test',
    files(
      'unit/ipc/buffer_sync_validation_test.cpp',
      '../src/ipc/connection_validation.cpp',
      '../src/ipc/message.cpp',
    ),
    dependencies: [gwipc_dep, gwipc_wire_dep],
    include_directories: test_source_root_include,
  )
  test('gwipc-buffer-sync-validation', gwipc_buffer_sync_validation_test,
       timeout: 10)

  gwipc_policy_contract_test = executable(
    'gwipc_policy_contract_test',
    files('unit/ipc/policy_contract_test.cpp'),
    dependencies: gwipc_wire_dep,
    include_directories: test_source_root_include,
  )
  test('gwipc-policy-contract', gwipc_policy_contract_test, timeout: 10)

  gwipc_lifecycle_contract_test = executable(
    'gwipc_lifecycle_contract_test',
    files('unit/ipc/lifecycle_contract_test.cpp'),
    dependencies: gwipc_wire_dep,
    include_directories: test_source_root_include,
  )
  test('gwipc-lifecycle-contract', gwipc_lifecycle_contract_test, timeout: 10)

  gwipc_input_contract_test = executable(
    'gwipc_input_contract_test',
    files('unit/ipc/input_contract_test.cpp'),
    dependencies: [gwipc_wire_dep, gwipc_dep],
    include_directories: test_source_root_include,
  )
  test('gwipc-input-contract', gwipc_input_contract_test, timeout: 10)

  gwipc_public_contract_api_test = executable(
    'gwipc_public_contract_api_test',
    files('unit/ipc/public_contract_api_test.cpp'),
    dependencies: gwipc_dep,
  )
  test('gwipc-public-contract-api', gwipc_public_contract_api_test, timeout: 10)

  gwipc_decoder_fuzz_test = executable(
    'gwipc_decoder_fuzz_test',
    files('unit/ipc/decoder_fuzz_test.cpp'),
    dependencies: gwipc_wire_dep,
    include_directories: test_source_root_include,
  )
  test('gwipc-malformed', gwipc_decoder_fuzz_test, timeout: 15)

  gwipc_wire_probe = executable(
    'gwipc_wire_probe',
    files('integration/gwipc_wire_probe.c'),
    dependencies: gwipc_dep,
  )

  gwipc_transport_test = executable(
    'gwipc_transport_test',
    files('integration/gwipc_transport_test.cpp'),
    dependencies: [gwipc_dep, gwipc_wire_dep],
    include_directories: test_source_root_include,
  )
  test('gwipc-transport', gwipc_transport_test, timeout: 10)

  gwipc_endpoint_test = executable(
    'gwipc_endpoint_test',
    files('integration/gwipc_endpoint_test.cpp'),
    dependencies: gwipc_dep,
  )
  test('gwipc-endpoint', gwipc_endpoint_test, timeout: 10)

  gwipc_fault_test = executable(
    'gwipc_fault_test',
    files(
      'integration/gwipc_fault_test.cpp',
      '../src/ipc/message.cpp',
    ),
    dependencies: [gwipc_dep, gwipc_wire_dep],
    include_directories: test_source_root_include,
  )
  test('gwipc-fault-atomicity', gwipc_fault_test, timeout: 10)

  gwipc_protocol_edge_test = executable(
    'gwipc_protocol_edge_test',
    files(
      'integration/gwipc_protocol_edge_test.cpp',
      '../src/ipc/message.cpp',
    ),
    dependencies: [gwipc_dep, gwipc_wire_dep],
    include_directories: test_source_root_include,
  )
  test('gwipc-protocol-edges', gwipc_protocol_edge_test, timeout: 10)

  gwipc_probe_server = executable(
    'gwipc_probe_server',
    files('integration/gwipc_probe_server.cpp'),
    dependencies: [gwipc_dep, gwipc_wire_dep],
    include_directories: test_source_root_include,
  )
  gwipc_probe_client = executable(
    'gwipc_probe_client',
    files('integration/gwipc_probe_client.cpp'),
    dependencies: [gwipc_dep, gwipc_wire_dep],
    include_directories: test_source_root_include,
  )

  gwipc_symbols_test = find_program('tools/gwipc_symbols_test.sh')
  test(
    'gwipc-public-symbols',
    gwipc_symbols_test,
    args: [gwipc_library.full_path()],
    depends: gwipc_library,
    timeout: 10,
  )
endif

x11_setup_parser_test = executable(
  'x11_setup_parser_test',
  files('protocol/x11_setup_parser_test.cpp'),
  dependencies: gw_x11_setup_dep,
  include_directories: test_source_root_include,
)
test('x11-setup-parser', x11_setup_parser_test, timeout: 10)

x11_setup_reply_test = executable(
  'x11_setup_reply_test',
  files('protocol/x11_setup_reply_test.cpp'),
  dependencies: gw_x11_setup_dep,
  include_directories: test_source_root_include,
)
test('x11-setup-reply', x11_setup_reply_test, timeout: 10)

x11_request_framer_test = executable(
  'x11_request_framer_test',
  files('protocol/x11_request_framer_test.cpp'),
  dependencies: gw_x11_setup_dep,
  include_directories: test_source_root_include,
)
test('x11-request-framer', x11_request_framer_test, timeout: 10)

x11_reply_atom_test = executable(
  'x11_reply_atom_test',
  files('protocol/x11_reply_atom_test.cpp'),
  dependencies: gw_x11_setup_dep,
  include_directories: test_source_root_include,
)
test('x11-reply-atom', x11_reply_atom_test, timeout: 10)

if get_option('glasswyrmd')
  foreach unit_case : [
    ['resource-id', 'resource_id_test.cpp'],
    ['resource-table', 'resource_table_test.cpp'],
    ['window-tree', 'window_tree_test.cpp'],
    ['server-lifecycle-state', 'server_lifecycle_state_test.cpp'],
    ['local-lifecycle-dispatch', 'local_lifecycle_dispatch_test.cpp'],
    ['lifecycle-coordinator', 'lifecycle_coordinator_test.cpp'],
    ['atom-table', 'atom_table_test.cpp'],
    ['property-store', 'property_store_test.cpp'],
    ['request-work-budget', 'request_work_budget_test.cpp'],
    ['compatibility-query-dispatch', 'compatibility_query_dispatch_test.cpp'],
    ['m12-protocol-foundation', 'm12_protocol_foundation_test.cpp'],
    ['mit-shm', 'mit_shm_test.cpp'],
    ['xfixes-damage', 'xfixes_damage_test.cpp'],
    ['render-ops', 'render_ops_test.cpp'],
    ['picture-model', 'picture_model_test.cpp'],
    ['composite-state', 'composite_state_test.cpp'],
    ['render-dispatch', 'render_dispatch_test.cpp'],
    ['composite-dispatch', 'composite_dispatch_test.cpp'],
    ['randr-reporting', 'randr_reporting_test.cpp'],
    ['randr-config', 'randr_config_test.cpp'],
    ['randr-event-wire', 'randr_event_wire_test.cpp'],
    ['ewmh-root', 'ewmh_root_test.cpp'],
    ['ewmh-policy', 'ewmh_policy_test.cpp'],
    ['ewmh-client-message', 'ewmh_client_message_test.cpp'],
  ]
    unit_program = executable(
      unit_case[0],
      files('unit/' + unit_case[1]),
      dependencies: gw_server_state_dep,
      include_directories: test_source_root_include,
    )
    unit_timeout = 10
    if unit_case[0] == 'window-tree'
      unit_timeout = 30
    endif
    test(unit_case[0], unit_program, timeout: unit_timeout)
  endforeach

  x11_fake_client_lib = static_library(
    'x11-fake-client',
    files(
      'helpers/x11_fake_client.cpp',
      'helpers/x11_request_builder.cpp',
      'integration/server_fixture.cpp',
    ),
    dependencies: gw_x11_setup_dep,
    include_directories: test_source_root_include,
  )
  x11_fake_client_dep = declare_dependency(
    link_with: x11_fake_client_lib,
    dependencies: gw_x11_setup_dep,
    include_directories: test_source_root_include,
  )

  foreach integration_case : [
    ['glasswyrmd-handshake', 'glasswyrmd_handshake_test.cpp'],
    ['glasswyrmd-malformed-client', 'glasswyrmd_malformed_client_test.cpp'],
    ['glasswyrmd-lifecycle', 'glasswyrmd_lifecycle_test.cpp'],
    ['glasswyrmd-core-requests', 'glasswyrmd_core_requests_test.cpp'],
    ['glasswyrmd-selection', 'glasswyrmd_selection_test.cpp'],
    ['glasswyrmd-m11-isolation', 'glasswyrmd_m11_isolation_test.cpp'],
  ]
    integration_program = executable(
      integration_case[0],
      files('integration/' + integration_case[1]),
      dependencies: x11_fake_client_dep,
      include_directories: test_source_root_include,
    )
    test(
      integration_case[0],
      integration_program,
      args: [glasswyrmd_program.full_path()],
      depends: glasswyrmd_program,
      timeout: 15,
    )
  endforeach

  x11_setup_probe = executable(
    'x11_setup_probe',
    files('integration/x11_setup_probe.cpp'),
    dependencies: x11_fake_client_dep,
    include_directories: test_source_root_include,
  )

  x11_milestone2_probe = executable(
    'x11_milestone2_probe',
    files('integration/x11_milestone2_probe.cpp'),
    dependencies: x11_fake_client_dep,
    include_directories: test_source_root_include,
  )

  x11_milestone6_probe = executable(
    'x11_milestone6_probe',
    files('integration/x11_milestone6_probe.cpp'),
    dependencies: x11_fake_client_dep,
    include_directories: test_source_root_include,
  )
  x11_milestone7_probe = executable(
    'x11_milestone7_probe',
    files('integration/x11_milestone7_probe.cpp'),
    dependencies: x11_fake_client_dep,
    include_directories: test_source_root_include,
  )
  if get_option('libgwipc')
    x11_milestone8_probe = executable(
      'x11_milestone8_probe',
      files(
        'integration/x11_milestone8_probe.cpp',
        'helpers/synthetic_input_client.cpp',
      ),
      dependencies: [x11_fake_client_dep, gwipc_dep],
      include_directories: test_source_root_include,
    )
  endif

  xcb_test_dependency = dependency('xcb', required: false)
  if xcb_test_dependency.found()
    m6_restart_hold_probe = executable(
      'm6_restart_hold_probe',
      'xcb/m6_restart_hold_probe.c',
      dependencies: xcb_test_dependency,
      install: false,
    )
    xcb_setup_probe = executable(
      'xcb_setup_probe',
      files('xcb/xcb_setup_probe.c'),
      dependencies: xcb_test_dependency,
    )
    xcb_milestone2_probe = executable(
      'xcb_milestone2_probe',
      files('xcb/xcb_milestone2_probe.c'),
      dependencies: xcb_test_dependency,
    )
    xcb_milestone6_probe = executable(
      'xcb_milestone6_probe',
      files('xcb/xcb_milestone6_probe.c'),
      dependencies: xcb_test_dependency,
    )
    xcb_milestone7_probe = executable(
      'xcb_milestone7_probe',
      files('xcb/xcb_milestone7_probe.c'),
      dependencies: xcb_test_dependency,
    )
    m11_selection_probe = executable(
      'm11_selection_probe',
      files('xcb/m11_selection_probe.cpp'),
      dependencies: xcb_test_dependency,
      install: false,
    )
    m11_xterm_geometry_probe = executable(
      'm11_xterm_geometry_probe',
      files('xcb/m11_xterm_geometry_probe.cpp'),
      dependencies: xcb_test_dependency,
      install: false,
    )
    if get_option('libgwipc')
      xcb_milestone8_probe = executable(
        'xcb_milestone8_probe',
        files(
          'xcb/xcb_milestone8_probe.cpp',
          'helpers/synthetic_input_client.cpp',
        ),
        dependencies: [xcb_test_dependency, gwipc_dep],
        include_directories: test_source_root_include,
      )
      m8_restart_hold_probe = executable(
        'm8_restart_hold_probe',
        files(
          'xcb/m8_restart_hold_probe.cpp',
          'helpers/synthetic_input_client.cpp',
        ),
        dependencies: [xcb_test_dependency, gwipc_dep],
        include_directories: test_source_root_include,
      )
    endif
    m7_restart_hold_probe = executable(
      'm7_restart_hold_probe',
      files('xcb/m7_restart_hold_probe.c'),
      dependencies: xcb_test_dependency,
    )
  endif
endif

m11_xterm_acceptance = find_program('apps/m11_xterm_acceptance')
m11_xterm_acceptance_test = find_program('apps/m11_xterm_acceptance_test.sh')
test(
  'm11-xterm-acceptance',
  m11_xterm_acceptance_test,
  args: [m11_xterm_acceptance],
  timeout: 10,
)

gw_vm_cli_test = find_program('tools/gw_vm_cli_test.sh')
test(
  'gw-vm-cli',
  gw_vm_cli_test,
  args: [meson.project_source_root()],
  timeout: 60,
)

source_layout_test = find_program('tools/source_layout_test.sh')
test(
  'source-layout',
  source_layout_test,
  timeout: 30,
)
