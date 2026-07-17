gw_scaffold_lib = static_library(
  'gw-scaffold',
  files('scaffold/component.cpp'),
  include_directories: [gw_include_dir, gw_generated_include_dir],
)

gw_x11_setup_lib = static_library(
  'gw-x11-setup',
  files(
    'protocol/x11/setup.cpp',
    'protocol/x11/request.cpp',
    'protocol/x11/reply.cpp',
    'protocol/x11/event.cpp',
    'protocol/x11/exposure_event.cpp',
    'protocol/x11/lifecycle_request.cpp',
    'protocol/x11/input_event.cpp',
    'protocol/x11/crossing_event.cpp',
    'protocol/x11/focus_event.cpp',
  ),
  include_directories: include_directories('.'),
)

gw_x11_setup_dep = declare_dependency(
  link_with: gw_x11_setup_lib,
  include_directories: include_directories('.'),
)

gw_scaffold_dep = declare_dependency(
  link_with: gw_scaffold_lib,
  include_directories: [gw_include_dir, gw_generated_include_dir],
)

if get_option('gwcomp')
gw_compositor_geometry_lib = static_library(
  'gwcomp-geometry',
  files(
    'compositor/rectangle.cpp',
    'compositor/damage_region.cpp',
    'compositor/scene.cpp',
    'compositor/buffer.cpp',
  ),
  include_directories: [include_directories('.'), gw_include_dir],
)
gw_compositor_geometry_dep = declare_dependency(
  link_with: gw_compositor_geometry_lib,
  include_directories: [include_directories('.'), gw_include_dir],
)

gw_render_software_lib = static_library(
  'gwcomp-render-software',
  files(
    'render/software/blend.cpp',
    'render/software/renderer.cpp',
  ),
  dependencies: gw_compositor_geometry_dep,
  include_directories: include_directories('.'),
)
gw_render_software_dep = declare_dependency(
  link_with: gw_render_software_lib,
  dependencies: gw_compositor_geometry_dep,
  include_directories: include_directories('.'),
)

  gw_output_lib = static_library(
    'gw-output',
    files(
      'backends/output/software_frame.cpp',
      'backends/headless/frame_dump.cpp',
    ),
    dependencies: gw_compositor_geometry_dep,
    include_directories: include_directories('.'),
  )
  gw_output_dep = declare_dependency(
    link_with: gw_output_lib,
    dependencies: gw_compositor_geometry_dep,
    include_directories: include_directories('.'),
  )

if get_option('headless_backend')
  gw_headless_lib = static_library(
    'gw-headless',
    files(
      'backends/headless/output.cpp',
      'backends/headless/presenter.cpp',
    ),
    dependencies: gw_output_dep,
    include_directories: include_directories('.'),
  )
  gw_headless_dep = declare_dependency(
    link_with: gw_headless_lib,
    dependencies: gw_output_dep,
    include_directories: include_directories('.'),
  )
endif

if get_option('drm_backend')
  gw_drm_model_lib = static_library(
    'gw-drm-model',
    files(
      'backends/drm/connector_name.cpp',
      'backends/drm/connector_selector.cpp',
      'backends/drm/dumb_buffer.cpp',
      'backends/drm/drm_report.cpp',
      'backends/drm/mode_selector.cpp',
      'backends/drm/pipeline_selector.cpp',
      'backends/drm/property_cache.cpp',
    ),
    include_directories: include_directories('.'),
  )
  gw_drm_model_dep = declare_dependency(
    link_with: gw_drm_model_lib,
    include_directories: include_directories('.'),
  )

  gw_drm_api_lib = static_library(
    'gw-drm-api',
    files(
      'backends/drm/device.cpp',
      'backends/drm/fake_drm_api.cpp',
      'backends/drm/fake_kms_api.cpp',
      'backends/drm/kms_dumb_buffer_api.cpp',
      'backends/drm/kms_state.cpp',
      'backends/drm/real_drm_api.cpp',
      'backends/drm/real_kms_api.cpp',
    ),
    dependencies: [gw_drm_model_dep, libdrm_dep],
    include_directories: include_directories('.'),
  )
  gw_drm_api_dep = declare_dependency(
    link_with: gw_drm_api_lib,
    dependencies: [gw_drm_model_dep, libdrm_dep],
    include_directories: include_directories('.'),
  )

  gw_session_lib = static_library(
    'gw-session',
    files(
      'backends/session/external_session.cpp',
      'backends/session/vt_api.cpp',
      'backends/session/vt_session.cpp',
    ),
    include_directories: include_directories('.'),
  )
  gw_session_dep = declare_dependency(
    link_with: gw_session_lib,
    include_directories: include_directories('.'),
  )

  gw_drm_presenter_lib = static_library(
    'gw-drm-presenter',
    files(
      'backends/drm/presenter.cpp',
      'backends/drm/presenter_pipeline.cpp',
    ),
    dependencies: [gw_drm_api_dep, gw_session_dep, gw_output_dep],
    include_directories: include_directories('.'),
  )
  gw_drm_presenter_dep = declare_dependency(
    link_with: gw_drm_presenter_lib,
    dependencies: [gw_drm_api_dep, gw_session_dep, gw_output_dep],
    include_directories: include_directories('.'),
  )
