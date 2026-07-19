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

  compositor_vrr_state_test = executable(
    'compositor_vrr_state_test',
    files(tests_source_dir / 'unit/compositor_vrr_state_test.cpp'),
    dependencies: gwcomp_options_dep,
    include_directories: test_source_root_include,
  )
  test('compositor-vrr-state', compositor_vrr_state_test, timeout: 10)

  compositor_vrr_runtime_test = executable(
    'compositor_vrr_runtime_test',
    files(tests_source_dir / 'unit/compositor_vrr_runtime_test.cpp'),
    dependencies: gwcomp_options_dep,
    include_directories: test_source_root_include,
  )
  test('compositor-vrr-runtime', compositor_vrr_runtime_test, timeout: 10)

  gwcomp_vrr_presentation_test = executable(
    'gwcomp_vrr_presentation_test',
    files(tests_source_dir / 'integration/gwcomp_vrr_presentation_test.cpp'),
    dependencies: gwcomp_options_dep,
    include_directories: test_source_root_include,
  )
  test('gwcomp-vrr-presentation', gwcomp_vrr_presentation_test, timeout: 10)

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

compositor_multi_output_scene_test = executable(
  'compositor_multi_output_scene_test',
  files(tests_source_dir / 'unit/compositor_multi_output_scene_test.cpp'),
  dependencies: gw_compositor_geometry_dep,
  include_directories: test_source_root_include,
)
test('compositor-multi-output-scene', compositor_multi_output_scene_test,
     timeout: 10)

output_damage_test = executable(
  'output_damage_test',
  files(tests_source_dir / 'unit/output_damage_test.cpp'),
  dependencies: [gw_render_software_dep, gw_output_dep],
  include_directories: test_source_root_include,
)
test('output-damage-transaction', output_damage_test, timeout: 10)

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

multi_output_software_scene_renderer_test = executable(
  'multi_output_software_scene_renderer_test',
  files(tests_source_dir / 'pixel/multi_output_software_scene_renderer_test.cpp'),
  dependencies: [gw_render_software_dep, gw_output_dep],
  include_directories: test_source_root_include,
)
multi_output_software_scene_renderer_environment = []
if get_option('render_gl')
  multi_output_software_scene_renderer_environment += [
    '__EGL_VENDOR_LIBRARY_FILENAMES=' + (
      tests_source_dir / 'pixel/mesa_egl_vendor.json'
    ),
  ]
endif
test(
  'multi-output-software-scene-renderer',
  multi_output_software_scene_renderer_test,
  env: multi_output_software_scene_renderer_environment,
  timeout: 10,
)

output_renderer_report_test = executable(
  'output_renderer_report_test',
  files(tests_source_dir / 'pixel/output_renderer_report_test.cpp'),
  dependencies: [gw_render_software_dep, gw_output_dep],
  include_directories: test_source_root_include,
)
test('output-renderer-report', output_renderer_report_test, timeout: 10)

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

    headless_vrr_simulation_test = executable(
      'headless_vrr_simulation_test',
      files(tests_source_dir / 'output/headless_vrr_simulation_test.cpp'),
      dependencies: gw_headless_dep,
      include_directories: test_source_root_include,
    )
    test('headless-vrr-simulation', headless_vrr_simulation_test, timeout: 10)

    headless_vrr_presenter_test = executable(
      'headless_vrr_presenter_test',
      files(tests_source_dir / 'output/headless_vrr_presenter_test.cpp'),
      dependencies: [gw_headless_dep, gw_vrr_model_dep],
      include_directories: test_source_root_include,
    )
    test('headless-vrr-presenter', headless_vrr_presenter_test, timeout: 10)

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
