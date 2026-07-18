if get_option('gwcomp') and get_option('headless_backend')
  m6_fixture_test = executable(
    'm6_fixture_test',
    files(tests_source_dir / 'unit/m6_fixture_test.cpp'),
    dependencies: [gwcomp_options_dep, gwipc_dep, gw_x11_setup_dep],
    include_directories: test_source_root_include,
  )
  test(
    'm6-fixtures',
    m6_fixture_test,
    args: [meson.project_source_root() / 'tests/fixtures/m6'],
    timeout: 10,
  )
  m6_fixture_validate = find_program(tests_source_dir / 'tools/m6_fixture_validate.sh')
  test(
    'm6-fixture-checksums',
    m6_fixture_validate,
    args: [meson.project_source_root() / 'tests/fixtures/m6'],
    timeout: 10,
  )

  gwcomp_options_test = executable(
    'gwcomp_options_test',
    files(tests_source_dir / 'unit/gwcomp_options_test.cpp'),
    dependencies: gwcomp_options_dep,
    include_directories: test_source_root_include,
  )
  test('gwcomp-options', gwcomp_options_test, timeout: 10)

  gwcomp_metadata_scene_test = executable(
    'gwcomp_metadata_scene_test',
    files(tests_source_dir / 'unit/gwcomp_metadata_scene_test.cpp'),
    dependencies: [gwcomp_options_dep, gwipc_dep],
    include_directories: test_source_root_include,
  )
  test('gwcomp-metadata-scene', gwcomp_metadata_scene_test, timeout: 10)

  gwcomp_cursor_surface_test = executable(
    'gwcomp_cursor_surface_test',
    files(tests_source_dir / 'unit/gwcomp_cursor_surface_test.cpp'),
    dependencies: [gwcomp_options_dep, gwipc_dep],
    include_directories: test_source_root_include,
  )
  test('gwcomp-cursor-surface', gwcomp_cursor_surface_test, timeout: 10)

  gwcomp_metadata_process_test = executable(
    'gwcomp_metadata_process_test',
    files(tests_source_dir / 'integration/gwcomp_metadata_process_test.cpp'),
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
    files(tests_source_dir / 'integration/gwcomp_process_test.cpp'),
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
    files(tests_source_dir / 'integration/gwcomp_golden_test.cpp'),
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
    files(tests_source_dir / 'integration/gwcomp_scenario_matrix_test.cpp'),
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
  files(tests_source_dir / 'unit/gwcomp_session_state_test.cpp'),
  dependencies: gwcomp_options_dep,
  include_directories: test_source_root_include,
)
test('gwcomp-session-state', gwcomp_session_state_test, timeout: 10)

gwcomp_signal_runtime_test = executable(
  'gwcomp_signal_runtime_test',
  files(tests_source_dir / 'unit/gwcomp_signal_runtime_test.cpp'),
  dependencies: gwcomp_options_dep,
  include_directories: test_source_root_include,
)
test('gwcomp-signal-runtime', gwcomp_signal_runtime_test, timeout: 10)

  gwcomp_buffer_readiness_test = executable(
  'gwcomp_buffer_readiness_test',
  files(tests_source_dir / 'unit/gwcomp_buffer_readiness_test.cpp'),
  dependencies: gwcomp_options_dep,
  include_directories: test_source_root_include,
)
  test('gwcomp-buffer-readiness', gwcomp_buffer_readiness_test, timeout: 10)

  gwcomp_output_inventory_publisher_test = executable(
    'gwcomp_output_inventory_publisher_test',
    files(tests_source_dir / 'unit/gwcomp_output_inventory_publisher_test.cpp'),
    dependencies: [gwcomp_options_dep, gwipc_wire_dep],
    include_directories: test_source_root_include,
  )
  test('gwcomp-output-inventory-publisher',
       gwcomp_output_inventory_publisher_test, timeout: 10)