endif
endif

if get_option('libgwipc')
  gwipc_cpp_args = ['-DGWIPC_BUILDING_LIBRARY', '-fvisibility-inlines-hidden']
  if get_option('ipc_trace')
    gwipc_cpp_args += '-DGWIPC_TRACE'
  endif
  gwipc_wire_sources = files(
    'ipc/wire/envelope.cpp',
    'ipc/wire/control.cpp',
    'ipc/wire/compositor_contract.cpp',
    'ipc/wire/policy_contract.cpp',
    'ipc/wire/lifecycle_contract.cpp',
    'ipc/wire/input_contract.cpp',
    'ipc/wire/session_contract.cpp',
  )
  gwipc_private_include = include_directories('.')
  gwipc_wire_lib = static_library(
    'gwipc-wire',
    gwipc_wire_sources,
    include_directories: gwipc_private_include,
    gnu_symbol_visibility: 'hidden',
    cpp_args: ['-fvisibility-inlines-hidden'],
    pic: true,
    install: false,
  )
  gwipc_wire_dep = declare_dependency(
    link_with: gwipc_wire_lib,
    include_directories: gwipc_private_include,
  )

  gwipc_library = shared_library(
    'gwipc',
    files(
      'ipc/public_api.cpp',
      'ipc/contract_api.cpp',
      'ipc/control_api.cpp',
      'ipc/message.cpp',
      'ipc/endpoint.cpp',
      'ipc/connection.cpp',
      'ipc/connection_correlation.cpp',
      'ipc/connection_handshake.cpp',
      'ipc/connection_poll.cpp',
      'ipc/connection_queue.cpp',
      'ipc/connection_transport.cpp',
      'ipc/connection_validation.cpp',
    ),
    link_whole: gwipc_wire_lib,
    include_directories: [
      gw_include_dir,
      gwipc_private_include,
      gw_generated_include_dir,
    ],
    cpp_args: gwipc_cpp_args,
    link_args: [
      '-Wl,--version-script=' + meson.current_source_dir() / 'ipc/gwipc.map',
    ],
    gnu_symbol_visibility: 'hidden',
    version: '0.6.0',
    soversion: '0',
    install: true,
  )
  gwipc_dep = declare_dependency(
    link_with: gwipc_library,
    include_directories: gw_include_dir,
  )

  install_headers(
    '../include/glasswyrm/ipc.h',
    '../include/glasswyrm/ipc.hpp',
    subdir: 'glasswyrm',
  )
  install_headers(
    '../include/glasswyrm/ipc/version.h',
    '../include/glasswyrm/ipc/types.h',
    '../include/glasswyrm/ipc/listener.h',
    '../include/glasswyrm/ipc/connection.h',
    '../include/glasswyrm/ipc/message.h',
    '../include/glasswyrm/ipc/contracts.h',
    '../include/glasswyrm/ipc/control.h',
    '../include/glasswyrm/ipc/policy.h',
    '../include/glasswyrm/ipc/lifecycle.h',
    '../include/glasswyrm/ipc/input.h',
    '../include/glasswyrm/ipc/session.h',
    subdir: 'glasswyrm/ipc',
  )

  pkgconfig = import('pkgconfig')
  pkgconfig.generate(
    gwipc_library,
    filebase: 'gwipc',
    name: 'gwipc',
    description: 'Glasswyrm versioned local IPC contracts',
    version: '0.6.0',
  )
endif

