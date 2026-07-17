#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xcb/bigreq.h>
#include <xcb/composite.h>
#include <xcb/damage.h>
#include <xcb/randr.h>
#include <xcb/render.h>
#include <xcb/shm.h>
#include <xcb/xcb.h>
#include <xcb/xfixes.h>

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
  bool composite;
  bool randr;
  char error[256];
} Probe;

static void fail(Probe *probe, const char *message) {
  if (probe->error[0] == '\0')
    (void)snprintf(probe->error, sizeof(probe->error), "%s", message);
}

static bool checked(xcb_connection_t *connection, xcb_void_cookie_t cookie,
                    Probe *probe, const char *operation) {
  xcb_generic_error_t *error = xcb_request_check(connection, cookie);
  if (!error) return true;
  if (probe->error[0] == '\0')
    (void)snprintf(probe->error, sizeof(probe->error),
                   "%s returned X11 error %u", operation, error->error_code);
  free(error);
  return false;
}

static xcb_generic_event_t *event_with_type(xcb_connection_t *connection,
                                            uint8_t type) {
  struct pollfd descriptor = {xcb_get_file_descriptor(connection), POLLIN, 0};
  for (unsigned attempt = 0; attempt < 30; ++attempt) {
    xcb_generic_event_t *event;
    while ((event = xcb_poll_for_event(connection)) != NULL) {
      if ((event->response_type & 0x7fU) == type) return event;
      free(event);
    }
    if (poll(&descriptor, 1, 100) < 0) return NULL;
  }
  return NULL;
}

static bool extension_present(xcb_connection_t *connection,
                              const char *name) {
  xcb_generic_error_t *error = NULL;
  xcb_query_extension_reply_t *reply = xcb_query_extension_reply(
      connection,
      xcb_query_extension(connection, (uint16_t)strlen(name), name), &error);
  const bool result = reply && !error && reply->present;
  free(error);
  free(reply);
  return result;
}

