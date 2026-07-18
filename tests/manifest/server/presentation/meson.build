if get_option('glasswyrmd') and get_option('libgwipc')
  test('gwinput-m8-help', gwinput_m8_program, args: ['--help'], timeout: 10)

  m9_xeyes_path = get_option('m9_xeyes')
  m9_xclock_path = get_option('m9_xclock')
  if (m9_xeyes_path != '' and m9_xclock_path != '' and
      get_option('gwm') and get_option('gwcomp') and
      get_option('render_software'))
    m9_live_profile = find_program(tests_source_dir / 'apps/m9_live_profile.sh')
    foreach profile : ['xeyes', 'xclock-analog', 'xclock-digital', 'combined']
      test(
        'm9-live-' + profile,
        m9_live_profile,
        args: [profile, glasswyrmd_program, gwm_program, gwcomp_program,
               gwinput_m8_program, m9_fixed_time_preload,
               meson.project_source_root() / 'tests/compat/m9/m9_trace_summarize',
               meson.project_source_root() / 'tests/fixtures/m9',
               m9_xeyes_path, m9_xclock_path],
        depends: [glasswyrmd_program, gwm_program, gwcomp_program,
                  gwinput_m8_program, m9_fixed_time],
        is_parallel: false,
        timeout: 30,
      )
    endforeach
  endif

  published_buffer_test = executable(
    'published_buffer_test',
    files(tests_source_dir / 'unit/published_buffer_test.cpp'),
    dependencies: gw_server_core_dep,
    include_directories: test_source_root_include,
  )
  test('published-buffer', published_buffer_test, timeout: 10)
  test('m12-extension-stress', m12_extension_stress_probe, timeout: 20)
  content_presenter_test = executable(
    'content_presenter_test',
    files(tests_source_dir / 'unit/content_presenter_test.cpp'),
    dependencies: gw_server_core_dep,
    include_directories: test_source_root_include,
  )
  test('content-presenter', content_presenter_test, timeout: 10)
  cursor_presenter_test = executable(
    'cursor_presenter_test',
    files(tests_source_dir / 'unit/cursor_presenter_test.cpp'),
    dependencies: gw_server_core_dep,
    include_directories: test_source_root_include,
  )
  test('cursor-presenter', cursor_presenter_test, timeout: 10)
  lifecycle_projection_test = executable(
    'lifecycle_projection_test',
    files(tests_source_dir / 'unit/lifecycle_projection_test.cpp'),
    dependencies: gw_server_core_dep,
    include_directories: test_source_root_include,
  )
  test('lifecycle-projection', lifecycle_projection_test, timeout: 10)
  compositor_output_inventory_test = executable(
    'compositor_output_inventory_test',
    files(tests_source_dir / 'unit/compositor_output_inventory_test.cpp'),
    dependencies: gw_server_core_dep,
    include_directories: test_source_root_include,
  )
  test('compositor-output-inventory', compositor_output_inventory_test,
       timeout: 10)
endif
