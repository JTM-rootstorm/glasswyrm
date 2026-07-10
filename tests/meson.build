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

gw_vm_cli_test = find_program('tools/gw_vm_cli_test.sh')
test(
  'gw-vm-cli',
  gw_vm_cli_test,
  args: [meson.project_source_root()],
  timeout: 60,
)
