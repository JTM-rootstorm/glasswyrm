if get_option('gwm')
  gwm_options_test = executable(
    'gwm_options_test',
    files(tests_source_dir / 'unit/gwm_options_test.cpp'),
    dependencies: gwm_options_dep,
    include_directories: test_source_root_include,
  )
  test('gwm-options', gwm_options_test, timeout: 10)

  wm_policy_test = executable(
    'wm_policy_test',
    files(tests_source_dir / 'wm/policy_engine_test.cpp'),
    dependencies: gw_wm_policy_dep,
    include_directories: test_source_root_include,
  )
  test('wm-policy', wm_policy_test, timeout: 10)
  multi_output_policy_test = executable(
    'multi_output_policy_test',
    files(tests_source_dir / 'wm/multi_output_policy_test.cpp'),
    dependencies: gw_wm_policy_dep,
    include_directories: test_source_root_include,
  )
  test('multi-output-policy', multi_output_policy_test, timeout: 10)
  interactive_policy_test = executable(
    'interactive_policy_test',
    files(tests_source_dir / 'wm/interactive_policy_test.cpp'),
    dependencies: gw_wm_policy_dep,
    include_directories: test_source_root_include,
  )
  test('interactive-policy', interactive_policy_test, timeout: 10)

  foreach wm_vrr_test : [
    ['wm-vrr-policy', 'vrr_policy_test.cpp'],
    ['wm-vrr-validation', 'vrr_validation_test.cpp'],
    ['wm-vrr-hash', 'vrr_hash_test.cpp'],
  ]
    wm_vrr_test_executable = executable(
      wm_vrr_test[0].underscorify(),
      files(tests_source_dir / 'unit/wm' / wm_vrr_test[1]),
      dependencies: gw_wm_policy_dep,
      include_directories: test_source_root_include,
    )
    test(wm_vrr_test[0], wm_vrr_test_executable, timeout: 10)
  endforeach
  if get_option('glasswyrmd')
    interactive_close_test = executable(
      'interactive_close_test',
      files(tests_source_dir / 'wm/interactive_close_test.cpp'),
      dependencies: [gw_wm_policy_dep, gw_server_state_dep],
      include_directories: test_source_root_include,
    )
    test('interactive-close', interactive_close_test, timeout: 10)
  endif

  gwm_process_test = executable(
    'gwm_process_test',
    files(tests_source_dir / 'integration/gwm_process_test.cpp'),
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
    files(tests_source_dir / 'integration/gwm_scenario_matrix_test.cpp'),
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
    files(tests_source_dir / 'integration/gwm_robustness_test.cpp'),
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