compositor_geometry_test = executable(
  'compositor_geometry_test',
  files(tests_source_dir / 'unit/compositor_geometry_test.cpp'),
  dependencies: gw_compositor_geometry_dep,
  include_directories: test_source_root_include,
)
test('compositor-geometry', compositor_geometry_test, timeout: 10)

compositor_scene_test = executable(
  'compositor_scene_test',
  files(tests_source_dir / 'unit/compositor_scene_test.cpp'),
  dependencies: gw_compositor_geometry_dep,
  include_directories: test_source_root_include,
)
test('compositor-scene', compositor_scene_test, timeout: 10)

compositor_buffer_test = executable(
  'compositor_buffer_test',
  files(tests_source_dir / 'unit/compositor_buffer_test.cpp'),
  dependencies: gw_compositor_geometry_dep,
  include_directories: test_source_root_include,
)
test('compositor-buffer', compositor_buffer_test, timeout: 10)

software_renderer_test = executable(
  'software_renderer_test',
  files(tests_source_dir / 'pixel/software_renderer_test.cpp'),
  dependencies: gw_render_software_dep,
  include_directories: test_source_root_include,
)
test('software-renderer', software_renderer_test, timeout: 10)

software_scene_renderer_test = executable(
  'software_scene_renderer_test',
  files(tests_source_dir / 'pixel/software_scene_renderer_test.cpp'),
  dependencies: [gw_render_software_dep, gw_output_dep],
  include_directories: test_source_root_include,
)
software_scene_renderer_environment = []
if get_option('render_gl')
  # GLVND otherwise probes every installed EGL vendor. Keep this Mesa
  # surfaceless renderer test deterministic and isolated from unrelated
  # vendor plug-in initialization and process-global state.
  software_scene_renderer_environment += [
    '__EGL_VENDOR_LIBRARY_FILENAMES=' + (
      tests_source_dir / 'pixel/mesa_egl_vendor.json'
    ),
  ]
endif
test(
  'software-scene-renderer',
  software_scene_renderer_test,
  env: software_scene_renderer_environment,
  timeout: 10,
)

software_frame_test = executable(
  'software_frame_test',
  files(tests_source_dir / 'output/software_frame_test.cpp'),
  dependencies: gw_output_dep,
  include_directories: test_source_root_include,
)
test('software-frame', software_frame_test, timeout: 10)

frame_dump_staging_test = executable(
  'frame_dump_staging_test',
  files(tests_source_dir / 'output/frame_dump_staging_test.cpp'),
  dependencies: gw_output_dep,
  include_directories: test_source_root_include,
)
test('frame-dump-staging', frame_dump_staging_test, timeout: 10)

  if get_option('headless_backend')
    headless_inventory_test = executable(
      'headless_inventory_test',
      files(tests_source_dir / 'unit/headless_inventory_test.cpp'),
      dependencies: gw_headless_dep,
      include_directories: test_source_root_include,
    )
    test('headless-inventory', headless_inventory_test, timeout: 10)

    headless_output_test = executable(
    'headless_output_test',
    files(tests_source_dir / 'unit/headless_output_test.cpp'),
    dependencies: gw_headless_dep,
    include_directories: test_source_root_include,
  )
  test('headless-output', headless_output_test, timeout: 10)

  headless_presenter_test = executable(
    'headless_presenter_test',
    files(tests_source_dir / 'output/headless_presenter_test.cpp'),
    dependencies: gw_headless_dep,
    include_directories: test_source_root_include,
  )
  test('headless-presenter', headless_presenter_test, timeout: 10)
endif
endif

if get_option('drm_backend')
  drm_inventory_test = executable(
    'drm_inventory_test',
    files(tests_source_dir / 'drm/drm_inventory_test.cpp'),
    dependencies: gw_drm_model_dep,
    include_directories: test_source_root_include,
  )
  test('drm-inventory', drm_inventory_test, timeout: 10)

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
