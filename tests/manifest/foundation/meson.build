scaffold_test = executable(
  'scaffold_test',
  files(tests_source_dir / 'unit/scaffold_test.cpp'),
  dependencies: gw_scaffold_dep,
)

test('scaffold', scaffold_test)

output_scale_test = executable(
  'output_scale_test',
  files(tests_source_dir / 'unit/output/output_scale_test.cpp'),
  dependencies: gw_output_model_dep,
  include_directories: test_source_include,
)
test('output-scale', output_scale_test)

output_transform_test = executable(
  'output_transform_test',
  files(tests_source_dir / 'unit/output/output_transform_test.cpp'),
  dependencies: gw_output_model_dep,
  include_directories: test_source_include,
)
test('output-transform', output_transform_test)

output_identity_test = executable(
  'output_identity_test',
  files(tests_source_dir / 'unit/output/output_identity_test.cpp'),
  dependencies: gw_output_model_dep,
  include_directories: test_source_include,
)
test('output-identity', output_identity_test)

output_layout_test = executable(
  'output_layout_test',
  files(tests_source_dir / 'unit/output/output_layout_test.cpp'),
  dependencies: gw_output_model_dep,
  include_directories: test_source_include,
)
test('output-layout', output_layout_test)

output_mapping_test = executable(
  'output_mapping_test',
  files(tests_source_dir / 'unit/output/output_mapping_test.cpp'),
  dependencies: gw_output_model_dep,
  include_directories: test_source_include,
)
test('output-mapping', output_mapping_test)

output_damage_mapping_test = executable(
  'output_damage_mapping_test',
  files(tests_source_dir / 'unit/output/output_damage_mapping_test.cpp'),
  dependencies: gw_output_model_dep,
  include_directories: test_source_include,
)
test('output-damage-mapping', output_damage_mapping_test)

if get_option('glasswyrmd') and get_option('gwm') and get_option('gwcomp')
  session_launcher_test = executable(
    'session_launcher_test',
    files(tests_source_dir / 'unit/session_launcher_test.cpp'),
    dependencies: [gw_session_launcher_dep, dependency('threads')],
    include_directories: test_source_root_include,
  )
  test('session-launcher', session_launcher_test, timeout: 10)
  test('session-launcher-help', glasswyrm_session_program, args: ['--help'],
       depends: glasswyrm_session_program, timeout: 10)
endif

if host_machine.system() == 'linux'
  test('gw-uinput-m11-help', gw_uinput_m11_program, args: ['--help'],
       timeout: 10)
  uinput_m11_protocol_test = executable(
    'uinput_m11_protocol_test',
    files(tests_source_dir / 'unit/uinput_m11_protocol_test.cpp'),
    dependencies: uinput_m11_protocol_dep,
  )
  test('gw-uinput-m11-protocol', uinput_m11_protocol_test, timeout: 10)
endif

x11_event_test = executable(
  'x11_event_test',
  files(tests_source_dir / 'protocol/x11_event_test.cpp'),
  dependencies: gw_x11_setup_dep,
)
test('x11-structural-events', x11_event_test, timeout: 10)

x11_input_event_test = executable(
  'x11_input_event_test',
  files(tests_source_dir / 'protocol/x11_input_event_test.cpp'),
  dependencies: gw_x11_setup_dep,
  include_directories: test_source_root_include,
)
test('x11-input-events', x11_input_event_test, timeout: 10)

x11_exposure_event_test = executable(
  'x11_exposure_event_test',
  files(tests_source_dir / 'protocol/x11_exposure_event_test.cpp'),
  dependencies: gw_x11_setup_dep,
  include_directories: test_source_root_include,
)
test('x11-exposure-events', x11_exposure_event_test, timeout: 10)

m7_fixture_validate = find_program(tests_source_dir / 'tools/m7_fixture_validate.sh')
test(
  'm7-fixture-checksums',
  m7_fixture_validate,
  args: [meson.project_source_root() / 'tests/fixtures/m7'],
  timeout: 10,
)
m8_fixture_validate = find_program(tests_source_dir / 'tools/m8_fixture_validate.sh')
test(
  'm8-fixture-checksums',
  m8_fixture_validate,
  args: [meson.project_source_root() / 'tests/fixtures/m8'],
  timeout: 10,
)
m9_fixture_validate = find_program(tests_source_dir / 'tools/m9_fixture_validate.sh')
test(
  'm9-fixture-checksums',
  m9_fixture_validate,
  args: [meson.project_source_root()],
  timeout: 10,
)
m9_trace_summarize_test = find_program(tests_source_dir / 'compat/m9/m9_trace_summarize_test.py')
test(
  'm9-trace-summarize',
  m9_trace_summarize_test,
  args: [tests_source_dir / 'compat/m9/m9_trace_summarize'],
  timeout: 10,
)
m11_trace_summarize_test = find_program(tests_source_dir / 'compat/m11/m11_trace_summarize_test.py')
test(
  'm11-trace-summarize',
  m11_trace_summarize_test,
  args: [tests_source_dir / 'compat/m11/m11_trace_summarize'],
  timeout: 10,
)
m11_fixture_validate_test = find_program(tests_source_dir / 'tools/m11_fixture_validate_test.py')
test(
  'm11-fixture-validator',
  m11_fixture_validate_test,
  args: [tests_source_dir / 'tools/m11_fixture_validate.py'],
  timeout: 10,
)
test(
  'm11-fixture-checksum',
  find_program(tests_source_dir / 'tools/m11_fixture_validate.py'),
  args: [meson.project_source_root() / 'tests/fixtures/m11'],
  timeout: 10,
)
m12_fixture_validate = find_program(tests_source_dir / 'compat/m12/validate_fixtures.py')
test(
  'm12-fixture-checksum',
  m12_fixture_validate,
  args: [meson.project_source_root() / 'tests/fixtures/m12'],
  timeout: 10,
)

x11_lifecycle_request_test = executable(
  'x11_lifecycle_request_test',
  files(tests_source_dir / 'protocol/x11_lifecycle_request_test.cpp'),
  dependencies: gw_x11_setup_dep,
)
test('x11-lifecycle-request-decode', x11_lifecycle_request_test, timeout: 10)

m9_fixed_time_test = executable(
  'm9_fixed_time_test',
  files(tests_source_dir / 'apps/m9_fixed_time_test.cpp'),
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
  files(tests_source_dir / 'apps/m9_app_runner.cpp'),
  install: false,
)
m9_runner_fixture = find_program(tests_source_dir / 'apps/m9_runner_fixture.sh')
test(
  'm9-app-runner',
  m9_app_runner,
  args: [
    '--expected-version', '1.2.3',
    '--evidence', tests_build_dir / 'm9-runner-evidence',
    '--result', tests_build_dir / 'm9-runner-result.json',
    '--timeout-ms', '1000', '--', m9_runner_fixture,
    tests_build_dir / 'm9-runner-evidence',
  ],
  timeout: 10,
)

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
