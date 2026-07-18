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
