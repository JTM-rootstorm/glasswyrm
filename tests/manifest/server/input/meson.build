if get_option('glasswyrmd')
  if get_option('libinput_backend')
    xkb_keymap_test = executable(
      'xkb_keymap_test',
      files(tests_source_dir / 'unit/xkb_keymap_test.cpp'),
      dependencies: gw_xkb_keymap_dep,
      include_directories: test_source_root_include,
    )
    test('xkb-keymap', xkb_keymap_test, timeout: 10)
    repeat_timer_test = executable(
      'repeat_timer_test',
      files(tests_source_dir / 'unit/repeat_timer_test.cpp'),
      dependencies: gw_xkb_keymap_dep,
      include_directories: test_source_root_include,
    )
    test('repeat-timer', repeat_timer_test, timeout: 10)
    libinput_path_allowlist_test = executable(
      'libinput_path_allowlist_test',
      files(tests_source_dir / 'unit/libinput_path_allowlist_test.cpp'),
      dependencies: gw_xkb_keymap_dep,
      include_directories: test_source_root_include,
    )
    test('libinput-path-allowlist', libinput_path_allowlist_test, timeout: 10)
    libinput_backend_test = executable(
      'libinput_backend_test',
      files(tests_source_dir / 'unit/libinput_backend_test.cpp'),
      dependencies: gw_xkb_keymap_dep,
      include_directories: test_source_root_include,
    )
    test('libinput-backend', libinput_backend_test, timeout: 10)
    real_input_controller_test = executable(
      'real_input_controller_test',
      files(tests_source_dir / 'unit/real_input_controller_test.cpp'),
      dependencies: gw_server_core_dep,
      include_directories: test_source_root_include,
    )
    test('real-input-controller', real_input_controller_test, timeout: 10)
    real_absolute_pointer_routing_test = executable(
      'real_absolute_pointer_routing_test',
      files(tests_source_dir / 'integration/real_absolute_pointer_routing_test.cpp'),
      dependencies: gw_server_core_dep,
      include_directories: test_source_root_include,
    )
    test('real-absolute-pointer-routing',
         real_absolute_pointer_routing_test, timeout: 10)
  endif

endif
