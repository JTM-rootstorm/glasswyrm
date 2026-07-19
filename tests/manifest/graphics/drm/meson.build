if get_option('drm_backend')
  drm_inventory_test = executable(
    'drm_inventory_test',
    files(tests_source_dir / 'drm/drm_inventory_test.cpp'),
    dependencies: gw_drm_model_dep,
    include_directories: test_source_root_include,
  )
  test('drm-inventory', drm_inventory_test, timeout: 10)

  drm_runtime_selection_test = executable(
    'drm_runtime_selection_test',
    files(tests_source_dir / 'drm/drm_runtime_selection_test.cpp'),
    dependencies: gwcomp_options_dep,
    include_directories: test_source_root_include,
  )
  test('drm-runtime-selection', drm_runtime_selection_test, timeout: 10)

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
      files(tests_source_dir / 'drm' / drm_model_test[1]),
      dependencies: gw_drm_model_dep,
      include_directories: test_source_root_include,
    )
    test(drm_model_test[0], drm_test_executable, timeout: 10)
  endforeach

  drm_dumb_buffer_test = executable(
    'drm_dumb_buffer_test',
    files(tests_source_dir / 'drm/dumb_buffer_test.cpp'),
    dependencies: [gw_drm_model_dep, gw_output_dep],
    include_directories: test_source_root_include,
  )
  test('drm-dumb-buffer', drm_dumb_buffer_test, timeout: 10)

  drm_presentation_transaction_test = executable(
    'drm_presentation_transaction_test',
    files(tests_source_dir / 'drm/presentation_transaction_test.cpp'),
    dependencies: [gwcomp_options_dep, gwipc_dep,
                   gw_compositor_geometry_dep, gw_render_software_dep,
                   gw_output_dep] + gwcomp_backend_dependencies,
    include_directories: test_source_root_include,
  )
  test('drm-presentation-transaction', drm_presentation_transaction_test,
       timeout: 10)

  drm_presenter_test = executable(
    'drm_presenter_test',
    files(tests_source_dir / 'drm/drm_presenter_test.cpp'),
    dependencies: gw_drm_presenter_dep,
    include_directories: test_source_root_include,
  )
  test('drm-presenter', drm_presenter_test, timeout: 10)

  drm_m13_presenter_test = executable(
    'drm_m13_presenter_test',
    files(tests_source_dir / 'drm/drm_m13_presenter_test.cpp'),
    dependencies: gw_drm_presenter_dep,
    include_directories: test_source_root_include,
  )
  test('drm-m13-presenter', drm_m13_presenter_test, timeout: 10)

  drm_ipc_integration_test = executable(
    'drm_ipc_integration_test',
    files(
      tests_source_dir / 'drm/drm_ipc_integration_test.cpp',
      tests_source_dir / 'drm/drm_ipc_test_fixture.cpp',
    ),
    dependencies: [gwcomp_options_dep, gwipc_dep, gw_drm_presenter_dep],
    include_directories: test_source_root_include,
  )
  test('drm-ipc-integration', drm_ipc_integration_test, timeout: 15)

  foreach drm_api_test : [
    ['drm-device-api', 'device_api_test.cpp'],
    ['drm-fake-events', 'fake_drm_event_test.cpp'],
    ['drm-kms-api', 'kms_api_test.cpp'],
    ['drm-vrr-capability-property', '../unit/drm/vrr_capability_property_test.cpp'],
    ['drm-vrr-saved-state', '../unit/drm/vrr_saved_state_test.cpp'],
    ['drm-vrr-timing', '../unit/drm/vrr_timing_test.cpp'],
  ]
    drm_api_test_executable = executable(
      drm_api_test[0].underscorify(),
      files(tests_source_dir / 'drm' / drm_api_test[1]),
      dependencies: gw_drm_api_dep,
      include_directories: test_source_root_include,
    )
    test(drm_api_test[0], drm_api_test_executable, timeout: 10)
  endforeach

  if get_option('tools')
    drm_probe_test = executable(
      'drm_probe_test',
      files(tests_source_dir / 'drm/drm_probe_test.cpp'),
      dependencies: gw_drm_probe_dep,
      include_directories: test_source_root_include,
    )
    test('drm-probe', drm_probe_test, timeout: 10)
  endif

  vt_session_test = executable(
    'vt_session_test',
    files(tests_source_dir / 'session/vt_session_test.cpp'),
    dependencies: gw_session_dep,
    include_directories: test_source_root_include,
  )
  test('vt-session', vt_session_test, timeout: 10)

  external_session_test = executable(
    'external_session_test',
    files(tests_source_dir / 'session/external_session_test.cpp'),
    dependencies: gw_session_dep,
    include_directories: test_source_root_include,
  )
  test('external-session', external_session_test, timeout: 10)
endif
