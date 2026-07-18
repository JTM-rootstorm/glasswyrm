#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <xcb/xcb.h>

typedef struct Probe {
  bool registry;
  bool big_requests;
  bool shm;
  bool xfixes_version;
  bool selection_notify;
  bool region_algebra;
  bool damage_version;
  bool damage_notify;
  bool damage_subtract;
  bool render;
  bool render_formats;
  bool render_exact_pixels;
  bool composite;
  bool composite_named_lifetime;
  bool randr;
  bool randr_reporting;
  bool recoverable_errors;
  uint8_t registry_major[7];
  uint8_t registry_event[7];
  uint8_t registry_error[7];
  uint32_t render_pixel;
  uint32_t named_pixel;
  uint32_t randr_output;
  uint32_t randr_crtc;
  uint32_t randr_mode;
  char error[256];
} Probe;

void probe_fail(Probe *probe, const char *message);
bool probe_checked(xcb_connection_t *connection, xcb_void_cookie_t cookie,
                   Probe *probe, const char *operation);
xcb_generic_event_t *probe_event_with_type(xcb_connection_t *connection,
                                           uint8_t type);

bool probe_shm_transport(xcb_connection_t *connection, xcb_screen_t *screen,
                         Probe *probe);
bool probe_render_pixels(xcb_connection_t *connection, xcb_screen_t *screen,
                         Probe *probe);
bool probe_composite_lifetime(xcb_connection_t *connection,
                              xcb_screen_t *screen, Probe *probe);
