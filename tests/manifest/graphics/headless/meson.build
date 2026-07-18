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

  gwcomp_output_model_presentation_test = executable(
    'gwcomp_output_model_presentation_test',
    files(tests_source_dir / 'unit/gwcomp_output_model_presentation_test.cpp'),
    dependencies: [gwcomp_options_dep, gwipc_dep],
    include_directories: test_source_root_include,
  )
  test(
    'gwcomp-output-model-presentation',
    gwcomp_output_model_presentation_test,
    timeout: 10,
  )

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

  gwcomp_output_inventory_process_test = executable(
    'gwcomp_output_inventory_process_test',
    files(tests_source_dir / 'integration/gwcomp_output_inventory_process_test.cpp'),
    dependencies: gwipc_dep,
  )
  test(
    'gwcomp-output-inventory-process',
    gwcomp_output_inventory_process_test,
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
