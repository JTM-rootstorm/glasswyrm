project(
  'glasswyrm',
  ['cpp', 'c'],
  version: '0.2.0',
  license: 'GPL-3.0-only',
  default_options: [
    'cpp_std=c++20',
    'c_std=c17',
    'warning_level=2',
  ],
)

if get_option('gwm') and not get_option('libgwipc')
  error('gwm requires -Dlibgwipc=true for the Milestone 5 policy contract')
endif

if get_option('tools') and not get_option('libgwipc')
  error('tools requires -Dlibgwipc=true because gwinfo and gwout are real output-control clients')
endif

if get_option('gwcomp')
  if not get_option('libgwipc')
    error('gwcomp requires -Dlibgwipc=true for the Milestone 4 contract')
  endif
  if not get_option('headless_backend') and not get_option('drm_backend')
    error('gwcomp requires at least one presentation backend: -Dheadless_backend=true or -Ddrm_backend=true')
  endif
  if not get_option('render_software')
    error('gwcomp requires -Drender_software=true in Milestone 4')
  endif
endif

if get_option('drm_backend')
  if host_machine.system() != 'linux'
    error('the DRM backend requires Linux')
  endif
  if not get_option('gwcomp')
    error('the DRM backend requires -Dgwcomp=true')
  endif
  if not get_option('render_software')
    error('the DRM backend requires -Drender_software=true')
  endif
endif

libinput_dep = declare_dependency()
xkbcommon_dep = declare_dependency()
if get_option('libinput_backend')
  if host_machine.system() != 'linux'
    error('the libinput backend requires Linux')
  endif
  if not get_option('glasswyrmd')
    error('the libinput backend requires -Dglasswyrmd=true')
  endif
  if not get_option('libgwipc')
    error('the libinput backend requires -Dlibgwipc=true')
  endif

  libinput_dep = dependency('libinput', required: true)
  xkbcommon_dep = dependency('xkbcommon', required: true)
  dependency('xkeyboard-config', required: true)

  libinput_api_probe = '''
    #define _GNU_SOURCE
    #include <fcntl.h>
    #include <libinput.h>
    #include <stdint.h>
    #include <sys/timerfd.h>
    #include <time.h>
    static int open_restricted(const char *path, int flags, void *data) {
      (void)path; (void)flags; (void)data; return -1;
    }
    static void close_restricted(int fd, void *data) {
      (void)fd; (void)data;
    }
    int main(void) {
      const struct libinput_interface interface = {
        open_restricted, close_restricted
      };
      struct libinput *context = libinput_path_create_context(&interface, 0);
      struct libinput_device *device = libinput_path_add_device(context, "/dev/null");
      struct libinput_event *event = libinput_get_event(context);
      struct libinput_event_keyboard *keyboard = libinput_event_get_keyboard_event(event);
      struct libinput_event_pointer *pointer = libinput_event_get_pointer_event(event);
      (void)libinput_get_fd(context);
      (void)libinput_dispatch(context);
      (void)libinput_suspend(context);
      (void)libinput_resume(context);
      (void)libinput_event_keyboard_get_key(keyboard);
      (void)libinput_event_keyboard_get_key_state(keyboard);
      (void)libinput_event_keyboard_get_time_usec(keyboard);
      (void)libinput_event_pointer_get_dx(pointer);
      (void)libinput_event_pointer_get_dy(pointer);
      (void)libinput_event_pointer_get_absolute_x_transformed(pointer, 1);
      (void)libinput_event_pointer_get_absolute_y_transformed(pointer, 1);
      (void)libinput_event_pointer_get_button(pointer);
      (void)libinput_event_pointer_get_button_state(pointer);
      (void)libinput_event_pointer_has_axis(pointer, LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL);
      (void)libinput_event_pointer_get_scroll_value_v120(pointer, LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL);
      (void)libinput_device_has_capability(device, LIBINPUT_DEVICE_CAP_KEYBOARD);
      libinput_event_destroy(event);
      libinput_unref(context);
      return timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC) < 0;
    }
  '''
  if not meson.get_compiler('c').links(
      libinput_api_probe,
      dependencies: libinput_dep,
      name: 'required libinput path, event, suspend, wheel, and timerfd APIs',
    )
    error('libinput or libc lacks APIs required by the Glasswyrm real-input backend')
  endif

  xkb_api_probe = '''
    #include <xkbcommon/xkbcommon.h>
    int main(void) {
      struct xkb_rule_names names = {"evdev", "pc105", "us", "", ""};
      struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
      struct xkb_keymap *keymap = xkb_keymap_new_from_names(
          context, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
      struct xkb_state *state = xkb_state_new(keymap);
      (void)xkb_keymap_key_repeats(keymap, 38);
      (void)xkb_state_serialize_mods(state, XKB_STATE_MODS_EFFECTIVE);
      (void)xkb_state_key_get_one_sym(state, 38);
      xkb_state_unref(state);
      xkb_keymap_unref(keymap);
      xkb_context_unref(context);
      return 0;
    }
  '''
  if not meson.get_compiler('c').links(
      xkb_api_probe,
      dependencies: xkbcommon_dep,
      name: 'required libxkbcommon keymap, state, repeat, modifier, and keysym APIs',
    )
    error('libxkbcommon lacks APIs required by the Glasswyrm keyboard engine')
  endif