static bool probe_randr_profile(xcb_connection_t *connection,
                                xcb_screen_t *screen, Probe *probe) {
  xcb_generic_error_t *error = NULL;
  xcb_randr_query_version_reply_t *version = xcb_randr_query_version_reply(
      connection, xcb_randr_query_version(connection, 1, 3), &error);
  bool passed = version && !error && version->major_version == 1 &&
                version->minor_version == 3;
  free(error); error = NULL; free(version);

  xcb_randr_get_screen_info_reply_t *screen_info =
      xcb_randr_get_screen_info_reply(
          connection, xcb_randr_get_screen_info(connection, screen->root),
          &error);
  passed = passed && screen_info && !error && screen_info->root == screen->root &&
           screen_info->nSizes == 1 && screen_info->rotation == XCB_RANDR_ROTATION_ROTATE_0;
  free(error); error = NULL; free(screen_info);

  xcb_randr_get_screen_size_range_reply_t *range =
      xcb_randr_get_screen_size_range_reply(
          connection,
          xcb_randr_get_screen_size_range(connection, screen->root), &error);
  passed = passed && range && !error && range->min_width == screen->width_in_pixels &&
           range->min_height == screen->height_in_pixels &&
           range->max_width == screen->width_in_pixels &&
           range->max_height == screen->height_in_pixels;
  free(error); error = NULL; free(range);

  xcb_randr_get_screen_resources_reply_t *resources =
      xcb_randr_get_screen_resources_reply(
          connection,
          xcb_randr_get_screen_resources(connection, screen->root), &error);
  const bool resource_shape = resources && !error &&
                              resources->num_crtcs == 1 &&
                              resources->num_outputs == 1 &&
                              resources->num_modes == 1;
  passed = passed && resource_shape;
  free(error); error = NULL;
  if (!resource_shape) {
    free(resources);
    return false;
  }
  const xcb_randr_crtc_t crtc = xcb_randr_get_screen_resources_crtcs(resources)[0];
  const xcb_randr_output_t output =
      xcb_randr_get_screen_resources_outputs(resources)[0];
  const xcb_randr_mode_info_t mode =
      xcb_randr_get_screen_resources_modes(resources)[0];
  const xcb_timestamp_t config_timestamp = resources->config_timestamp;
  free(resources);

  xcb_randr_get_screen_resources_current_reply_t *current =
      xcb_randr_get_screen_resources_current_reply(
          connection,
          xcb_randr_get_screen_resources_current(connection, screen->root),
          &error);
  passed = passed && current && !error && current->num_crtcs == 1 &&
           current->num_outputs == 1 && current->num_modes == 1 &&
           xcb_randr_get_screen_resources_current_crtcs(current)[0] == crtc &&
           xcb_randr_get_screen_resources_current_outputs(current)[0] == output &&
           xcb_randr_get_screen_resources_current_modes(current)[0].id == mode.id;
  free(error); error = NULL; free(current);

  xcb_randr_get_output_info_reply_t *output_info =
      xcb_randr_get_output_info_reply(
          connection,
          xcb_randr_get_output_info(connection, output, config_timestamp),
          &error);
  passed = passed && output_info && !error &&
           output_info->connection == XCB_RANDR_CONNECTION_CONNECTED &&
           output_info->crtc == crtc && output_info->num_crtcs == 1 &&
           output_info->num_modes == 1 && output_info->num_preferred == 1 &&
           output_info->num_clones == 0 && output_info->name_len == 11 &&
           memcmp(xcb_randr_get_output_info_name(output_info), "Glasswyrm-1", 11) == 0;
  free(error); error = NULL; free(output_info);

  xcb_randr_get_crtc_info_reply_t *crtc_info = xcb_randr_get_crtc_info_reply(
      connection, xcb_randr_get_crtc_info(connection, crtc, config_timestamp),
      &error);
  passed = passed && crtc_info && !error && crtc_info->x == 0 &&
           crtc_info->y == 0 && crtc_info->mode == mode.id &&
           crtc_info->rotation == XCB_RANDR_ROTATION_ROTATE_0 &&
           crtc_info->num_outputs == 1 &&
           xcb_randr_get_crtc_info_outputs(crtc_info)[0] == output;
  free(error); error = NULL; free(crtc_info);

  xcb_randr_list_output_properties_reply_t *properties =
      xcb_randr_list_output_properties_reply(
          connection, xcb_randr_list_output_properties(connection, output),
          &error);
  passed = passed && properties && !error && properties->num_atoms == 0;
  free(error); error = NULL; free(properties);

  xcb_randr_query_output_property_reply_t *property_info =
      xcb_randr_query_output_property_reply(
          connection,
          xcb_randr_query_output_property(connection, output, XCB_ATOM_PRIMARY),
          &error);
  passed = passed && property_info && !error && !property_info->pending &&
           !property_info->range && !property_info->immutable;
  free(error); error = NULL; free(property_info);

  xcb_randr_get_output_property_reply_t *property =
      xcb_randr_get_output_property_reply(
          connection,
          xcb_randr_get_output_property(connection, output, XCB_ATOM_PRIMARY,
                                        XCB_ATOM_NONE, 0, 64, 0, 0),
          &error);
  passed = passed && property && !error && property->format == 0 &&
           property->type == XCB_ATOM_NONE && property->num_items == 0;
  free(error); error = NULL; free(property);

  xcb_randr_get_crtc_gamma_size_reply_t *gamma =
      xcb_randr_get_crtc_gamma_size_reply(
          connection, xcb_randr_get_crtc_gamma_size(connection, crtc), &error);
  passed = passed && gamma && !error && gamma->size == 0;
  free(error); error = NULL; free(gamma);

  xcb_randr_get_output_primary_reply_t *primary =
      xcb_randr_get_output_primary_reply(
          connection, xcb_randr_get_output_primary(connection, screen->root),
          &error);
  passed = passed && primary && !error && primary->output == output;
  free(error); error = NULL; free(primary);

  passed = checked(connection,
                   xcb_randr_select_input_checked(
                       connection, screen->root,
                       XCB_RANDR_NOTIFY_MASK_SCREEN_CHANGE |
                           XCB_RANDR_NOTIFY_MASK_CRTC_CHANGE |
                           XCB_RANDR_NOTIFY_MASK_OUTPUT_CHANGE |
                           XCB_RANDR_NOTIFY_MASK_OUTPUT_PROPERTY),
                   probe, "RANDR SelectInput") && passed;

  xcb_randr_set_crtc_config_reply_t *same = xcb_randr_set_crtc_config_reply(
      connection,
      xcb_randr_set_crtc_config(connection, crtc, config_timestamp,
                                config_timestamp, 0, 0, mode.id,
                                XCB_RANDR_ROTATION_ROTATE_0, 1, &output),
      &error);
  passed = passed && same && !error &&
           same->status == XCB_RANDR_SET_CONFIG_SUCCESS;
  free(error); error = NULL; free(same);

  xcb_randr_set_crtc_config_reply_t *rejected =
      xcb_randr_set_crtc_config_reply(
          connection,
          xcb_randr_set_crtc_config(connection, crtc, config_timestamp,
                                    config_timestamp, 1, 0, mode.id,
                                    XCB_RANDR_ROTATION_ROTATE_0, 1, &output),
          &error);
  passed = passed && rejected && !error &&
           rejected->status == XCB_RANDR_SET_CONFIG_FAILED;
  free(error); free(rejected);
  return passed;
}

