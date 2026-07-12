#define _GNU_SOURCE
#include <xcb/xcb.h>

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static void fail(const char *message) {
  fprintf(stderr, "m6_restart_hold_probe: %s\n", message);
  exit(1);
}

static void check_cookie(xcb_connection_t *connection,
                         xcb_void_cookie_t cookie, const char *message) {
  xcb_generic_error_t *error = xcb_request_check(connection, cookie);
  if (error != NULL) {
    free(error);
    fail(message);
  }
}

static void write_file_at(int directory, const char *name, const char *value,
                          int flags) {
  const int descriptor = openat(directory, name,
                                O_WRONLY | O_CLOEXEC | O_NOFOLLOW | flags,
                                0600);
  if (descriptor < 0)
    fail("cannot create control marker");
  const size_t size = strlen(value);
  if (write(descriptor, value, size) != (ssize_t)size || close(descriptor) != 0)
    fail("cannot write control marker");
}

static bool tree_contains(const xcb_query_tree_reply_t *reply,
                          xcb_window_t window) {
  const int count = xcb_query_tree_children_length(reply);
  const xcb_window_t *children = xcb_query_tree_children(reply);
  for (int index = 0; index < count; ++index)
    if (children[index] == window)
      return true;
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
  if (control < 0)
    fail("control directory must be an existing real directory");

  int screen_number = 0;
  xcb_connection_t *connection = xcb_connect(display, &screen_number);
  if (connection == NULL || xcb_connection_has_error(connection) != 0)
    fail("cannot connect to display");
  const xcb_setup_t *setup = xcb_get_setup(connection);
  xcb_screen_iterator_t screens = xcb_setup_roots_iterator(setup);
  for (int index = 0; index < screen_number && screens.rem != 0; ++index)
    xcb_screen_next(&screens);
  if (screens.rem == 0)
    fail("display screen is unavailable");
  xcb_screen_t *screen = screens.data;
  const xcb_window_t window = xcb_generate_id(connection);
  check_cookie(connection,
               xcb_create_window_checked(
                   connection, XCB_COPY_FROM_PARENT, window, screen->root, 32,
                   24, 320, 200, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT,
                   screen->root_visual, 0, NULL),
               "CreateWindow failed");
  check_cookie(connection, xcb_map_window_checked(connection, window),
               "MapWindow failed");
  const uint32_t initial_geometry[] = {64, 48, 320, 200};
  check_cookie(connection,
               xcb_configure_window_checked(
                   connection, window,
                   XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y |
                       XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT,
                   initial_geometry),
               "initial ConfigureWindow failed");
  if (xcb_flush(connection) <= 0)
    fail("initial flush failed");

  xcb_generic_error_t *error = NULL;
  xcb_get_input_focus_reply_t *focus_before = xcb_get_input_focus_reply(
      connection, xcb_get_input_focus(connection), &error);
  if (error != NULL || focus_before == NULL)
    fail("initial focus query failed");
  const xcb_window_t original_focus = focus_before->focus;
  free(focus_before);
  write_file_at(control, "ready", "ready\n", O_CREAT | O_EXCL);

  const time_t deadline = time(NULL) + 30;
  while (faccessat(control, "continue", F_OK, 0) != 0) {
    if (errno != ENOENT)
      fail("cannot inspect continue marker");
    if (time(NULL) >= deadline)
      fail("timed out waiting for continue marker");
    const struct timespec pause = {0, 50000000};
    nanosleep(&pause, NULL);
  }
  if (xcb_connection_has_error(connection) != 0)
    fail("XCB connection did not survive restart");

  xcb_get_window_attributes_reply_t *attributes =
      xcb_get_window_attributes_reply(
          connection, xcb_get_window_attributes(connection, window), &error);
  if (error != NULL || attributes == NULL)
    fail("window attributes were not preserved");
  if (attributes->map_state != XCB_MAP_STATE_VIEWABLE)
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
  xcb_get_input_focus_reply_t *focus_after = xcb_get_input_focus_reply(
      connection, xcb_get_input_focus(connection), &error);
  if (error != NULL || focus_after == NULL ||
      focus_after->focus != original_focus)
    fail("focus was not preserved");
  free(focus_after);

  const uint32_t final_geometry[] = {96, 80, 400, 240};
  check_cookie(connection,
               xcb_configure_window_checked(
                   connection, window,
                   XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y |
                       XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT,
                   final_geometry),
               "post-restart ConfigureWindow failed");
  check_cookie(connection, xcb_unmap_window_checked(connection, window),
               "post-restart UnmapWindow failed");
  check_cookie(connection, xcb_destroy_window_checked(connection, window),
               "post-restart DestroyWindow failed");
  if (xcb_flush(connection) <= 0 || xcb_connection_has_error(connection) != 0)
    fail("post-restart lifecycle flush failed");
  xcb_disconnect(connection);
  write_file_at(control, "result.json",
                "{\"completed\":true,\"connection_preserved\":true,"
                "\"focus_preserved\":true,\"geometry_preserved\":true,"
                "\"tree_preserved\":true,\"window_preserved\":true}\n",
                O_CREAT | O_TRUNC);
  close(control);
  return 0;
}
