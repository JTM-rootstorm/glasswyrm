#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xcb/xcb.h>

static int fail(xcb_connection_t *connection, const char *message) {
  fprintf(stderr, "xcb_milestone7_probe: %s\n", message);
  if (connection != NULL) xcb_disconnect(connection);
  return 1;
}

static int checked(xcb_connection_t *connection, xcb_void_cookie_t cookie,
                   const char *message) {
  xcb_generic_error_t *error = xcb_request_check(connection, cookie);
  if (error == NULL) return 1;
  fprintf(stderr, "xcb_milestone7_probe: %s (error=%u request=%u)\n", message,
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

static void make_pattern(uint32_t *pixels) {
  for (unsigned y = 0; y < 64; ++y)
    for (unsigned x = 0; x < 64; ++x)
      pixels[y * 64 + x] = ((x / 8U) ^ (y / 8U)) & 1U ? 0x00d94848U
                                                            : 0x003c78d8U;
}

int main(void) {
  int screen_index = 0;
  xcb_connection_t *connection = xcb_connect(NULL, &screen_index);
  if (connection == NULL || xcb_connection_has_error(connection) != 0)
    return fail(connection, "connection setup failed");
  const xcb_setup_t *setup = xcb_get_setup(connection);
  xcb_screen_iterator_t screens = xcb_setup_roots_iterator(setup);
  while (screen_index-- > 0) xcb_screen_next(&screens);
  if (screens.rem == 0) return fail(connection, "setup has no screen");
  xcb_screen_t *screen = screens.data;

  const xcb_window_t window = xcb_generate_id(connection);
  const xcb_pixmap_t pixmap = xcb_generate_id(connection);
  const xcb_gcontext_t gc = xcb_generate_id(connection);
  const uint32_t window_values[] = {
      0x000b1830U,
      XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_EXPOSURE};
  if (!checked(connection,
               xcb_create_window_checked(
                   connection, XCB_COPY_FROM_PARENT, window, screen->root, 24,
                   32, 320, 200, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT,
                   screen->root_visual, XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK,
                   window_values),
               "CreateWindow failed") ||
      !checked(connection,
               xcb_create_pixmap_checked(connection, 24, pixmap, screen->root,
                                         64, 64),
               "CreatePixmap failed"))
    return fail(connection, "drawable creation failed");

  const uint32_t gc_values[] = {0x00ffffffU, 0x00d94848U,
                                XCB_GRAPHICS_EXPOSURE};
  if (!checked(connection,
               xcb_create_gc_checked(
                   connection, gc, pixmap,
                   XCB_GC_FOREGROUND | XCB_GC_PLANE_MASK |
                       XCB_GC_GRAPHICS_EXPOSURES,
                   gc_values),
               "CreateGC failed"))
    return fail(connection, "GC creation failed");

  uint32_t pixels[64 * 64];
  make_pattern(pixels);
  if (!checked(connection,
               xcb_put_image_checked(connection, XCB_IMAGE_FORMAT_Z_PIXMAP,
                                     pixmap, gc, 64, 64, 0, 0, 0, 24,
                                     sizeof(pixels), (const uint8_t *)pixels),
               "PutImage failed"))
    return fail(connection, "image upload failed");

  const xcb_rectangle_t hidden_fill[] = {{8, 8, 96, 48},
                                         {128, 24, 80, 72}};
  if (!checked(connection,
               xcb_poly_fill_rectangle_checked(connection, window, gc, 2,
                                               hidden_fill),
               "PolyFillRectangle(hidden) failed") ||
      !checked(connection,
               xcb_copy_area_checked(connection, pixmap, window, gc, 0, 0, 40,
                                     56, 64, 64),
               "CopyArea(pixmap) failed") ||
      !checked(connection, xcb_map_window_checked(connection, window),
               "MapWindow failed"))
    return fail(connection, "initial drawing or map failed");

  xcb_generic_event_t *event = wait_event(connection, XCB_MAP_NOTIFY);
  if (event == NULL) return fail(connection, "MapNotify missing");
  free(event);
  event = wait_event(connection, XCB_EXPOSE);
  if (event == NULL) return fail(connection, "initial Expose missing");
  free(event);

  const uint32_t foreground = 0x00ef4747U;
  if (!checked(connection,
               xcb_change_gc_checked(connection, gc, XCB_GC_FOREGROUND,
                                     &foreground),
               "ChangeGC failed"))
    return fail(connection, "GC update failed");
  const xcb_rectangle_t visible_fill = {224, 24, 72, 88};
  if (!checked(connection,
               xcb_poly_fill_rectangle_checked(connection, window, gc, 1,
                                               &visible_fill),
               "PolyFillRectangle(visible) failed") ||
      !checked(connection,
               xcb_copy_area_checked(connection, window, window, gc, 40, 56,
                                     72, 80, 96, 64),
               "CopyArea(overlap) failed") ||
      !checked(connection,
               xcb_clear_area_checked(connection, 1, window, 16, 144, 80, 32),
               "ClearArea failed"))
    return fail(connection, "visible drawing failed");
  event = wait_event(connection, XCB_EXPOSE);
  if (event == NULL) return fail(connection, "ClearArea Expose missing");
  free(event);

  const uint32_t resize[] = {384, 256};
  if (!checked(connection,
               xcb_configure_window_checked(
                   connection, window,
                   XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, resize),
               "ConfigureWindow failed"))
    return fail(connection, "resize failed");
  event = wait_event(connection, XCB_CONFIGURE_NOTIFY);
  if (event == NULL) return fail(connection, "ConfigureNotify missing");
  free(event);
  event = wait_event(connection, XCB_EXPOSE);
  if (event == NULL) return fail(connection, "resize Expose missing");
  free(event);
  const xcb_rectangle_t growth[] = {{320, 0, 64, 256}, {0, 200, 320, 56}};
  if (!checked(connection,
               xcb_poly_fill_rectangle_checked(connection, window, gc, 2,
                                               growth),
               "PolyFillRectangle(growth) failed") ||
      !checked(connection, xcb_free_gc_checked(connection, gc),
               "FreeGC failed") ||
      !checked(connection, xcb_free_pixmap_checked(connection, pixmap),
               "FreePixmap failed"))
    return fail(connection, "final drawing cleanup failed");

  printf("{\"completed\":true,\"width\":384,\"height\":256,"
         "\"put_image\":true,\"poly_fill_rectangle\":true,"
         "\"copy_area\":true,\"clear_area\":true,\"expose\":true}\n");
  xcb_disconnect(connection);
  return 0;
}
