unit_tests = {
  'core_build_config_test': 'unit/core_build_config_test.cpp',
  'protocol_x11_test': 'protocol/x11_scaffold_test.cpp',
  'headless_scene_test': 'integration/headless_scene_test.cpp',
  'software_framebuffer_test': 'pixel/software_framebuffer_test.cpp',
}

foreach test_name, test_source : unit_tests
  test_exe = executable(
    test_name,
    files(test_source),
    dependencies: gw_dep,
  )

  test(test_name, test_exe)
endforeach
