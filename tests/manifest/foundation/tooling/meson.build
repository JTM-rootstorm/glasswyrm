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

gw_hw_test = find_program(tests_source_dir / 'hardware/m14/gw_hw_test.py')
test(
  'gw-hw-parser-doctor-dry-run',
  gw_hw_test,
  timeout: 30,
)
