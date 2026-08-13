if get_option('libgwipc')
  gwipc_consumer_apis = [
    ['0.9', 'vrr'],
    ['0.8', 'output'],
    ['0.7', 'sync'],
    ['0.6', 'session'],
  ]
  foreach consumer_api : gwipc_consumer_apis
    foreach language : ['c', 'cpp']
      consumer_name = 'gwipc_' + consumer_api[1] + '_' + language + '_consumer'
      consumer_program = executable(
        consumer_name,
        files(tests_source_dir / ('install/' + consumer_name + '.' + language)),
        dependencies: gwipc_dep,
        install: false,
      )
      test(
        'gwipc-api-' + consumer_api[0] + '-' + consumer_api[1] + '-' + language + '-consumer',
        consumer_program,
        timeout: 10,
        suite: test_contract_suites,
      )
    endforeach
  endforeach

  public_control_api_test = executable(
    'public_control_api_test',
    files(tests_source_dir / 'unit/ipc/public_control_api_test.cpp'),
    dependencies: gwipc_dep,
  )
  test('gwipc-public-control-api', public_control_api_test, timeout: 10, suite: test_contract_suites)
  gwipc_wire_codec_test = executable(
    'gwipc_wire_codec_test',
    files(tests_source_dir / 'unit/ipc/wire_codec_test.cpp'),
    dependencies: gwipc_wire_dep,
    include_directories: test_source_root_include,
  )
  test('gwipc-wire-codec', gwipc_wire_codec_test, timeout: 10, suite: test_contract_suites)
  test('gwipc-envelope', gwipc_wire_codec_test, timeout: 10, suite: test_contract_suites)
  test('gwipc-control-codec', gwipc_wire_codec_test, timeout: 10, suite: test_contract_suites)
  test('gwipc-snapshot-codec', gwipc_wire_codec_test, timeout: 10, suite: test_contract_suites)

  gwipc_contract_codec_test = executable(
    'gwipc_contract_codec_test',
    files(tests_source_dir / 'unit/ipc/contract_codec_test.cpp'),
    dependencies: gwipc_wire_dep,
    include_directories: test_source_root_include,
  )
  test('gwipc-compositor-contract', gwipc_contract_codec_test, timeout: 10, suite: test_contract_suites)

  gwipc_output_contract_test = executable(
    'gwipc_output_contract_test',
    files(tests_source_dir / 'unit/ipc/output_contract_test.cpp'),
    dependencies: gwipc_wire_dep,
    include_directories: test_source_root_include,
  )
  test('gwipc-output-contract', gwipc_output_contract_test, timeout: 10, suite: test_contract_suites)

  gwipc_vrr_contract_test = executable(
    'gwipc_vrr_contract_test',
    files(tests_source_dir / 'unit/ipc/vrr_contract_test.cpp'),
    dependencies: gwipc_wire_dep,
    include_directories: test_source_root_include,
  )
  test('gwipc-vrr-contract', gwipc_vrr_contract_test, timeout: 10, suite: test_contract_suites)

  gwipc_buffer_sync_validation_test = executable(
    'gwipc_buffer_sync_validation_test',
    files(
      tests_source_dir / 'unit/ipc/buffer_sync_validation_test.cpp',
      tests_source_dir / '../src/ipc/connection_validation.cpp',
      tests_source_dir / '../src/ipc/connection_output_validation.cpp',
      tests_source_dir / '../src/ipc/message.cpp',
    ),
    dependencies: [gwipc_dep, gwipc_wire_dep],
    include_directories: test_source_root_include,
  )
  test('gwipc-buffer-sync-validation', gwipc_buffer_sync_validation_test,
         suite: test_contract_suites,
       timeout: 10)

  gwipc_vrr_connection_validation_test = executable(
    'gwipc_vrr_connection_validation_test',
    files(
      tests_source_dir / 'unit/ipc/vrr_connection_validation_test.cpp',
      tests_source_dir / '../src/ipc/connection_validation.cpp',
      tests_source_dir / '../src/ipc/connection_output_validation.cpp',
      tests_source_dir / '../src/ipc/message.cpp',
    ),
    dependencies: [gwipc_dep, gwipc_wire_dep],
    include_directories: test_source_root_include,
  )
  test('gwipc-vrr-connection-validation',
       gwipc_vrr_connection_validation_test, timeout: 10,
       suite: test_contract_suites)

  gwipc_policy_contract_test = executable(
    'gwipc_policy_contract_test',
    files(tests_source_dir / 'unit/ipc/policy_contract_test.cpp'),
    dependencies: gwipc_wire_dep,
    include_directories: test_source_root_include,
  )
  test('gwipc-policy-contract', gwipc_policy_contract_test, timeout: 10, suite: test_contract_suites)

  gwipc_lifecycle_contract_test = executable(
    'gwipc_lifecycle_contract_test',
    files(tests_source_dir / 'unit/ipc/lifecycle_contract_test.cpp'),
    dependencies: gwipc_wire_dep,
    include_directories: test_source_root_include,
  )
  test('gwipc-lifecycle-contract', gwipc_lifecycle_contract_test, timeout: 10, suite: test_contract_suites)

  gwipc_input_contract_test = executable(
    'gwipc_input_contract_test',
    files(tests_source_dir / 'unit/ipc/input_contract_test.cpp'),
    dependencies: [gwipc_wire_dep, gwipc_dep],
    include_directories: test_source_root_include,
  )
  test('gwipc-input-contract', gwipc_input_contract_test, timeout: 10, suite: test_contract_suites)

  gwipc_public_contract_api_test = executable(
    'gwipc_public_contract_api_test',
    files(tests_source_dir / 'unit/ipc/public_contract_api_test.cpp'),
    dependencies: gwipc_dep,
  )
  test('gwipc-public-contract-api', gwipc_public_contract_api_test, timeout: 10, suite: test_contract_suites)

  gwipc_public_output_api_test = executable(
    'gwipc_public_output_api_test',
    files(tests_source_dir / 'unit/ipc/public_output_api_test.cpp'),
    dependencies: [gwipc_dep, gwipc_wire_dep],
    include_directories: test_source_root_include,
  )
  test('gwipc-public-output-api', gwipc_public_output_api_test, timeout: 10, suite: test_contract_suites)

  gwipc_public_vrr_api_test = executable(
    'gwipc_public_vrr_api_test',
    files(tests_source_dir / 'unit/ipc/public_vrr_api_test.cpp'),
    dependencies: [gwipc_dep, gwipc_wire_dep],
    include_directories: test_source_root_include,
  )
  test('gwipc-public-vrr-api', gwipc_public_vrr_api_test, timeout: 10, suite: test_contract_suites)

  vrr_membership_hint_test = executable(
    'vrr_membership_hint_test',
    files(tests_source_dir / 'unit/ipc/vrr_membership_hint_test.cpp'),
    include_directories: [test_source_root_include,
                          include_directories('../../../src')],
  )
  test('gwipc-vrr-membership-hint', vrr_membership_hint_test, timeout: 10, suite: test_contract_suites)

  gwipc_decoder_fuzz_test = executable(
    'gwipc_decoder_fuzz_test',
    files(tests_source_dir / 'unit/ipc/decoder_fuzz_test.cpp'),
    dependencies: gwipc_wire_dep,
    include_directories: test_source_root_include,
  )
  test('gwipc-malformed', gwipc_decoder_fuzz_test, timeout: 15, suite: test_contract_suites)

  gwipc_transport_test = executable(
    'gwipc_transport_test',
    files(tests_source_dir / 'integration/gwipc_transport_test.cpp'),
    dependencies: [gwipc_dep, gwipc_wire_dep],
    include_directories: test_source_root_include,
  )
  test('gwipc-transport', gwipc_transport_test, timeout: 10, suite: test_contract_suites)

  gwipc_output_protocol_test = executable(
    'gwipc_output_protocol_test',
    files(
      tests_source_dir / 'integration/gwipc_output_protocol_test.cpp',
      tests_source_dir / '../src/ipc/connection_validation.cpp',
      tests_source_dir / '../src/ipc/connection_output_validation.cpp',
      tests_source_dir / '../src/ipc/connection_correlation.cpp',
      tests_source_dir / '../src/ipc/connection_output_correlation.cpp',
      tests_source_dir / '../src/ipc/connection_queue.cpp',
      tests_source_dir / '../src/ipc/connection_transport.cpp',
      tests_source_dir / '../src/ipc/connection_handshake.cpp',
      tests_source_dir / '../src/ipc/message.cpp',
    ),
    dependencies: [gwipc_dep, gwipc_wire_dep],
    include_directories: test_source_root_include,
  )
  test('gwipc-output-protocol', gwipc_output_protocol_test, timeout: 10, suite: test_contract_suites)

  gwipc_endpoint_test = executable(
    'gwipc_endpoint_test',
    files(tests_source_dir / 'integration/gwipc_endpoint_test.cpp'),
    dependencies: gwipc_dep,
  )
  test('gwipc-endpoint', gwipc_endpoint_test, timeout: 10, suite: test_contract_suites)

  gwipc_fault_test = executable(
    'gwipc_fault_test',
    files(
      tests_source_dir / 'integration/gwipc_fault_test.cpp',
      tests_source_dir / '../src/ipc/message.cpp',
    ),
    dependencies: [gwipc_dep, gwipc_wire_dep],
    include_directories: test_source_root_include,
  )
  test('gwipc-fault-atomicity', gwipc_fault_test, timeout: 10, suite: test_contract_suites)

  gwipc_protocol_edge_test = executable(
    'gwipc_protocol_edge_test',
    files(
      tests_source_dir / 'integration/gwipc_protocol_edge_test.cpp',
      tests_source_dir / '../src/ipc/message.cpp',
    ),
    dependencies: [gwipc_dep, gwipc_wire_dep],
    include_directories: test_source_root_include,
  )
  test('gwipc-protocol-edges', gwipc_protocol_edge_test, timeout: 10, suite: test_contract_suites)

  gwipc_symbols_test = find_program(tests_source_dir / 'tools/gwipc_symbols_test.sh')
  test(
    'gwipc-public-symbols',
    gwipc_symbols_test,
    args: [gwipc_library.full_path()],
    depends: gwipc_library,
    timeout: 10,
    suite: test_contract_suites,
  )

  gwipc_interop_fixture_root = tests_source_dir / 'fixtures/gwipc'
  rust_gwipc_interop_probe = executable(
    'rust_gwipc_interop_probe',
    files(tests_source_dir / 'integration/rust_gwipc_interop_probe.cpp'),
    dependencies: gwipc_wire_dep,
    include_directories: test_source_root_include,
  )
  test(
    'gwipc-legacy-interop-fixtures',
    rust_gwipc_interop_probe,
    args: ['verify', gwipc_interop_fixture_root],
    timeout: 10,
    suite: test_contract_suites,
  )

  sha256sum_program = find_program('sha256sum')
  test(
    'gwipc-interop-fixture-checksums',
    sha256sum_program,
    args: ['--check', gwipc_interop_fixture_root / 'SHA256SUMS'],
    workdir: gwipc_interop_fixture_root,
    timeout: 10,
    suite: test_contract_suites,
  )
endif
