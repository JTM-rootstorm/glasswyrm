scaffold_test = executable(
  'scaffold_test',
  files('unit/scaffold_test.cpp'),
  dependencies: gw_scaffold_dep,
)

test('scaffold', scaffold_test)

test_source_root_include = include_directories('..')

x11_setup_parser_test = executable(
  'x11_setup_parser_test',
  files('protocol/x11_setup_parser_test.cpp'),
  dependencies: gw_x11_setup_dep,
  include_directories: test_source_root_include,
)
test('x11-setup-parser', x11_setup_parser_test, timeout: 10)

x11_setup_reply_test = executable(
  'x11_setup_reply_test',
  files('protocol/x11_setup_reply_test.cpp'),
  dependencies: gw_x11_setup_dep,
  include_directories: test_source_root_include,
)
test('x11-setup-reply', x11_setup_reply_test, timeout: 10)

x11_request_framer_test = executable(
  'x11_request_framer_test',
  files('protocol/x11_request_framer_test.cpp'),
  dependencies: gw_x11_setup_dep,
  include_directories: test_source_root_include,
)
test('x11-request-framer', x11_request_framer_test, timeout: 10)

x11_reply_atom_test = executable(
  'x11_reply_atom_test',
  files('protocol/x11_reply_atom_test.cpp'),
  dependencies: gw_x11_setup_dep,
  include_directories: test_source_root_include,
)
test('x11-reply-atom', x11_reply_atom_test, timeout: 10)

if get_option('glasswyrmd')
  foreach unit_case : [
    ['resource-id', 'resource_id_test.cpp'],
    ['resource-table', 'resource_table_test.cpp'],
    ['window-tree', 'window_tree_test.cpp'],
    ['atom-table', 'atom_table_test.cpp'],
    ['property-store', 'property_store_test.cpp'],
  ]
    unit_program = executable(
      unit_case[0],
      files('unit/' + unit_case[1]),
      dependencies: gw_server_state_dep,
      include_directories: test_source_root_include,
    )
    test(unit_case[0], unit_program, timeout: 10)
  endforeach

  x11_fake_client_lib = static_library(
    'x11-fake-client',
    files(
      'helpers/x11_fake_client.cpp',
      'helpers/x11_request_builder.cpp',
      'integration/server_fixture.cpp',
    ),
    dependencies: gw_x11_setup_dep,
    include_directories: test_source_root_include,
  )
  x11_fake_client_dep = declare_dependency(
    link_with: x11_fake_client_lib,
    dependencies: gw_x11_setup_dep,
    include_directories: test_source_root_include,
  )

  foreach integration_case : [
    ['glasswyrmd-handshake', 'glasswyrmd_handshake_test.cpp'],
    ['glasswyrmd-malformed-client', 'glasswyrmd_malformed_client_test.cpp'],
    ['glasswyrmd-lifecycle', 'glasswyrmd_lifecycle_test.cpp'],
    ['glasswyrmd-core-requests', 'glasswyrmd_core_requests_test.cpp'],
  ]
    integration_program = executable(
      integration_case[0],
      files('integration/' + integration_case[1]),
      dependencies: x11_fake_client_dep,
      include_directories: test_source_root_include,
    )
    test(
      integration_case[0],
      integration_program,
      args: [glasswyrmd_program.full_path()],
      timeout: 15,
    )
  endforeach

  x11_setup_probe = executable(
    'x11_setup_probe',
    files('integration/x11_setup_probe.cpp'),
    dependencies: x11_fake_client_dep,
    include_directories: test_source_root_include,
  )

  xcb_test_dependency = dependency('xcb', required: false)
  if xcb_test_dependency.found()
    xcb_setup_probe = executable(
      'xcb_setup_probe',
      files('xcb/xcb_setup_probe.c'),
      dependencies: xcb_test_dependency,
    )
  endif
endif

gw_vm_cli_test = find_program('tools/gw_vm_cli_test.sh')
test(
  'gw-vm-cli',
  gw_vm_cli_test,
  args: [meson.project_source_root()],
  timeout: 60,
)
