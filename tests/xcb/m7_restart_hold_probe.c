#define _GNU_SOURCE
#include <xcb/xcb.h>

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static void fail(const char *message) {
  fprintf(stderr, "m7_restart_hold_probe: %s\n", message);
  exit(1);
}

static void check(xcb_connection_t *connection, xcb_void_cookie_t cookie,
                  const char *message) {
  xcb_generic_error_t *error = xcb_request_check(connection, cookie);
  if (error != NULL) {
    free(error);
    fail(message);
  }
}

static void marker(int directory, const char *name, const char *contents) {
  const int descriptor = openat(directory, name,
                                O_WRONLY | O_CLOEXEC | O_NOFOLLOW | O_CREAT |
                                    O_TRUNC,
                                0600);
  if (descriptor < 0) fail("cannot create control marker");
  const size_t size = strlen(contents);
  if (write(descriptor, contents, size) != (ssize_t)size ||
      close(descriptor) != 0)
    fail("cannot write control marker");
}

static bool tree_contains(const xcb_query_tree_reply_t *reply,
                          xcb_window_t window) {
  const xcb_window_t *children = xcb_query_tree_children(reply);
  for (int index = 0; index < xcb_query_tree_children_length(reply); ++index)
    if (children[index] == window) return true;
  return false;
}

int main(int argc, char **argv) {
  const char *display = NULL;
  const char *control_path = NULL;
  for (int index = 1; index < argc; ++index) {
    if (strcmp(argv[index], "--display") == 0 && index + 1 < argc)
      display = argv[++index];
    else if (strcmp(argv[index], "--control-dir") == 0 && index + 1 < argc)
      control_path = argv[++index];
    else
      fail("usage: --display :N --control-dir PATH");
  }
  if (display == NULL || control_path == NULL)
    fail("usage: --display :N --control-dir PATH");
  const int control = open(control_path, O_RDONLY | O_CLOEXEC | O_DIRECTORY |
                                             O_NOFOLLOW);
  if (control < 0) fail("control directory must be an existing real directory");

  int screen_index = 0;
  xcb_connection_t *connection = xcb_connect(display, &screen_index);
  if (connection == NULL || xcb_connection_has_error(connection) != 0)
    fail("cannot connect to display");
  xcb_screen_iterator_t screens = xcb_setup_roots_iterator(xcb_get_setup(connection));
  while (screen_index-- > 0) xcb_screen_next(&screens);
  if (screens.rem == 0) fail("display screen is unavailable");
  xcb_screen_t *screen = screens.data;

  const xcb_window_t window = xcb_generate_id(connection);
  const xcb_pixmap_t pixmap = xcb_generate_id(connection);
  const xcb_gcontext_t gc = xcb_generate_id(connection);
  const uint32_t window_values[] = {
      0x00102038U, XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_EXPOSURE};
  check(connection,
        xcb_create_window_checked(
            connection, XCB_COPY_FROM_PARENT, window, screen->root, 64, 48,
            320, 200, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual,
            XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK, window_values),
        "CreateWindow failed");
  check(connection,
        xcb_create_pixmap_checked(connection, 24, pixmap, screen->root, 32,
                                  32),
        "CreatePixmap failed");
  const uint32_t foreground = 0x00d94c4cU;
  check(connection,
        xcb_create_gc_checked(connection, gc, window, XCB_GC_FOREGROUND,
                              &foreground),
        "CreateGC failed");
  const xcb_rectangle_t initial[] = {{16, 16, 96, 64}, {152, 48, 80, 96}};
  check(connection,
        xcb_poly_fill_rectangle_checked(connection, window, gc, 2, initial),
        "initial drawing failed");
  check(connection, xcb_map_window_checked(connection, window),
        "MapWindow failed");
  check(connection,
        xcb_copy_area_checked(connection, window, pixmap, gc, 16, 16, 0, 0,
                              32, 32),
        "initial pixmap copy failed");
  if (xcb_flush(connection) <= 0) fail("initial flush failed");

  xcb_generic_error_t *error = NULL;
  xcb_get_input_focus_reply_t *focus = xcb_get_input_focus_reply(
      connection, xcb_get_input_focus(connection), &error);
  if (error != NULL || focus == NULL) fail("initial focus query failed");
  const xcb_window_t original_focus = focus->focus;
  free(focus);
  marker(control, "ready", "ready\n");

  const time_t deadline = time(NULL) + 30;
  while (faccessat(control, "continue", F_OK, 0) != 0) {
    if (errno != ENOENT) fail("cannot inspect continue marker");
    if (time(NULL) >= deadline) fail("timed out waiting for continue marker");
    const struct timespec pause = {0, 50000000};
    nanosleep(&pause, NULL);
  }
  if (xcb_connection_has_error(connection) != 0)
    fail("XCB connection did not survive restart");

  xcb_get_window_attributes_reply_t *attributes =
      xcb_get_window_attributes_reply(
          connection, xcb_get_window_attributes(connection, window), &error);
  if (error != NULL || attributes == NULL ||
      attributes->map_state != XCB_MAP_STATE_VIEWABLE)
    fail("window mapping was not preserved");
  free(attributes);
  xcb_get_geometry_reply_t *geometry = xcb_get_geometry_reply(
      connection, xcb_get_geometry(connection, window), &error);
  if (error != NULL || geometry == NULL || geometry->x != 64 ||
      geometry->y != 48 || geometry->width != 320 || geometry->height != 200)
    fail("window geometry was not preserved");
  free(geometry);
  xcb_query_tree_reply_t *tree = xcb_query_tree_reply(
      connection, xcb_query_tree(connection, screen->root), &error);
  if (error != NULL || tree == NULL || !tree_contains(tree, window))
    fail("window tree was not preserved");
  free(tree);
  focus = xcb_get_input_focus_reply(connection, xcb_get_input_focus(connection),
                                    &error);
  if (error != NULL || focus == NULL || focus->focus != original_focus)
    fail("focus was not preserved");
  free(focus);

  const uint32_t masked_values[] = {0x00ff0000U, 0x00123456U};
  check(connection,
        xcb_change_gc_checked(connection, gc,
                              XCB_GC_PLANE_MASK | XCB_GC_FOREGROUND,
                              masked_values),
        "post-restart plane-mask update failed");
  const xcb_rectangle_t post_restart = {248, 112, 48, 64};
  check(connection,
        xcb_poly_fill_rectangle_checked(connection, window, gc, 1,
                                        &post_restart),
        "post-restart drawing failed");
  check(connection,
        xcb_copy_area_checked(connection, pixmap, window, gc, 0, 0, 264, 16,
                              32, 32),
        "post-restart pixmap use failed");
  if (xcb_flush(connection) <= 0 || xcb_connection_has_error(connection) != 0)
    fail("post-restart drawing flush failed");

  marker(control, "result.json",
         "{\"completed\":true,\"connection_preserved\":true,"
         "\"focus_preserved\":true,\"geometry_preserved\":true,"
         "\"resources_preserved\":true,\"tree_preserved\":true,"
         "\"window_preserved\":true,\"plane_mask\":true,"
         "\"post_restart_drawing\":true}\n");
  xcb_disconnect(connection);
  close(control);
  return 0;
}