if get_option('glasswyrmd')
  if get_option('libinput_backend')
    gw_xkb_keymap_lib = static_library(
      'gw-xkb-keymap',
      files(
        'input/device_path_allowlist.cpp',
        'input/fake_libinput_api.cpp',
        'input/libinput_backend.cpp',
        'input/real_libinput_api.cpp',
        'input/repeat_timer.cpp',
        'input/xkb_keymap.cpp',
      ),
      dependencies: [libinput_dep, xkbcommon_dep],
      include_directories: include_directories('.'),
    )
    gw_xkb_keymap_dep = declare_dependency(
      link_with: gw_xkb_keymap_lib,
      dependencies: [libinput_dep, xkbcommon_dep],
      include_directories: include_directories('.'),
    )
  endif

  gw_server_state_lib = static_library(
    'gw-server-state',
    files(
      'glasswyrmd/property.cpp',
      'glasswyrmd/resource_table.cpp',
      'glasswyrmd/window_store.cpp',
      'glasswyrmd/drawable_store.cpp',
      'glasswyrmd/font_store.cpp',
      'glasswyrmd/cursor_store.cpp',
      'glasswyrmd/property_store.cpp',
      'glasswyrmd/selection_store.cpp',
      'glasswyrmd/grab_state.cpp',
      'glasswyrmd/resource_cleanup.cpp',
      'glasswyrmd/resource_invariants.cpp',
      'glasswyrmd/atom_table.cpp',
      'glasswyrmd/server_state.cpp',
      'glasswyrmd/lifecycle_snapshot.cpp',
      'glasswyrmd/lifecycle_coordinator.cpp',
      'glasswyrmd/request_dispatcher.cpp',
      'glasswyrmd/request_handlers/common.cpp',
      'glasswyrmd/request_handlers/drawable_access.cpp',
      'glasswyrmd/request_handlers/lifecycle.cpp',
      'glasswyrmd/request_handlers/window.cpp',
      'glasswyrmd/request_handlers/drawable_resource.cpp',
      'glasswyrmd/request_handlers/drawing.cpp',
      'glasswyrmd/request_handlers/font.cpp',
      'glasswyrmd/request_handlers/text.cpp',
      'glasswyrmd/request_handlers/query.cpp',
      'glasswyrmd/request_handlers/atom.cpp',
      'glasswyrmd/request_handlers/property.cpp',
      'glasswyrmd/request_handlers/selection.cpp',
      'glasswyrmd/request_handlers/grab.cpp',
      'glasswyrmd/request_handlers/keyboard_control.cpp',
      'glasswyrmd/request_handlers/color.cpp',
      'glasswyrmd/request_handlers/cursor.cpp',
      'core/geometry/rectangle.cpp',
      'core/geometry/region.cpp',
      'glasswyrmd/pixel_storage.cpp',
      'glasswyrmd/bitmap_storage.cpp',
      'glasswyrmd/raster_ops.cpp',
      'glasswyrmd/font.cpp',
      'glasswyrmd/m9_raster_ops.cpp',
      'glasswyrmd/subtree_compositor.cpp',
      'input/input_state.cpp',
      'input/input_router.cpp',
      'input/cursor_model.cpp',
    ),
    dependencies: gw_x11_setup_dep,
    include_directories: include_directories('.'),
  )
  gw_server_state_dep = declare_dependency(
    link_with: gw_server_state_lib,
    dependencies: gw_x11_setup_dep,
    include_directories: include_directories('.'),
  )
  gw_server_core_sources = files(
    'glasswyrmd/options.cpp',
    'glasswyrmd/compatibility_trace.cpp',
    'glasswyrmd/client_connection.cpp',
    'glasswyrmd/event_router.cpp',
    'glasswyrmd/protocol_event_router.cpp',
    'glasswyrmd/server.cpp',
    'glasswyrmd/server_signals.cpp',
    'glasswyrmd/x11_listener.cpp',
    'glasswyrmd/client_registry.cpp',
    'glasswyrmd/server_reactor.cpp',
    'glasswyrmd/server_shutdown.cpp',
    'glasswyrmd/integrated_runtime.cpp',
  )
  gw_server_core_dependencies = [gw_server_state_dep]
  if get_option('libgwipc')
    gw_server_core_sources += files(
      'glasswyrmd/peer_transport.cpp',
      'glasswyrmd/policy_peer.cpp',
      'glasswyrmd/compositor_peer.cpp',
      'glasswyrmd/lifecycle_projection.cpp',
      'glasswyrmd/runtime_bridge.cpp',
      'glasswyrmd/published_buffer.cpp',
      'glasswyrmd/content_presenter.cpp',
      'glasswyrmd/cursor_presenter.cpp',
      'glasswyrmd/synthetic_input_peer.cpp',
      'glasswyrmd/lifecycle_runtime.cpp',
      'glasswyrmd/input_runtime.cpp',
    )
    gw_server_core_dependencies += gwipc_dep
  endif
  if get_option('libinput_backend')
    gw_server_core_sources += files(
      'glasswyrmd/real_input_controller.cpp',
      'glasswyrmd/real_input_runtime.cpp',
      'glasswyrmd/interactive_wm.cpp',
      'wm/interactive_policy.cpp',
    )
    gw_server_core_dependencies += gw_xkb_keymap_dep
  endif
  gw_server_core_lib = static_library(
    'gw-server-core',
    gw_server_core_sources,
    dependencies: gw_server_core_dependencies,
    include_directories: [include_directories('.'), gw_generated_include_dir],
    cpp_args: get_option('libgwipc') ? ['-DGW_SERVER_HAS_IPC=1'] : [],
  )
  gw_server_core_dep = declare_dependency(
    link_with: gw_server_core_lib,
    dependencies: gw_server_core_dependencies,
    include_directories: [include_directories('.'), gw_generated_include_dir],
  )
  glasswyrmd_program = executable(
    'glasswyrmd',
    files('glasswyrmd/main.cpp'),
    dependencies: gw_server_core_dep,
    install: true,
  )
