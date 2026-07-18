if get_option('glasswyrmd')
  if get_option('libinput_backend')
    xkb_keymap_test = executable(
      'xkb_keymap_test',
      files(tests_source_dir / 'unit/xkb_keymap_test.cpp'),
      dependencies: gw_xkb_keymap_dep,
      include_directories: test_source_root_include,
    )
    test('xkb-keymap', xkb_keymap_test, timeout: 10)
    repeat_timer_test = executable(
      'repeat_timer_test',
      files(tests_source_dir / 'unit/repeat_timer_test.cpp'),
      dependencies: gw_xkb_keymap_dep,
      include_directories: test_source_root_include,
    )
    test('repeat-timer', repeat_timer_test, timeout: 10)
    libinput_path_allowlist_test = executable(
      'libinput_path_allowlist_test',
      files(tests_source_dir / 'unit/libinput_path_allowlist_test.cpp'),
      dependencies: gw_xkb_keymap_dep,
      include_directories: test_source_root_include,
    )
    test('libinput-path-allowlist', libinput_path_allowlist_test, timeout: 10)
    libinput_backend_test = executable(
      'libinput_backend_test',
      files(tests_source_dir / 'unit/libinput_backend_test.cpp'),
      dependencies: gw_xkb_keymap_dep,
      include_directories: test_source_root_include,
    )
    test('libinput-backend', libinput_backend_test, timeout: 10)
    real_input_controller_test = executable(
      'real_input_controller_test',
      files(tests_source_dir / 'unit/real_input_controller_test.cpp'),
      dependencies: gw_server_core_dep,
      include_directories: test_source_root_include,
    )
    test('real-input-controller', real_input_controller_test, timeout: 10)
    real_absolute_pointer_routing_test = executable(
      'real_absolute_pointer_routing_test',
      files(tests_source_dir / 'integration/real_absolute_pointer_routing_test.cpp'),
      dependencies: gw_server_core_dep,
      include_directories: test_source_root_include,
    )
    test('real-absolute-pointer-routing',
         real_absolute_pointer_routing_test, timeout: 10)
  endif

  input_foundation_test = executable(
    'input_foundation_test',
    files(tests_source_dir / 'unit/input_foundation_test.cpp'),
    dependencies: gw_server_state_dep,
    include_directories: test_source_root_include,
  )
  test('input-foundation', input_foundation_test, timeout: 10)
  keyboard_mapping_dispatch_test = executable(
    'keyboard_mapping_dispatch_test',
    files(tests_source_dir / 'unit/keyboard_mapping_dispatch_test.cpp'),
    dependencies: gw_server_state_dep,
    include_directories: test_source_root_include,
  )
  test('keyboard-mapping-dispatch', keyboard_mapping_dispatch_test,
       timeout: 10)
  cursor_model_test = executable(
    'cursor_model_test',
    files(tests_source_dir / 'unit/cursor_model_test.cpp'),
    dependencies: gw_server_state_dep,
    include_directories: test_source_root_include,
  )
  test('cursor-model', cursor_model_test, timeout: 10)
  cursor_dispatch_test = executable(
    'cursor_dispatch_test',
    files(tests_source_dir / 'unit/cursor_dispatch_test.cpp'),
    dependencies: gw_server_state_dep,
    include_directories: test_source_root_include,
  )
  test('cursor-dispatch', cursor_dispatch_test, timeout: 10)
  selection_store_test = executable(
    'selection_store_test',
    files(tests_source_dir / 'unit/selection_store_test.cpp'),
    dependencies: gw_server_state_dep,
    include_directories: test_source_root_include,
  )
  test('selection-store', selection_store_test, timeout: 10)
  selection_dispatch_test = executable(
    'selection_dispatch_test',
    files(tests_source_dir / 'unit/selection_dispatch_test.cpp'),
    dependencies: gw_server_state_dep,
    include_directories: test_source_root_include,
  )
  test('selection-dispatch', selection_dispatch_test, timeout: 10)
  grab_state_test = executable(
    'grab_state_test',
    files(tests_source_dir / 'unit/grab_state_test.cpp'),
    dependencies: gw_server_state_dep,
    include_directories: test_source_root_include,
  )
  test('grab-state', grab_state_test, timeout: 10)
  grab_dispatch_test = executable(
    'grab_dispatch_test',
    files(tests_source_dir / 'unit/grab_dispatch_test.cpp'),
    dependencies: gw_server_state_dep,
    include_directories: test_source_root_include,
  )
  test('grab-dispatch', grab_dispatch_test, timeout: 10)
  drawable_core_test = executable(
    'drawable_core_test',
    files(tests_source_dir / 'unit/drawable_core_test.cpp'),
    dependencies: gw_server_state_dep,
    include_directories: test_source_root_include,
  )
  test('drawable-core', drawable_core_test, timeout: 10)

  subtree_compositor_test = executable(
    'subtree_compositor_test',
    files(tests_source_dir / 'unit/subtree_compositor_test.cpp'),
    dependencies: gw_server_state_dep,
    include_directories: test_source_root_include,
  )
  test('subtree-compositor', subtree_compositor_test, timeout: 10)
  drawable_resource_test = executable(
    'drawable_resource_test',
    files(tests_source_dir / 'unit/drawable_resource_test.cpp'),
    dependencies: gw_server_state_dep,
    include_directories: test_source_root_include,
  )
  test('drawable-resources', drawable_resource_test, timeout: 10)
  drawable_dispatch_test = executable(
    'drawable_dispatch_test',
    files(tests_source_dir / 'unit/drawable_dispatch_test.cpp'),
    dependencies: gw_server_state_dep,
    include_directories: test_source_root_include,
  )
  test('drawable-dispatch', drawable_dispatch_test, timeout: 10)
  font_dispatch_test = executable(
    'font_dispatch_test',
    files(tests_source_dir / 'unit/font_dispatch_test.cpp'),
    dependencies: gw_server_state_dep,
    include_directories: test_source_root_include,
  )
  test('font-dispatch', font_dispatch_test, timeout: 10)
  glasswyrmd_options_test = executable(
    'glasswyrmd_options_test',
    files(tests_source_dir / 'unit/glasswyrmd_options_test.cpp'),
    dependencies: gw_server_core_dep,
    include_directories: test_source_root_include,
  )
  test('glasswyrmd-options', glasswyrmd_options_test, timeout: 10)
  if get_option('libgwipc')
    screen_geometry_test = executable(
      'screen_geometry_test',
      files(tests_source_dir / 'unit/screen_geometry_test.cpp'),
      dependencies: gw_server_core_dep,
      include_directories: test_source_root_include,
    )
    test('screen-geometry', screen_geometry_test, timeout: 10)
  endif
  compatibility_trace_test = executable(
    'compatibility_trace_test',
    files(tests_source_dir / 'unit/compatibility_trace_test.cpp'),
    dependencies: gw_server_core_dep,
    include_directories: test_source_root_include,
  )
  test('compatibility-trace', compatibility_trace_test, timeout: 10)
  event_router_test = executable(
    'event_router_test',
    files(tests_source_dir / 'unit/event_router_test.cpp'),
    dependencies: gw_server_core_dep,
    include_directories: test_source_root_include,
  )
  test('event-router', event_router_test, timeout: 10)
  client_connection_deferred_test = executable(
    'client_connection_deferred_test',
    files(tests_source_dir / 'unit/client_connection_deferred_test.cpp'),
    dependencies: gw_server_core_dep,
    include_directories: test_source_root_include,
  )
  test('client-connection-deferred', client_connection_deferred_test,
       timeout: 10)
  if get_option('libgwipc') and get_option('gwm') and get_option('gwcomp')
    glasswyrmd_peer_bootstrap_test = executable(
      'glasswyrmd_peer_bootstrap_test',
      files(tests_source_dir / 'integration/glasswyrmd_peer_bootstrap_test.cpp'),
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
      files(tests_source_dir / 'integration/runtime_bridge_transaction_test.cpp'),
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
      files(tests_source_dir / 'integration/glasswyrmd_integrated_startup_test.cpp'),
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
        tests_source_dir / 'integration/glasswyrmd_integrated_lifecycle_test.cpp',
        tests_source_dir / 'helpers/x11_fake_client.cpp',
        tests_source_dir / 'helpers/x11_request_builder.cpp',
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
    if get_option('experimental')
      glasswyrmd_m12_game_profile_test = executable(
        'glasswyrmd_m12_game_profile_test',
        files(
          tests_source_dir / 'integration/glasswyrmd_m12_game_profile_test.cpp',
          tests_source_dir / 'helpers/x11_fake_client.cpp',
          tests_source_dir / 'helpers/x11_request_builder.cpp',
        ),
        dependencies: [gw_x11_setup_dep],
        include_directories: test_source_root_include,
      )
      test(
        'glasswyrmd-m12-game-profile',
        glasswyrmd_m12_game_profile_test,
        args: [glasswyrmd_program.full_path(), gwm_program.full_path(),
               gwcomp_program.full_path()],
        depends: [glasswyrmd_program, gwm_program, gwcomp_program],
        timeout: 45,
      )
      glasswyrmd_integrated_property_test = executable(
        'glasswyrmd_integrated_property_test',
        files(
          tests_source_dir / 'integration/glasswyrmd_integrated_property_test.cpp',
          tests_source_dir / 'helpers/x11_fake_client.cpp',
          tests_source_dir / 'helpers/x11_request_builder.cpp',
        ),
        dependencies: [gw_x11_setup_dep],
        include_directories: test_source_root_include,
      )
      test(
        'glasswyrmd-integrated-property',
        glasswyrmd_integrated_property_test,
        args: [glasswyrmd_program.full_path(), gwm_program.full_path(),
               gwcomp_program.full_path()],
        depends: [glasswyrmd_program, gwm_program, gwcomp_program],
        timeout: 45,
      )
    endif
    glasswyrmd_input_recovery_test = executable(
      'glasswyrmd_input_recovery_test',
      files(
        tests_source_dir / 'integration/glasswyrmd_input_recovery_test.cpp',
        tests_source_dir / 'helpers/synthetic_input_client.cpp',
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
  test('gwinput-m8-help', gwinput_m8_program, args: ['--help'], timeout: 10)

  m9_xeyes_path = get_option('m9_xeyes')
  m9_xclock_path = get_option('m9_xclock')
  if (m9_xeyes_path != '' and m9_xclock_path != '' and
      get_option('gwm') and get_option('gwcomp') and
      get_option('render_software'))
    m9_live_profile = find_program(tests_source_dir / 'apps/m9_live_profile.sh')
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
    files(tests_source_dir / 'unit/published_buffer_test.cpp'),
    dependencies: gw_server_core_dep,
    include_directories: test_source_root_include,
  )
  test('published-buffer', published_buffer_test, timeout: 10)
  test('m12-extension-stress', m12_extension_stress_probe, timeout: 20)
  content_presenter_test = executable(
    'content_presenter_test',
    files(tests_source_dir / 'unit/content_presenter_test.cpp'),
    dependencies: gw_server_core_dep,
    include_directories: test_source_root_include,
  )
  test('content-presenter', content_presenter_test, timeout: 10)
  cursor_presenter_test = executable(
    'cursor_presenter_test',
    files(tests_source_dir / 'unit/cursor_presenter_test.cpp'),
    dependencies: gw_server_core_dep,
    include_directories: test_source_root_include,
  )
  test('cursor-presenter', cursor_presenter_test, timeout: 10)
  lifecycle_projection_test = executable(
    'lifecycle_projection_test',
    files(tests_source_dir / 'unit/lifecycle_projection_test.cpp'),
    dependencies: gw_server_core_dep,
    include_directories: test_source_root_include,
  )
  test('lifecycle-projection', lifecycle_projection_test, timeout: 10)
  compositor_output_inventory_test = executable(
    'compositor_output_inventory_test',
    files(tests_source_dir / 'unit/compositor_output_inventory_test.cpp'),
    dependencies: gw_server_core_dep,
    include_directories: test_source_root_include,
  )
  test('compositor-output-inventory', compositor_output_inventory_test,
       timeout: 10)
endif
