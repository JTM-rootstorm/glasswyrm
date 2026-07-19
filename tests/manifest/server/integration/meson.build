if get_option('glasswyrmd')
  if get_option('libgwipc') and get_option('gwm') and get_option('gwcomp')
    if get_option('headless_backend')
      glasswyrmd_synthetic_cursor_test = executable(
        'glasswyrmd_synthetic_cursor_test',
        files(
          tests_source_dir / 'integration/glasswyrmd_synthetic_cursor_test.cpp',
          tests_source_dir / 'helpers/synthetic_input_client.cpp',
        ),
        dependencies: gwipc_dep,
        include_directories: test_source_root_include,
      )
      test(
        'glasswyrmd-synthetic-cursor',
        glasswyrmd_synthetic_cursor_test,
        args: [glasswyrmd_program.full_path(), gwm_program.full_path(),
               gwcomp_program.full_path()],
        depends: [glasswyrmd_program, gwm_program, gwcomp_program],
        timeout: 30,
      )
      m13_rejecting_compositor_peer = executable(
        'm13_rejecting_compositor_peer',
        files(tests_source_dir / 'integration/m13_rejecting_compositor_peer.cpp'),
        dependencies: [gwcomp_options_dep, gwipc_dep],
        include_directories: test_source_root_include,
      )
      output_configuration_process_test = executable(
        'output_configuration_process_test',
        files(
          tests_source_dir / 'integration/output_configuration_process_test.cpp',
          tests_source_dir / 'helpers/x11_fake_client.cpp',
          tests_source_dir / 'helpers/x11_request_builder.cpp',
        ),
        dependencies: [gwipc_dep, gw_x11_setup_dep],
        include_directories: test_source_root_include,
      )
      test(
        'output-configuration-process',
        output_configuration_process_test,
        args: [glasswyrmd_program.full_path(), gwm_program.full_path(),
               gwcomp_program.full_path(), x11_milestone2_probe.full_path(),
               m13_rejecting_compositor_peer.full_path()],
        depends: [glasswyrmd_program, gwm_program, gwcomp_program,
                  x11_milestone2_probe, m13_rejecting_compositor_peer],
        timeout: 45,
      )
    endif
    glasswyrmd_peer_bootstrap_test = executable(
      'glasswyrmd_peer_bootstrap_test',
      files(tests_source_dir / 'integration/glasswyrmd_peer_bootstrap_test.cpp'),
      dependencies: gw_server_core_dep,
      include_directories: test_source_root_include,
    )
    test(
      'glasswyrmd-peer-bootstrap',
      glasswyrmd_peer_bootstrap_test,
      args: [gwm_program.full_path(), gwcomp_program.full_path()],
      depends: [gwm_program, gwcomp_program],
      timeout: 15,
    )
    runtime_bridge_transaction_test = executable(
      'runtime_bridge_transaction_test',
      files(tests_source_dir / 'integration/runtime_bridge_transaction_test.cpp'),
      dependencies: gw_server_core_dep,
      include_directories: test_source_root_include,
    )
    test(
      'runtime-bridge-transaction',
      runtime_bridge_transaction_test,
      args: [gwm_program.full_path(), gwcomp_program.full_path()],
      depends: [gwm_program, gwcomp_program],
      timeout: 15,
    )
    server_vrr_lifecycle_test = executable(
      'server_vrr_lifecycle_test',
      files(tests_source_dir / 'integration/server_vrr_lifecycle_test.cpp'),
      dependencies: gw_server_core_dep,
      include_directories: test_source_root_include,
    )
    test('server-vrr-lifecycle', server_vrr_lifecycle_test, timeout: 10)
    glasswyrmd_integrated_startup_test = executable(
      'glasswyrmd_integrated_startup_test',
      files(tests_source_dir / 'integration/glasswyrmd_integrated_startup_test.cpp'),
    )
    test(
      'glasswyrmd-integrated-startup',
      glasswyrmd_integrated_startup_test,
      args: [glasswyrmd_program.full_path(), gwm_program.full_path(),
             gwcomp_program.full_path()],
      depends: [glasswyrmd_program, gwm_program, gwcomp_program],
      timeout: 20,
    )
    glasswyrmd_integrated_lifecycle_test = executable(
      'glasswyrmd_integrated_lifecycle_test',
      files(
        tests_source_dir / 'integration/glasswyrmd_integrated_lifecycle_test.cpp',
        tests_source_dir / 'helpers/x11_fake_client.cpp',
        tests_source_dir / 'helpers/x11_request_builder.cpp',
      ),
      dependencies: [gw_x11_setup_dep],
      include_directories: test_source_root_include,
    )
    test(
      'glasswyrmd-integrated-lifecycle',
      glasswyrmd_integrated_lifecycle_test,
      args: [glasswyrmd_program.full_path(), gwm_program.full_path(),
             gwcomp_program.full_path()],
      depends: [glasswyrmd_program, gwm_program, gwcomp_program],
      timeout: 45,
    )
    if get_option('experimental')
      glasswyrmd_m12_game_profile_test = executable(
        'glasswyrmd_m12_game_profile_test',
        files(
          tests_source_dir / 'integration/glasswyrmd_m12_game_profile_test.cpp',
          tests_source_dir / 'helpers/x11_fake_client.cpp',
          tests_source_dir / 'helpers/x11_request_builder.cpp',
        ),
        dependencies: [gw_x11_setup_dep],
        include_directories: test_source_root_include,
      )
      test(
        'glasswyrmd-m12-game-profile',
        glasswyrmd_m12_game_profile_test,
        args: [glasswyrmd_program.full_path(), gwm_program.full_path(),
               gwcomp_program.full_path()],
        depends: [glasswyrmd_program, gwm_program, gwcomp_program],
        timeout: 45,
      )
      glasswyrmd_integrated_property_test = executable(
        'glasswyrmd_integrated_property_test',
        files(
          tests_source_dir / 'integration/glasswyrmd_integrated_property_test.cpp',
          tests_source_dir / 'helpers/x11_fake_client.cpp',
          tests_source_dir / 'helpers/x11_request_builder.cpp',
        ),
        dependencies: [gw_x11_setup_dep],
        include_directories: test_source_root_include,
      )
      test(
        'glasswyrmd-integrated-property',
        glasswyrmd_integrated_property_test,
        args: [glasswyrmd_program.full_path(), gwm_program.full_path(),
               gwcomp_program.full_path()],
        depends: [glasswyrmd_program, gwm_program, gwcomp_program],
        timeout: 45,
      )
    endif
    glasswyrmd_input_recovery_test = executable(
      'glasswyrmd_input_recovery_test',
      files(
        tests_source_dir / 'integration/glasswyrmd_input_recovery_test.cpp',
        tests_source_dir / 'helpers/synthetic_input_client.cpp',
      ),
      dependencies: [gwipc_dep, gw_x11_setup_dep],
      include_directories: test_source_root_include,
    )
    test(
      'glasswyrmd-input-recovery',
      glasswyrmd_input_recovery_test,
      args: [glasswyrmd_program.full_path(), gwm_program.full_path(),
             gwcomp_program.full_path()],
      depends: [glasswyrmd_program, gwm_program, gwcomp_program],
      timeout: 30,
    )
  endif
endif
