if get_option('glasswyrmd')
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
    output_configuration_coordinator_test = executable(
      'output_configuration_coordinator_test',
      files(tests_source_dir / 'unit/output_configuration_coordinator_test.cpp'),
      dependencies: gw_server_core_dep,
      include_directories: test_source_root_include,
    )
    test('output-configuration-coordinator',
         output_configuration_coordinator_test, timeout: 10)
    output_control_peer_process_test = executable(
      'output_control_peer_process_test',
      files(tests_source_dir / 'integration/output_control_peer_process_test.cpp'),
      dependencies: gw_server_core_dep,
      include_directories: test_source_root_include,
    )
    test('output-control-peer-process', output_control_peer_process_test,
         timeout: 10)
    output_control_windows_test = executable(
      'output_control_windows_test',
      files(tests_source_dir / 'unit/output_control_windows_test.cpp'),
      dependencies: gw_server_core_dep,
      include_directories: test_source_root_include,
    )
    test('output-control-windows', output_control_windows_test, timeout: 10)
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
endif