endif
egl_dep = declare_dependency()
glesv2_dep = declare_dependency()
gbm_dep = declare_dependency()
if get_option('render_gl')
  if not get_option('gwcomp')
    error('the EGL/GLES renderer requires -Dgwcomp=true')
  endif
  egl_dep = dependency('egl', required: true)
  glesv2_dep = dependency('glesv2', required: true)
  gbm_dep = dependency('gbm', required: false)
  gles_probe = '''
    #include <EGL/egl.h>
    #include <EGL/eglext.h>
    #include <GLES2/gl2.h>
    int main(void) {
      EGLDisplay display = eglGetPlatformDisplay(
          EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, 0);
      glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 1, 1, GL_RGBA,
                     GL_UNSIGNED_BYTE, 0);
      glScissor(0, 0, 1, 1);
      glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, 0);
      return display == EGL_NO_DISPLAY;
    }
  '''
  if not meson.get_compiler('cpp').links(
      gles_probe, dependencies: [egl_dep, glesv2_dep],
      name: 'required EGL 1.5 platform and GLES2 rendering APIs',
  )
    error('EGL/GLES lacks APIs required by the Glasswyrm renderer')
  endif
endif
if get_option('render_vulkan')
  error('the Vulkan renderer is not implemented')
endif

gw_include_dir = include_directories('include')

libdrm_dep = dependency('libdrm', required: get_option('drm_backend'))
if get_option('drm_backend')
  drm_compile_probe = '''
    #include <stdint.h>
    #include <sys/ioctl.h>
    #include <xf86drm.h>
    #include <xf86drmMode.h>
    #include <drm.h>
    #include <drm_fourcc.h>
    #include <drm_mode.h>
    int main(void) {
      uint32_t handles[4] = {0};
      uint32_t pitches[4] = {0};
      uint32_t offsets[4] = {0};
      uint32_t framebuffer = 0;
      struct drm_mode_create_dumb create = {0};
      struct drm_mode_map_dumb map = {0};
      struct drm_mode_destroy_dumb destroy = {0};
      drmEventContext events = {0};
      drmModeAtomicReq *request = drmModeAtomicAlloc();
      (void)drmSetClientCap(-1, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
      (void)drmSetClientCap(-1, DRM_CLIENT_CAP_ATOMIC, 1);
      (void)drmModeAtomicAddProperty(request, 0, 0, 0);
      (void)drmModeAtomicCommit(-1, request,
          DRM_MODE_ATOMIC_TEST_ONLY | DRM_MODE_ATOMIC_ALLOW_MODESET, 0);
      drmModeAtomicFree(request);
      (void)drmModeAddFB2(-1, 1, 1, DRM_FORMAT_XRGB8888, handles, pitches,
                         offsets, &framebuffer, 0);
      (void)drmModePageFlip(-1, 0, 0, DRM_MODE_PAGE_FLIP_EVENT, 0);
      (void)drmHandleEvent(-1, &events);
      (void)ioctl(-1, DRM_IOCTL_MODE_CREATE_DUMB, &create);
      (void)ioctl(-1, DRM_IOCTL_MODE_MAP_DUMB, &map);
      return ioctl(-1, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
    }
  '''
  if not meson.get_compiler('c').links(
      drm_compile_probe,
      dependencies: libdrm_dep,
      name: 'required libdrm KMS, atomic, page-flip, and dumb-buffer API',
    )
    error('libdrm lacks the KMS, atomic, page-flip, or dumb-buffer API required by Glasswyrm')
  endif
