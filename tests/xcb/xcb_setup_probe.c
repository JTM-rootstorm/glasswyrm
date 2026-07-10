#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xcb/xcb.h>

int main(void) {
  int screen_number = 0;
  xcb_connection_t* connection = xcb_connect(NULL, &screen_number);
  const int error = xcb_connection_has_error(connection);
  if (error != 0) {
    fprintf(stderr, "xcb_connect failed with error %d\n", error);
    xcb_disconnect(connection);
    return 1;
  }
  const xcb_setup_t* setup = xcb_get_setup(connection);
  const int vendor_length = xcb_setup_vendor_length(setup);
  const char* vendor = (const char*)xcb_setup_vendor(setup);
  if (xcb_setup_roots_length(setup) != 1 || vendor_length < 9 ||
      memcmp(vendor, "Glasswyrm", 9) != 0 ||
      setup->resource_id_mask != 0x001fffffU ||
      setup->maximum_request_length == 0) {
    fprintf(stderr, "unexpected Glasswyrm setup record\n");
    xcb_disconnect(connection);
    return 1;
  }
  printf("libxcb setup complete: vendor=%.*s roots=1 resource-mask=0x%08x\n",
         vendor_length, vendor, setup->resource_id_mask);
  xcb_disconnect(connection);
  return 0;
}
