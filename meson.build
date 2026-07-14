project(
  'glasswyrm',
  ['cpp', 'c'],
  version: '0.2.0',
  license: 'MIT',
  default_options: [
    'cpp_std=c++20',
    'c_std=c17',
    'warning_level=2',
  ],
)

if get_option('gwm') and not get_option('libgwipc')
  error('gwm requires -Dlibgwipc=true for the Milestone 5 policy contract')
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
if get_option('render_gl')
  error('the OpenGL renderer is not implemented')
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
