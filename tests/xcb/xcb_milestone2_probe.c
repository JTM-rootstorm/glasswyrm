#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xcb/xcb.h>

static int fail(xcb_connection_t *connection, const char *message) {
  fprintf(stderr, "xcb_milestone2_probe: %s\n", message);
  xcb_disconnect(connection);
  return 1;
}

int main(void) {
  int screen_index = 0;
  xcb_connection_t *connection = xcb_connect(NULL, &screen_index);
  if (xcb_connection_has_error(connection)) {
    return fail(connection, "connection setup failed");
  }
  const xcb_setup_t *setup = xcb_get_setup(connection);
  xcb_screen_iterator_t screens = xcb_setup_roots_iterator(setup);
  while (screen_index-- > 0) xcb_screen_next(&screens);
  if (screens.rem == 0) return fail(connection, "setup has no screen");
  xcb_screen_t *screen = screens.data;
  const xcb_window_t window = xcb_generate_id(connection);
  xcb_void_cookie_t create = xcb_create_window_checked(
      connection, XCB_COPY_FROM_PARENT, window, screen->root, 1, 2, 80, 60, 0,
      XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual, 0, NULL);
  xcb_generic_error_t *error = xcb_request_check(connection, create);
  if (error != NULL) {
    free(error);
    return fail(connection, "checked CreateWindow failed");
  }

  xcb_get_geometry_reply_t *geometry =
      xcb_get_geometry_reply(connection, xcb_get_geometry(connection, window),
                             &error);
  if (geometry == NULL || error != NULL || geometry->width != 80 ||
      geometry->height != 60) {
    free(geometry);
    free(error);
    return fail(connection, "GetGeometry mismatch");
  }
  free(geometry);

  xcb_query_tree_reply_t *tree = xcb_query_tree_reply(
      connection, xcb_query_tree(connection, screen->root), &error);
  int found = 0;
  if (tree != NULL) {
    const xcb_window_t *children = xcb_query_tree_children(tree);
    for (int index = 0; index < xcb_query_tree_children_length(tree); ++index) {
      if (children[index] == window) found = 1;
    }
  }
  if (tree == NULL || error != NULL || !found) {
    free(tree);
    free(error);
    return fail(connection, "QueryTree did not contain child");
  }
  free(tree);

  static const char atom_name[] = "GW_M2_XCB";
  xcb_intern_atom_reply_t *intern = xcb_intern_atom_reply(
      connection,
      xcb_intern_atom(connection, 0, sizeof(atom_name) - 1, atom_name), &error);
  if (intern == NULL || error != NULL || intern->atom == XCB_ATOM_NONE) {
    free(intern);
    free(error);
    return fail(connection, "InternAtom failed");
  }
  const xcb_atom_t atom = intern->atom;
  free(intern);

  xcb_get_atom_name_reply_t *name_reply = xcb_get_atom_name_reply(
      connection, xcb_get_atom_name(connection, atom), &error);
  if (name_reply == NULL || error != NULL ||
      xcb_get_atom_name_name_length(name_reply) != (int)(sizeof(atom_name) - 1) ||
      memcmp(xcb_get_atom_name_name(name_reply), atom_name,
             sizeof(atom_name) - 1) != 0) {
    free(name_reply);
    free(error);
    return fail(connection, "GetAtomName mismatch");
  }
  free(name_reply);

  static const char value[] = "glasswyrm-m2";
  xcb_void_cookie_t change = xcb_change_property_checked(
      connection, XCB_PROP_MODE_REPLACE, window, atom, XCB_ATOM_STRING, 8,
      sizeof(value) - 1, value);
  error = xcb_request_check(connection, change);
  if (error != NULL) {
    free(error);
    return fail(connection, "checked ChangeProperty failed");
  }
  xcb_get_property_reply_t *property = xcb_get_property_reply(
      connection,
      xcb_get_property(connection, 0, window, atom, XCB_ATOM_STRING, 0, 64),
      &error);
  if (property == NULL || error != NULL || property->format != 8 ||
      xcb_get_property_value_length(property) != (int)(sizeof(value) - 1) ||
      memcmp(xcb_get_property_value(property), value, sizeof(value) - 1) != 0) {
    free(property);
    free(error);
    return fail(connection, "GetProperty mismatch");
  }
  free(property);

  xcb_list_properties_reply_t *properties = xcb_list_properties_reply(
      connection, xcb_list_properties(connection, window), &error);
  found = 0;
  if (properties != NULL) {
    const xcb_atom_t *atoms = xcb_list_properties_atoms(properties);
    for (int index = 0; index < xcb_list_properties_atoms_length(properties);
         ++index) {
      if (atoms[index] == atom) found = 1;
    }
  }
  if (properties == NULL || error != NULL || !found) {
    free(properties);
    free(error);
    return fail(connection, "ListProperties did not contain atom");
  }
  free(properties);

  error = xcb_request_check(
      connection, xcb_delete_property_checked(connection, window, atom));
  if (error != NULL) {
    free(error);
    return fail(connection, "checked DeleteProperty failed");
  }
  property = xcb_get_property_reply(
      connection, xcb_get_property(connection, 0, window, atom, 0, 0, 64),
      &error);
  if (property == NULL || error != NULL || property->type != XCB_ATOM_NONE) {
    free(property);
    free(error);
    return fail(connection, "deleted property remained present");
  }
  free(property);

  error = xcb_request_check(connection, xcb_destroy_window_checked(connection,
                                                                    window));
  if (error != NULL) {
    free(error);
    return fail(connection, "checked DestroyWindow failed");
  }
  tree = xcb_query_tree_reply(connection, xcb_query_tree(connection, screen->root),
                              &error);
  found = 0;
  if (tree != NULL) {
    const xcb_window_t *children = xcb_query_tree_children(tree);
    for (int index = 0; index < xcb_query_tree_children_length(tree); ++index) {
      if (children[index] == window) found = 1;
    }
  }
  if (tree == NULL || error != NULL || found) {
    free(tree);
    free(error);
    return fail(connection, "destroyed child remained in QueryTree");
  }
  free(tree);

  geometry = xcb_get_geometry_reply(
      connection, xcb_get_geometry(connection, window), &error);
  if (geometry != NULL || error == NULL || error->error_code != XCB_DRAWABLE) {
    free(geometry);
    free(error);
    return fail(connection, "destroyed drawable did not return BadDrawable");
  }
  free(error);
  printf("xcb milestone2 probe passed: root=0x%08x atom=%u\n", screen->root,
         atom);
  xcb_disconnect(connection);
  return 0;
}
