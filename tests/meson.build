scaffold_test = executable(
  'scaffold_test',
  files('unit/scaffold_test.cpp'),
  dependencies: gw_scaffold_dep,
)

test('scaffold', scaffold_test)

gw_vm_cli_test = find_program('tools/gw_vm_cli_test.sh')
test(
  'gw-vm-cli',
  gw_vm_cli_test,
  args: [meson.project_source_root()],
  timeout: 60,
)
