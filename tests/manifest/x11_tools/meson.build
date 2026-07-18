
x11_setup_parser_test = executable(
  'x11_setup_parser_test',
  files(tests_source_dir / 'protocol/x11_setup_parser_test.cpp'),
  dependencies: gw_x11_setup_dep,
  include_directories: test_source_root_include,
)
test('x11-setup-parser', x11_setup_parser_test, timeout: 10)

x11_setup_reply_test = executable(
  'x11_setup_reply_test',
  files(tests_source_dir / 'protocol/x11_setup_reply_test.cpp'),
  dependencies: gw_x11_setup_dep,
  include_directories: test_source_root_include,
)
test('x11-setup-reply', x11_setup_reply_test, timeout: 10)

x11_request_framer_test = executable(
  'x11_request_framer_test',
  files(tests_source_dir / 'protocol/x11_request_framer_test.cpp'),
  dependencies: gw_x11_setup_dep,
  include_directories: test_source_root_include,
)
test('x11-request-framer', x11_request_framer_test, timeout: 10)

x11_reply_atom_test = executable(
  'x11_reply_atom_test',
  files(tests_source_dir / 'protocol/x11_reply_atom_test.cpp'),
  dependencies: gw_x11_setup_dep,
  include_directories: test_source_root_include,
)
test('x11-reply-atom', x11_reply_atom_test, timeout: 10)

if get_option('glasswyrmd')
  foreach unit_case : [
    ['resource-id', 'resource_id_test.cpp'],
    ['resource-table', 'resource_table_test.cpp'],
    ['server-lifecycle-state', 'server_lifecycle_state_test.cpp'],
    ['local-lifecycle-dispatch', 'local_lifecycle_dispatch_test.cpp'],
    ['lifecycle-coordinator', 'lifecycle_coordinator_test.cpp'],
    ['atom-table', 'atom_table_test.cpp'],
    ['property-store', 'property_store_test.cpp'],
    ['request-work-budget', 'request_work_budget_test.cpp'],
    ['compatibility-query-dispatch', 'compatibility_query_dispatch_test.cpp'],
    ['set-input-focus-dispatch', 'set_input_focus_dispatch_test.cpp'],
    ['m12-protocol-foundation', 'm12_protocol_foundation_test.cpp'],
    ['mit-shm', 'mit_shm_test.cpp'],
    ['xfixes-damage', 'xfixes_damage_test.cpp'],
    ['damage-add', 'damage_add_test.cpp'],
    ['xfixes-damage-limits', 'xfixes_damage_limits_test.cpp'],
    ['render-ops', 'render_ops_test.cpp'],
    ['picture-model', 'picture_model_test.cpp'],
    ['composite-state', 'composite_state_test.cpp'],
    ['render-dispatch', 'render_dispatch_test.cpp'],
    ['composite-dispatch', 'composite_dispatch_test.cpp'],
    ['randr-reporting', 'randr_reporting_test.cpp'],
    ['randr-config', 'randr_config_test.cpp'],
    ['randr-event-wire', 'randr_event_wire_test.cpp'],
    ['gw-scale-dispatch', 'gw_scale_dispatch_test.cpp'],
    ['ewmh-root', 'ewmh_root_test.cpp'],
    ['ewmh-policy', 'ewmh_policy_test.cpp'],
    ['ewmh-client-message', 'ewmh_client_message_test.cpp'],
    ['ewmh-state-action', 'ewmh_state_action_test.cpp'],
    ['ewmh-lifecycle', 'ewmh_lifecycle_test.cpp'],
  ]
    unit_program = executable(
      unit_case[0],
      files(tests_source_dir / 'unit/' + unit_case[1]),
      dependencies: gw_server_state_dep,
      include_directories: test_source_root_include,
    )
    test(unit_case[0], unit_program, timeout: 10)
  endforeach

  window_tree_test = executable(
    'window_tree_test',
    files(tests_source_dir / 'unit/window_tree_test.cpp'),
    dependencies: gw_server_state_dep,
    include_directories: test_source_root_include,
  )
  test('window-tree', window_tree_test, args: ['basic'], timeout: 10)
  test('window-tree-deep', window_tree_test, args: ['deep'], timeout: 30)
  test('window-tree-wide', window_tree_test, args: ['wide'], timeout: 30)
  test('window-tree-mixed', window_tree_test, args: ['mixed'], timeout: 10)

  foreach integration_case : [
    ['glasswyrmd-handshake', 'glasswyrmd_handshake_test.cpp'],
    ['glasswyrmd-malformed-client', 'glasswyrmd_malformed_client_test.cpp'],
    ['glasswyrmd-lifecycle', 'glasswyrmd_lifecycle_test.cpp'],
    ['glasswyrmd-core-requests', 'glasswyrmd_core_requests_test.cpp'],
    ['glasswyrmd-selection', 'glasswyrmd_selection_test.cpp'],
    ['glasswyrmd-m11-isolation', 'glasswyrmd_m11_isolation_test.cpp'],
  ]
    integration_program = executable(
      integration_case[0],
      files(tests_source_dir / 'integration/' + integration_case[1]),
      dependencies: x11_fake_client_dep,
      include_directories: test_source_root_include,
    )
    test(
      integration_case[0],
      integration_program,
      args: [glasswyrmd_program.full_path()],
      depends: glasswyrmd_program,
      timeout: 15,
    )
  endforeach


endif

m11_xterm_acceptance = find_program(tests_source_dir / 'apps/m11_xterm_acceptance')
m11_xterm_acceptance_test = find_program(tests_source_dir / 'apps/m11_xterm_acceptance_test.sh')
test(
  'm11-xterm-acceptance',
  m11_xterm_acceptance_test,
  args: [m11_xterm_acceptance],
  timeout: 10,
)

m12_workload_environment_test = find_program(
  tests_source_dir / 'compat/m12/run_workloads_environment_test.py',
)
test(
  'm12-workload-environment',
  m12_workload_environment_test,
  timeout: 10,
)

m12_resident_workload_test = find_program(
  tests_source_dir / 'compat/m12/run_workloads_resident_test.py',
)
test(
  'm12-resident-workload',
  m12_resident_workload_test,
  timeout: 15,
)

m12_trace_validator_test = find_program(tests_source_dir / 'compat/m12/validate_trace_test.py')
test(
  'm12-trace-validator',
  m12_trace_validator_test,
  timeout: 10,
)

m12_fixture_trace_test = find_program(
  tests_source_dir / 'compat/m12/normalize_fixture_traces_test.py',
)
test(
  'm12-fixture-trace-normalizer',
  m12_fixture_trace_test,
  timeout: 10,
)

m12_fixture_promotion_test = find_program(
  tests_source_dir / 'compat/m12/promote_fixtures_test.py',
)
test(
  'm12-fixture-promotion',
  m12_fixture_promotion_test,
  timeout: 10,
)

m12_frame_evidence_test = find_program(tests_source_dir / 'compat/m12/frame_evidence_test.py')
test(
  'm12-frame-evidence',
  m12_frame_evidence_test,
  timeout: 10,
)

gw_vm_cli_test = find_program(tests_source_dir / 'tools/gw_vm_cli_test.sh')
test(
  'gw-vm-cli',
  gw_vm_cli_test,
  args: [meson.project_source_root()],
  timeout: 60,
)

source_layout_test = find_program(tests_source_dir / 'tools/source_layout_test.sh')
test(
  'source-layout',
  source_layout_test,
  timeout: 30,
)
