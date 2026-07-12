#include <stdio.h>
#include <stdlib.h>
#include <xcb/xcb.h>

static int fail(xcb_connection_t *connection, const char *message) {
  fprintf(stderr, "xcb_milestone6_probe: %s\n", message);
  xcb_disconnect(connection);
  return 1;
}

static int checked(xcb_connection_t *connection, xcb_void_cookie_t cookie,
                   const char *message) {
  xcb_generic_error_t *error = xcb_request_check(connection, cookie);
  if (error == NULL) return 1;
  fprintf(stderr, "xcb_milestone6_probe: %s (error=%u request=%u)\n", message,
          error->error_code, error->major_code);
  free(error);
  return 0;
}

static xcb_generic_event_t *wait_event(xcb_connection_t *connection,
                                       uint8_t type) {
  for (;;) {
    xcb_generic_event_t *event = xcb_wait_for_event(connection);
    if (event == NULL) return NULL;
    if ((event->response_type & 0x7fU) == type) return event;
    free(event);
  }
}

int main(void) {
  int screen_index = 0;
  xcb_connection_t *connection = xcb_connect(NULL, &screen_index);
  if (xcb_connection_has_error(connection))
    return fail(connection, "connection setup failed");
  const xcb_setup_t *setup = xcb_get_setup(connection);
  xcb_screen_iterator_t screens = xcb_setup_roots_iterator(setup);
  while (screen_index-- > 0) xcb_screen_next(&screens);
  if (screens.rem == 0) return fail(connection, "setup has no screen");
  xcb_screen_t *screen = screens.data;

  const xcb_window_t first = xcb_generate_id(connection);
  const xcb_window_t second = xcb_generate_id(connection);
  const uint32_t event_mask = XCB_EVENT_MASK_STRUCTURE_NOTIFY;
  if (!checked(connection,
               xcb_create_window_checked(
                   connection, XCB_COPY_FROM_PARENT, first, screen->root, 10,
                   20, 320, 200, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT,
                   screen->root_visual, XCB_CW_EVENT_MASK, &event_mask),
               "CreateWindow(first) failed") ||
      !checked(connection,
               xcb_create_window_checked(
                   connection, XCB_COPY_FROM_PARENT, second, screen->root, 30,
                   40, 200, 120, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT,
                   screen->root_visual, 0, NULL),
               "CreateWindow(second) failed"))
    return fail(connection, "window creation did not commit");

  if (!checked(connection, xcb_map_window_checked(connection, first),
               "MapWindow(first) failed"))
    return fail(connection, "first map did not commit");
  xcb_generic_event_t *generic = wait_event(connection, XCB_MAP_NOTIFY);
  if (generic == NULL) return fail(connection, "MapNotify was not delivered");
  const xcb_map_notify_event_t *mapped = (xcb_map_notify_event_t *)generic;
  if (mapped->event != first || mapped->window != first) {
    free(generic);
    return fail(connection, "MapNotify fields mismatch");
  }
  free(generic);
  if (!checked(connection, xcb_map_window_checked(connection, second),
               "MapWindow(second) failed"))
    return fail(connection, "second map did not commit");

  const uint16_t configure_mask =
      XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH |
      XCB_CONFIG_WINDOW_HEIGHT | XCB_CONFIG_WINDOW_SIBLING |
      XCB_CONFIG_WINDOW_STACK_MODE;
  const uint32_t configure_values[] = {70, 80, 360, 240, second,
                                       XCB_STACK_MODE_ABOVE};
  if (!checked(connection,
               xcb_configure_window_checked(connection, first, configure_mask,
                                            configure_values),
               "ConfigureWindow failed"))
    return fail(connection, "configure did not commit");
  generic = wait_event(connection, XCB_CONFIGURE_NOTIFY);
  if (generic == NULL)
    return fail(connection, "ConfigureNotify was not delivered");
  const xcb_configure_notify_event_t *configured =
      (xcb_configure_notify_event_t *)generic;
  if (configured->event != first || configured->window != first ||
      configured->x != 70 || configured->y != 80 ||
      configured->width != 360 || configured->height != 240) {
    free(generic);
    return fail(connection, "ConfigureNotify fields mismatch");
  }
  free(generic);

  xcb_generic_error_t *error = NULL;
  xcb_get_geometry_reply_t *geometry = xcb_get_geometry_reply(
      connection, xcb_get_geometry(connection, first), &error);
  if (geometry == NULL || error != NULL || geometry->x != 70 ||
      geometry->y != 80 || geometry->width != 360 || geometry->height != 240) {
    free(geometry);
    free(error);
    return fail(connection, "committed GetGeometry mismatch");
  }
  free(geometry);
  xcb_get_window_attributes_reply_t *attributes =
      xcb_get_window_attributes_reply(
          connection, xcb_get_window_attributes(connection, first), &error);
  if (attributes == NULL || error != NULL ||
      attributes->map_state != XCB_MAP_STATE_VIEWABLE ||
      (attributes->your_event_mask & XCB_EVENT_MASK_STRUCTURE_NOTIFY) == 0) {
    free(attributes);
    free(error);
    return fail(connection, "mapped GetWindowAttributes mismatch");
  }
  free(attributes);

  if (!checked(connection, xcb_unmap_window_checked(connection, first),
               "UnmapWindow failed"))
    return fail(connection, "unmap did not commit");
  generic = wait_event(connection, XCB_UNMAP_NOTIFY);
  if (generic == NULL) return fail(connection, "UnmapNotify was not delivered");
  const xcb_unmap_notify_event_t *unmapped = (xcb_unmap_notify_event_t *)generic;
  if (unmapped->event != first || unmapped->window != first ||
      unmapped->from_configure) {
    free(generic);
    return fail(connection, "UnmapNotify fields mismatch");
  }
  free(generic);

  if (!checked(connection, xcb_destroy_window_checked(connection, first),
               "DestroyWindow failed"))
    return fail(connection, "destroy did not commit");
  generic = wait_event(connection, XCB_DESTROY_NOTIFY);
  if (generic == NULL)
    return fail(connection, "DestroyNotify was not delivered");
  const xcb_destroy_notify_event_t *destroyed =
      (xcb_destroy_notify_event_t *)generic;
  if (destroyed->event != first || destroyed->window != first) {
    free(generic);
    return fail(connection, "DestroyNotify fields mismatch");
  }
  free(generic);

  xcb_query_tree_reply_t *tree = xcb_query_tree_reply(
      connection, xcb_query_tree(connection, screen->root), &error);
  int found_first = 0;
  int found_second = 0;
  if (tree != NULL) {
    const xcb_window_t *children = xcb_query_tree_children(tree);
    for (int index = 0; index < xcb_query_tree_children_length(tree); ++index) {
      found_first |= children[index] == first;
      found_second |= children[index] == second;
    }
  }
  if (tree == NULL || error != NULL || found_first || !found_second) {
    free(tree);
    free(error);
    return fail(connection, "final QueryTree mismatch");
  }
  free(tree);

  printf("xcb milestone6 probe passed: root=0x%08x survivor=0x%08x\n",
         screen->root, second);
  xcb_disconnect(connection);
  return 0;
}
