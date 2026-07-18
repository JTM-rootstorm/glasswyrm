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
