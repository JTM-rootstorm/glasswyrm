scaffold_test = executable(
  'scaffold_test',
  files('unit/scaffold_test.cpp'),
  dependencies: gw_scaffold_dep,
)

test('scaffold', scaffold_test)

test_source_root_include = include_directories('..')

if get_option('gwcomp')
  gwcomp_options_test = executable(
    'gwcomp_options_test',
    files('unit/gwcomp_options_test.cpp'),
    dependencies: gwcomp_options_dep,
    include_directories: test_source_root_include,
  )
  test('gwcomp-options', gwcomp_options_test, timeout: 10)

  gwcomp_process_test = executable(
    'gwcomp_process_test',
    files('integration/gwcomp_process_test.cpp'),
    dependencies: gwipc_dep,
    include_directories: test_source_root_include,
  )
  test(
    'gwcomp-process',
    gwcomp_process_test,
    args: [gwcomp_program.full_path()],
    depends: gwcomp_program,
    timeout: 15,
  )

  gwcomp_golden_test = executable(
    'gwcomp_golden_test',
    files('integration/gwcomp_golden_test.cpp'),
  )
  test(
    'gwcomp-golden',
    gwcomp_golden_test,
    args: [gwcomp_program.full_path(), gwcomp_m4_producer_program.full_path()],
    depends: [gwcomp_program, gwcomp_m4_producer_program],
    timeout: 15,
  )

  test(
    'gwcomp-m4-producer-help',
    gwcomp_m4_producer_program,
    args: ['--help'],
    depends: gwcomp_m4_producer_program,
    timeout: 10,
  )
endif

if get_option('gwcomp')
compositor_geometry_test = executable(
  'compositor_geometry_test',
  files('unit/compositor_geometry_test.cpp'),
  dependencies: gw_compositor_geometry_dep,
  include_directories: test_source_root_include,
)
test('compositor-geometry', compositor_geometry_test, timeout: 10)

compositor_scene_test = executable(
  'compositor_scene_test',
  files('unit/compositor_scene_test.cpp'),
  dependencies: gw_compositor_geometry_dep,
  include_directories: test_source_root_include,
)
test('compositor-scene', compositor_scene_test, timeout: 10)

compositor_buffer_test = executable(
  'compositor_buffer_test',
  files('unit/compositor_buffer_test.cpp'),
  dependencies: gw_compositor_geometry_dep,
  include_directories: test_source_root_include,
)
test('compositor-buffer', compositor_buffer_test, timeout: 10)

software_renderer_test = executable(
  'software_renderer_test',
  files('pixel/software_renderer_test.cpp'),
  dependencies: gw_render_software_dep,
  include_directories: test_source_root_include,
)
test('software-renderer', software_renderer_test, timeout: 10)

if get_option('headless_backend')
  headless_output_test = executable(
    'headless_output_test',
    files('unit/headless_output_test.cpp'),
    dependencies: gw_headless_dep,
    include_directories: test_source_root_include,
  )
  test('headless-output', headless_output_test, timeout: 10)
endif
endif

if get_option('libgwipc')
  gwipc_wire_codec_test = executable(
    'gwipc_wire_codec_test',
    files('unit/ipc/wire_codec_test.cpp'),
    dependencies: gwipc_wire_dep,
    include_directories: test_source_root_include,
  )
  test('gwipc-wire-codec', gwipc_wire_codec_test, timeout: 10)
  test('gwipc-envelope', gwipc_wire_codec_test, timeout: 10)
  test('gwipc-control-codec', gwipc_wire_codec_test, timeout: 10)
  test('gwipc-snapshot-codec', gwipc_wire_codec_test, timeout: 10)

  gwipc_contract_codec_test = executable(
    'gwipc_contract_codec_test',
    files('unit/ipc/contract_codec_test.cpp'),
    dependencies: gwipc_wire_dep,
    include_directories: test_source_root_include,
  )
  test('gwipc-compositor-contract', gwipc_contract_codec_test, timeout: 10)

  gwipc_public_contract_api_test = executable(
    'gwipc_public_contract_api_test',
    files('unit/ipc/public_contract_api_test.cpp'),
    dependencies: gwipc_dep,
  )
  test('gwipc-public-contract-api', gwipc_public_contract_api_test, timeout: 10)

  gwipc_decoder_fuzz_test = executable(
    'gwipc_decoder_fuzz_test',
    files('unit/ipc/decoder_fuzz_test.cpp'),
    dependencies: gwipc_wire_dep,
    include_directories: test_source_root_include,
  )
  test('gwipc-malformed', gwipc_decoder_fuzz_test, timeout: 15)

  gwipc_wire_probe = executable(
    'gwipc_wire_probe',
    files('integration/gwipc_wire_probe.c'),
    dependencies: gwipc_dep,
  )

  gwipc_transport_test = executable(
    'gwipc_transport_test',
    files('integration/gwipc_transport_test.cpp'),
    dependencies: [gwipc_dep, gwipc_wire_dep],
    include_directories: test_source_root_include,
  )
  test('gwipc-transport', gwipc_transport_test, timeout: 10)

  gwipc_endpoint_test = executable(
    'gwipc_endpoint_test',
    files('integration/gwipc_endpoint_test.cpp'),
    dependencies: gwipc_dep,
  )
  test('gwipc-endpoint', gwipc_endpoint_test, timeout: 10)

  gwipc_fault_test = executable(
    'gwipc_fault_test',
    files(
      'integration/gwipc_fault_test.cpp',
      '../src/ipc/message.cpp',
    ),
    dependencies: [gwipc_dep, gwipc_wire_dep],
    include_directories: test_source_root_include,
  )
  test('gwipc-fault-atomicity', gwipc_fault_test, timeout: 10)

  gwipc_protocol_edge_test = executable(
    'gwipc_protocol_edge_test',
    files(
      'integration/gwipc_protocol_edge_test.cpp',
      '../src/ipc/message.cpp',
    ),
    dependencies: [gwipc_dep, gwipc_wire_dep],
    include_directories: test_source_root_include,
  )
  test('gwipc-protocol-edges', gwipc_protocol_edge_test, timeout: 10)

  gwipc_probe_server = executable(
    'gwipc_probe_server',
    files('integration/gwipc_probe_server.cpp'),
    dependencies: [gwipc_dep, gwipc_wire_dep],
    include_directories: test_source_root_include,
  )
  gwipc_probe_client = executable(
    'gwipc_probe_client',
    files('integration/gwipc_probe_client.cpp'),
    dependencies: [gwipc_dep, gwipc_wire_dep],
    include_directories: test_source_root_include,
  )

  gwipc_symbols_test = find_program('tools/gwipc_symbols_test.sh')
  test(
    'gwipc-public-symbols',
    gwipc_symbols_test,
    args: [gwipc_library.full_path()],
    depends: gwipc_library,
    timeout: 10,
  )
endif

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
    ['request-work-budget', 'request_work_budget_test.cpp'],
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
      depends: glasswyrmd_program,
      timeout: 15,
    )
  endforeach

  x11_setup_probe = executable(
    'x11_setup_probe',
    files('integration/x11_setup_probe.cpp'),
    dependencies: x11_fake_client_dep,
    include_directories: test_source_root_include,
  )

  x11_milestone2_probe = executable(
    'x11_milestone2_probe',
    files('integration/x11_milestone2_probe.cpp'),
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
    xcb_milestone2_probe = executable(
      'xcb_milestone2_probe',
      files('xcb/xcb_milestone2_probe.c'),
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