endif

if get_option('asan')
  add_project_arguments('-fsanitize=address', language: ['c', 'cpp'])
  add_project_link_arguments('-fsanitize=address', language: ['c', 'cpp'])
endif

if get_option('ubsan')
  add_project_arguments('-fsanitize=undefined', language: ['c', 'cpp'])
  add_project_link_arguments('-fsanitize=undefined', language: ['c', 'cpp'])
endif

if get_option('tsan')
  add_project_arguments('-fsanitize=thread', language: ['c', 'cpp'])
  add_project_link_arguments('-fsanitize=thread', language: ['c', 'cpp'])
endif

config_data = configuration_data()
config_data.set_quoted('GW_PROJECT_VERSION', meson.project_version())
config_data.set10('GW_HAS_DRM_BACKEND', get_option('drm_backend'))
config_data.set10('GW_HAS_HEADLESS_BACKEND', get_option('headless_backend'))
config_data.set10('GW_HAS_LIBINPUT_BACKEND', get_option('libinput_backend'))
config_data.set10('GW_HAS_GLES_RENDERER', get_option('render_gl'))
config_data.set10('GW_HAS_GBM', get_option('render_gl') and gbm_dep.found())
config_data.set10('GW_HAS_EXPERIMENTAL', get_option('experimental'))
config_data.set10('GW_HAS_LIBGWIPC', get_option('libgwipc'))

configure_file(
  output: 'config.hpp',
  configuration: config_data,
)

gw_generated_include_dir = include_directories('.')

subdir('src')
if get_option('tools')
  subdir('tools')
endif
subdir('tests')

if get_option('physical_validation_provenance')
  if not (get_option('glasswyrmd') and get_option('gwm') and
          get_option('gwcomp') and get_option('tools') and
          get_option('drm_backend') and get_option('libinput_backend'))
    error('physical_validation_provenance requires glasswyrmd, gwm, gwcomp, tools, drm_backend, and libinput_backend')
  endif
  if not xcb_test_dependency.found()
    error('physical_validation_provenance requires XCB for the M14 hardware client')
  endif

  git_program = find_program('git', required: true)
  source_commit_result = run_command(
    git_program,
    '-C', meson.project_source_root(),
    'rev-parse', '--verify', 'HEAD',
    check: false,
  )
  source_commit = source_commit_result.stdout().strip()
  if source_commit_result.returncode() != 0 or source_commit == ''
    error('physical_validation_provenance requires a Git checkout with an exact HEAD commit')
  endif

  physical_provenance_generator = find_program(
    'tools/generate_m14_build_provenance.py',
  )
  custom_target(
    'm14-physical-build-provenance',
    input: [
      gwm_program,
      gwcomp_program,
      glasswyrmd_program,
      gwout_program,
      gwinfo_program,
      m14_vrr_client,
      gw_drm_probe_program,
    ],
    output: 'glasswyrm-m14-build-manifest.json',
    command: [
      physical_provenance_generator,
      '--source-root', meson.project_source_root(),
      '--git', git_program.full_path(),
      '--expected-commit', source_commit,
      '--output', '@OUTPUT@',
      '--roles', 'gwm,gwcomp,server,gwout,gwinfo,client,drm-probe',
      '--binaries', '@INPUT@',
    ],
    build_by_default: true,
  )
endif