static void json_string(FILE *stream, const char *value) {
  fputc('"', stream);
  for (; *value; ++value) {
    if (*value == '"' || *value == '\\') fputc('\\', stream);
    if (*value == '\n')
      fputs("\\n", stream);
    else
      fputc(*value, stream);
  }
  fputc('"', stream);
}

static bool write_result(const char *path, const Probe *probe) {
  FILE *stream = fopen(path, "w");
  if (!stream) return false;
  const bool passed = probe->registry && probe->big_requests && probe->shm &&
                      probe->xfixes_version && probe->selection_notify &&
                      probe->region_algebra && probe->damage_version &&
                      probe->damage_notify && probe->damage_subtract &&
                      probe->render && probe->composite && probe->randr;
  fprintf(stream,
          "{\n  \"schema\": 1,\n  \"probe\": \"m12_xcb_probe\",\n"
          "  \"checks\": {\n"
          "    \"registry\": %s,\n    \"big_requests\": %s,\n"
          "    \"mit_shm\": %s,\n    \"xfixes_version\": %s,\n"
          "    \"selection_notify\": %s,\n"
          "    \"region_algebra\": %s,\n"
          "    \"damage_version\": %s,\n"
          "    \"damage_notify\": %s,\n"
          "    \"damage_subtract\": %s,\n"
          "    \"render_version\": %s,\n"
          "    \"composite_version\": %s,\n"
          "    \"randr_version\": %s\n  },\n"
          "  \"passed\": %s,\n  \"error\": ",
          probe->registry ? "true" : "false",
          probe->big_requests ? "true" : "false",
          probe->shm ? "true" : "false",
          probe->xfixes_version ? "true" : "false",
          probe->selection_notify ? "true" : "false",
          probe->region_algebra ? "true" : "false",
          probe->damage_version ? "true" : "false",
          probe->damage_notify ? "true" : "false",
          probe->damage_subtract ? "true" : "false",
          probe->render ? "true" : "false",
          probe->composite ? "true" : "false",
          probe->randr ? "true" : "false", passed ? "true" : "false");
  json_string(stream, probe->error);
  fputs("\n}\n", stream);
  return fclose(stream) == 0;
}