endif

if get_option('gwm')
  gw_wm_policy_lib = static_library(
    'gw-wm-policy',
    files(
      'wm/interactive_policy.cpp',
      'wm/policy_engine.cpp',
      'wm/transaction.cpp',
    ),
    include_directories: include_directories('.'),
  )
  gw_wm_policy_dep = declare_dependency(
    link_with: gw_wm_policy_lib,
    include_directories: include_directories('.'),
  )
  gwm_options_lib = static_library(
    'gwm-options',
    files('gwm/options.cpp'),
    include_directories: [include_directories('.'), gw_generated_include_dir],
  )
  gwm_options_dep = declare_dependency(
    link_with: gwm_options_lib,
    include_directories: [include_directories('.'), gw_generated_include_dir],
  )
  gwm_program = executable(
    'gwm',
    files(
      'gwm/main.cpp',
      'gwm/runtime.cpp',
      'gwm/contract_dispatch.cpp',
      'gwm/signal_runtime.cpp',
    ),
    dependencies: [gwm_options_dep, gw_wm_policy_dep, gwipc_dep],
    install: true,
  )
  gwm_m5_producer_program = executable(
    'gwm_m5_producer',
    files('../tests/integration/gwm_m5_producer.cpp'),
    dependencies: gwipc_dep,
    install: false,
  )
endif

if get_option('gwcomp')
  gwcomp_backend_dependencies = [gw_output_dep]
  if get_option('headless_backend')
    gwcomp_backend_dependencies += gw_headless_dep
  endif
  if get_option('drm_backend')
    gwcomp_backend_dependencies += gw_drm_presenter_dep
  endif
  gwcomp_options_sources = files(
    'gwcomp/options.cpp',
    'gwcomp/compositor.cpp',
    'gwcomp/scene_manifest.cpp',
    'gwcomp/runtime.cpp',
    'gwcomp/signal_runtime.cpp',
    'gwcomp/session_state_coordinator.cpp',
    'gwcomp/contract_dispatch.cpp',
    'gwcomp/presentation_transaction.cpp',
  )
  if get_option('drm_backend')
    gwcomp_options_sources += files('gwcomp/drm_runtime.cpp')
  endif
  gwcomp_options_lib = static_library(
    'gwcomp-options',
    gwcomp_options_sources,
    include_directories: [include_directories('.'), gw_generated_include_dir],
    dependencies: [gwipc_dep, gw_compositor_geometry_dep,
                   gw_render_software_dep] + gwcomp_backend_dependencies,
  )
  gwcomp_options_dep = declare_dependency(
    link_with: gwcomp_options_lib,
    dependencies: [gwipc_dep, gw_compositor_geometry_dep,
                   gw_render_software_dep] + gwcomp_backend_dependencies,
    include_directories: [include_directories('.'), gw_generated_include_dir],
  )
  gwcomp_program = executable(
    'gwcomp',
    files('gwcomp/main.cpp'),
    dependencies: [gwcomp_options_dep, gwipc_dep, gw_compositor_geometry_dep,
                   gw_render_software_dep] + gwcomp_backend_dependencies,
    install: true,
  )
  gwcomp_m4_producer_program = executable(
    'gwcomp_m4_producer',
    files('../tests/integration/gwcomp_m4_producer.cpp',
          '../tests/integration/gwcomp_m4_edge_scenarios.cpp'),
    include_directories: include_directories('../tests/integration'),
    dependencies: gwipc_dep,
    install: false,
  )
endif

if get_option('glasswyrmd') and get_option('gwm') and get_option('gwcomp')
  gw_session_launcher_lib = static_library(
    'gw-session-launcher',
    files(
      'session/launcher.cpp',
      'session/process_supervisor.cpp',
    ),
    include_directories: [include_directories('.'), gw_generated_include_dir],
  )
  gw_session_launcher_dep = declare_dependency(
    link_with: gw_session_launcher_lib,
    include_directories: [include_directories('.'), gw_generated_include_dir],
  )
  glasswyrm_session_program = executable(
    'glasswyrm-session',
    files('session/main.cpp'),
    dependencies: gw_session_launcher_dep,
    install: true,
  )
endif
