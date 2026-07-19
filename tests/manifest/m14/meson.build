m14_fixture_root = tests_source_dir / 'fixtures/m14'

m14_host_fixture_validator = find_program(
  tests_source_dir / 'compat/m14/validate_host_fixtures.py',
)
test(
  'm14-host-fixtures',
  m14_host_fixture_validator,
  args: [m14_fixture_root],
  timeout: 10,
)

m14_host_fixture_validator_test = find_program(
  tests_source_dir / 'compat/m14/validate_host_fixtures_test.py',
)
test(
  'm14-host-fixture-validator',
  m14_host_fixture_validator_test,
  args: [m14_host_fixture_validator.full_path(), m14_fixture_root],
  timeout: 10,
)

m14_client_state_validator = find_program(
  tests_source_dir / 'compat/m14/validate_client_state.py',
)
test(
  'm14-client-state-fixture',
  m14_client_state_validator,
  args: [m14_fixture_root / 'm14-vrr-client.json'],
  timeout: 10,
)

m14_client_state_validator_test = find_program(
  tests_source_dir / 'compat/m14/validate_client_state_test.py',
)
test(
  'm14-client-state-validator',
  m14_client_state_validator_test,
  timeout: 10,
)

m14_vrr_client_test = executable(
  'm14_vrr_client_test',
  files(
    tests_source_dir / 'apps/m14_vrr_client_test.cpp',
    tests_source_dir / 'apps/m14_vrr_client_options.cpp',
    tests_source_dir / 'apps/m14_vrr_client_support.cpp',
  ),
  include_directories: test_source_root_include,
)
test('m14-vrr-client-unit', m14_vrr_client_test, timeout: 10)

m14_policy_fixture_test = executable(
  'm14_policy_fixture_test',
  files(tests_source_dir / 'unit/output/m14_policy_fixture_test.cpp'),
  dependencies: gw_vrr_model_dep,
  include_directories: test_source_root_include,
)
test(
  'm14-policy-fixtures',
  m14_policy_fixture_test,
  args: [m14_fixture_root],
  timeout: 10,
)

if get_option('glasswyrmd')
  m14_gw_vrr_fixture_test = executable(
    'm14_gw_vrr_fixture_test',
    files(tests_source_dir / 'protocol/m14_gw_vrr_fixture_test.cpp'),
    dependencies: gw_server_state_dep,
    include_directories: test_source_root_include,
  )
  test(
    'm14-gw-vrr-fixtures',
    m14_gw_vrr_fixture_test,
    args: [m14_fixture_root],
    timeout: 10,
  )

  if xcb_test_dependency.found()
    m14_vrr_client = executable(
      'm14_vrr_client',
      files(
        tests_source_dir / 'apps/m14_vrr_client.cpp',
        tests_source_dir / 'apps/m14_vrr_client_options.cpp',
        tests_source_dir / 'apps/m14_vrr_client_support.cpp',
      ),
      dependencies: xcb_test_dependency,
      include_directories: test_source_root_include,
    )
    test(
      'm14-vrr-client-help',
      m14_vrr_client,
      args: ['--help'],
      timeout: 10,
    )
    test(
      'm14-vrr-client-self-test',
      m14_vrr_client,
      args: ['--self-test'],
      timeout: 10,
    )
  endif
endif

if get_option('tools') and get_option('libgwipc')
  m14_output_tool_fixture_test = find_program(
    tests_source_dir / 'tools/m14_output_tool_fixture_test.sh',
  )
  test(
    'm14-output-tool-fixtures',
    m14_output_tool_fixture_test,
    args: [output_tools_fake_server, gwinfo_program, gwout_program,
           m14_fixture_root],
    depends: [output_tools_fake_server, gwinfo_program, gwout_program],
    timeout: 20,
  )
endif