int main(int argc, char **argv) {
  if (argc != 3 || strcmp(argv[1], "--output") != 0) {
    fprintf(stderr, "Usage: %s --output RESULT.json\n", argv[0]);
    return 2;
  }
  Probe probe = {0};
  int screen_index = 0;
  xcb_connection_t *connection = xcb_connect(NULL, &screen_index);
  if (!connection || xcb_connection_has_error(connection)) {
    fail(&probe, "xcb_connect failed");
    (void)write_result(argv[2], &probe);
    if (connection) xcb_disconnect(connection);
    return 1;
  }
  const xcb_setup_t *setup = xcb_get_setup(connection);
  xcb_screen_iterator_t screens = xcb_setup_roots_iterator(setup);
  for (int index = 0; index < screen_index && screens.rem; ++index)
    xcb_screen_next(&screens);
  if (!screens.rem) {
    fail(&probe, "X11 setup has no selected screen");
    goto done;
  }
  xcb_screen_t *screen = screens.data;

  static const char *extensions[] = {"BIG-REQUESTS", "MIT-SHM", "XFIXES",
                                     "DAMAGE", "RENDER", "Composite",
                                     "RANDR"};
  probe.registry = true;
  for (size_t index = 0; index < sizeof(extensions) / sizeof(extensions[0]);
       ++index)
    if (!extension_present(connection, extensions[index])) {
      probe.registry = false;
      fail(&probe, "required extension is absent");
    }

  xcb_generic_error_t *error = NULL;
  xcb_big_requests_enable_reply_t *big = xcb_big_requests_enable_reply(
      connection, xcb_big_requests_enable(connection), &error);
  probe.big_requests = big && !error && big->maximum_request_length >= 4194304U;
  if (!probe.big_requests) fail(&probe, "BIG-REQUESTS Enable failed");
  free(error); error = NULL; free(big);

  xcb_shm_query_version_reply_t *shm = xcb_shm_query_version_reply(
      connection, xcb_shm_query_version(connection), &error);
  probe.shm = shm && !error && shm->major_version == 1 && shm->minor_version >= 1;
  if (!probe.shm) fail(&probe, "MIT-SHM QueryVersion failed");
  free(error); error = NULL; free(shm);

  xcb_xfixes_query_version_reply_t *xfixes = xcb_xfixes_query_version_reply(
      connection, xcb_xfixes_query_version(connection, 2, 0), &error);
  probe.xfixes_version = xfixes && !error && xfixes->major_version == 2 &&
                         xfixes->minor_version == 0;
  if (!probe.xfixes_version) fail(&probe, "XFIXES QueryVersion failed");
  free(error); error = NULL; free(xfixes);

  const xcb_query_extension_reply_t *xfixes_data =
      xcb_get_extension_data(connection, &xcb_xfixes_id);
  if (xfixes_data && xfixes_data->present &&
      checked(connection,
              xcb_xfixes_select_selection_input_checked(
                  connection, screen->root, XCB_ATOM_PRIMARY,
                  XCB_XFIXES_SELECTION_EVENT_MASK_SET_SELECTION_OWNER),
              &probe, "XFIXES SelectSelectionInput") &&
      checked(connection,
              xcb_set_selection_owner_checked(connection, screen->root,
                                              XCB_ATOM_PRIMARY,
                                              XCB_CURRENT_TIME),
              &probe, "SetSelectionOwner")) {
    xcb_flush(connection);
    xcb_generic_event_t *event = event_with_type(
        connection, (uint8_t)(xfixes_data->first_event + XCB_XFIXES_SELECTION_NOTIFY));
    if (event) {
      const xcb_xfixes_selection_notify_event_t *notify =
          (const xcb_xfixes_selection_notify_event_t *)event;
      probe.selection_notify = notify->window == screen->root &&
                               notify->owner == screen->root &&
                               notify->selection == XCB_ATOM_PRIMARY;
      free(event);
    }
  }
  if (!probe.selection_notify) fail(&probe, "XFIXES SelectionNotify missing");

  const xcb_xfixes_region_t first = xcb_generate_id(connection);
  const xcb_xfixes_region_t second = xcb_generate_id(connection);
  const xcb_xfixes_region_t combined = xcb_generate_id(connection);
  const xcb_xfixes_region_t extents = xcb_generate_id(connection);
  const xcb_rectangle_t first_rectangles[] = {{0, 0, 32, 32},
                                               {40, 0, 16, 16}};
  const xcb_rectangle_t second_rectangles[] = {{16, 0, 32, 32}};
  bool regions_ok =
      checked(connection,
              xcb_xfixes_create_region_checked(connection, first, 2,
                                               first_rectangles),
              &probe, "XFIXES CreateRegion first") &&
      checked(connection,
              xcb_xfixes_create_region_checked(connection, second, 1,
                                               second_rectangles),
              &probe, "XFIXES CreateRegion second") &&
      checked(connection,
              xcb_xfixes_create_region_checked(connection, combined, 0, NULL),
              &probe, "XFIXES CreateRegion combined") &&
      checked(connection,
              xcb_xfixes_create_region_checked(connection, extents, 0, NULL),
              &probe, "XFIXES CreateRegion extents") &&
      checked(connection,
              xcb_xfixes_union_region_checked(connection, first, second,
                                              combined),
              &probe, "XFIXES UnionRegion") &&
      checked(connection,
              xcb_xfixes_intersect_region_checked(connection, first, second,
                                                  combined),
              &probe, "XFIXES IntersectRegion") &&
      checked(connection,
              xcb_xfixes_subtract_region_checked(connection, first, second,
                                                 combined),
              &probe, "XFIXES SubtractRegion") &&
      checked(connection,
              xcb_xfixes_translate_region_checked(connection, combined, 5, 7),
              &probe, "XFIXES TranslateRegion") &&
      checked(connection,
              xcb_xfixes_region_extents_checked(connection, combined, extents),
              &probe, "XFIXES RegionExtents");
  xcb_xfixes_fetch_region_reply_t *fetched = xcb_xfixes_fetch_region_reply(
      connection, xcb_xfixes_fetch_region(connection, extents), &error);
  if (regions_ok && fetched && !error &&
      xcb_xfixes_fetch_region_rectangles_length(fetched) == 1) {
    const xcb_rectangle_t *rectangle =
        xcb_xfixes_fetch_region_rectangles(fetched);
    probe.region_algebra = rectangle->x == 5 && rectangle->y == 7 &&
                           rectangle->width == 56 && rectangle->height == 32;
  }
  free(error); error = NULL; free(fetched);
  if (!probe.region_algebra) fail(&probe, "XFIXES region algebra failed");

  xcb_damage_query_version_reply_t *damage_version =
      xcb_damage_query_version_reply(
          connection, xcb_damage_query_version(connection, 1, 1), &error);
  probe.damage_version = damage_version && !error &&
                         damage_version->major_version == 1 &&
                         damage_version->minor_version == 1;
  free(error); error = NULL; free(damage_version);
  if (!probe.damage_version) fail(&probe, "DAMAGE QueryVersion failed");

  const xcb_damage_damage_t damage = xcb_generate_id(connection);
  const xcb_query_extension_reply_t *damage_data =
      xcb_get_extension_data(connection, &xcb_damage_id);
  if (damage_data && damage_data->present &&
      checked(connection,
              xcb_damage_create_checked(connection, damage, screen->root,
                                        XCB_DAMAGE_REPORT_LEVEL_NON_EMPTY),
              &probe, "DAMAGE Create") &&
      checked(connection, xcb_damage_add_checked(connection, screen->root, first),
              &probe, "DAMAGE Add")) {
    xcb_flush(connection);
    xcb_generic_event_t *event = event_with_type(
        connection, (uint8_t)(damage_data->first_event + XCB_DAMAGE_NOTIFY));
    if (event) {
      const xcb_damage_notify_event_t *notify =
          (const xcb_damage_notify_event_t *)event;
      probe.damage_notify = notify->damage == damage &&
                            notify->drawable == screen->root &&
                            notify->area.width != 0 && notify->area.height != 0;
      free(event);
    }
  }
  if (!probe.damage_notify) fail(&probe, "DAMAGE Notify missing");
  probe.damage_subtract = checked(
      connection,
      xcb_damage_subtract_checked(connection, damage, XCB_XFIXES_REGION_NONE,
                                  combined),
      &probe, "DAMAGE Subtract");
  if (!probe.damage_subtract) fail(&probe, "DAMAGE Subtract failed");
  (void)checked(connection, xcb_damage_destroy_checked(connection, damage),
                &probe, "DAMAGE Destroy");

  xcb_render_query_version_reply_t *render = xcb_render_query_version_reply(
      connection, xcb_render_query_version(connection, 0, 11), &error);
  probe.render = render && !error && render->major_version == 0 &&
                 render->minor_version == 11;
  free(error); error = NULL; free(render);
  if (!probe.render) fail(&probe, "RENDER QueryVersion failed");

  xcb_composite_query_version_reply_t *composite =
      xcb_composite_query_version_reply(
          connection, xcb_composite_query_version(connection, 0, 4), &error);
  probe.composite = composite && !error && composite->major_version == 0 &&
                    composite->minor_version == 4;
  free(error); error = NULL; free(composite);
  if (!probe.composite) fail(&probe, "COMPOSITE QueryVersion failed");

  probe.randr = probe_randr_profile(connection, screen, &probe);
  if (!probe.randr) fail(&probe, "RANDR bounded profile failed");

  (void)checked(connection, xcb_xfixes_destroy_region_checked(connection, first),
                &probe, "XFIXES DestroyRegion first");
  (void)checked(connection, xcb_xfixes_destroy_region_checked(connection, second),
                &probe, "XFIXES DestroyRegion second");
  (void)checked(connection,
                xcb_xfixes_destroy_region_checked(connection, combined), &probe,
                "XFIXES DestroyRegion combined");
  (void)checked(connection, xcb_xfixes_destroy_region_checked(connection, extents),
                &probe, "XFIXES DestroyRegion extents");

done:
  xcb_disconnect(connection);
  if (!write_result(argv[2], &probe)) return 1;
  return probe.registry && probe.big_requests && probe.shm &&
                 probe.xfixes_version && probe.selection_notify &&
                 probe.region_algebra && probe.damage_version &&
                 probe.damage_notify && probe.damage_subtract && probe.render &&
                 probe.composite && probe.randr
             ? 0
             : 1;
}
