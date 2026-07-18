#include "m12_xcb_probe.h"

#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <xcb/composite.h>
#include <xcb/render.h>
#include <xcb/shm.h>

static const uint32_t kFormatArgb32 = 0x1ffff104U;

typedef struct SharedSegment {
  int shmid;
  uint8_t *pixels;
  xcb_shm_seg_t xid;
} SharedSegment;

static bool open_segment(xcb_connection_t *connection, SharedSegment *segment,
                         Probe *probe) {
  *segment = (SharedSegment){.shmid = -1};
  segment->shmid = shmget(IPC_PRIVATE, 64, IPC_CREAT | 0600);
  if (segment->shmid < 0) {
    probe_fail(probe, "shmget failed");
    return false;
  }
  void *mapping = shmat(segment->shmid, NULL, 0);
  if (mapping == (void *)-1) {
    probe_fail(probe, "shmat failed");
    (void)shmctl(segment->shmid, IPC_RMID, NULL);
    segment->shmid = -1;
    return false;
  }
  segment->pixels = mapping;
  segment->xid = xcb_generate_id(connection);
  if (!probe_checked(connection,
                     xcb_shm_attach_checked(connection, segment->xid,
                                            (uint32_t)segment->shmid, 0),
                     probe, "MIT-SHM Attach")) {
    (void)shmdt(mapping);
    (void)shmctl(segment->shmid, IPC_RMID, NULL);
    *segment = (SharedSegment){.shmid = -1};
    return false;
  }
  return true;
}

static void close_segment(xcb_connection_t *connection, SharedSegment *segment,
                          Probe *probe) {
  if (segment->xid != XCB_NONE)
    (void)probe_checked(connection,
                        xcb_shm_detach_checked(connection, segment->xid), probe,
                        "MIT-SHM Detach");
  if (segment->pixels)
    (void)shmdt(segment->pixels);
  if (segment->shmid >= 0)
    (void)shmctl(segment->shmid, IPC_RMID, NULL);
  *segment = (SharedSegment){.shmid = -1};
}

static bool read_pixel(xcb_connection_t *connection, xcb_drawable_t drawable,
                       SharedSegment *segment, uint32_t *pixel, Probe *probe) {
  memset(segment->pixels, 0, 64);
  xcb_generic_error_t *error = NULL;
  xcb_shm_get_image_reply_t *reply = xcb_shm_get_image_reply(
      connection,
      xcb_shm_get_image(connection, drawable, 0, 0, 1, 1, UINT32_MAX,
                        XCB_IMAGE_FORMAT_Z_PIXMAP, segment->xid, 0),
      &error);
  const bool passed = reply && !error && reply->size == 4;
  if (passed)
    memcpy(pixel, segment->pixels, sizeof(*pixel));
  if (!passed)
    probe_fail(probe, "MIT-SHM GetImage failed");
  free(error);
  free(reply);
  return passed;
}

bool probe_shm_transport(xcb_connection_t *connection, xcb_screen_t *screen,
                         Probe *probe) {
  SharedSegment segment;
  if (!open_segment(connection, &segment, probe))
    return false;
  const xcb_pixmap_t pixmap = xcb_generate_id(connection);
  const xcb_gcontext_t gc = xcb_generate_id(connection);
  bool passed =
      probe_checked(connection,
                    xcb_create_pixmap_checked(connection, screen->root_depth,
                                              pixmap, screen->root, 2, 2),
                    probe, "CreatePixmap for MIT-SHM") &&
      probe_checked(connection,
                    xcb_create_gc_checked(connection, gc, pixmap, 0, NULL),
                    probe, "CreateGC for MIT-SHM");
  const uint32_t expected[] = {0x00112233U, 0x00445566U, 0x00778899U,
                               0x00aabbccU};
  memcpy(segment.pixels, expected, sizeof(expected));
  passed =
      passed && probe_checked(connection,
                              xcb_shm_put_image_checked(
                                  connection, pixmap, gc, 2, 2, 0, 0, 2, 2, 0,
                                  0, screen->root_depth,
                                  XCB_IMAGE_FORMAT_Z_PIXMAP, 0, segment.xid, 0),
                              probe, "MIT-SHM PutImage");
  memset(segment.pixels, 0, 64);
  xcb_generic_error_t *error = NULL;
  xcb_shm_get_image_reply_t *reply = xcb_shm_get_image_reply(
      connection,
      xcb_shm_get_image(connection, pixmap, 0, 0, 2, 2, UINT32_MAX,
                        XCB_IMAGE_FORMAT_Z_PIXMAP, segment.xid, 0),
      &error);
  passed = passed && reply && !error && reply->size == sizeof(expected) &&
           memcmp(segment.pixels, expected, sizeof(expected)) == 0;
  if (!passed)
    probe_fail(probe, "MIT-SHM transport round trip failed");
  free(error);
  free(reply);
  (void)probe_checked(connection, xcb_free_gc_checked(connection, gc), probe,
                      "FreeGC after MIT-SHM");
  (void)probe_checked(connection, xcb_free_pixmap_checked(connection, pixmap),
                      probe, "FreePixmap after MIT-SHM");
  close_segment(connection, &segment, probe);
  return passed;
}

static bool exact_formats(xcb_connection_t *connection, Probe *probe) {
  xcb_generic_error_t *error = NULL;
  xcb_render_query_pict_formats_reply_t *reply =
      xcb_render_query_pict_formats_reply(
          connection, xcb_render_query_pict_formats(connection), &error);
  static const xcb_render_pictforminfo_t expected[] = {
      {.id = 0x1ffff101U,
       .type = XCB_RENDER_PICT_TYPE_DIRECT,
       .depth = 1,
       .direct = {.alpha_mask = 0x0001}},
      {.id = 0x1ffff102U,
       .type = XCB_RENDER_PICT_TYPE_DIRECT,
       .depth = 8,
       .direct = {.alpha_mask = 0x00ff}},
      {.id = 0x1ffff103U,
       .type = XCB_RENDER_PICT_TYPE_DIRECT,
       .depth = 24,
       .direct = {.red_shift = 16,
                  .red_mask = 0x00ff,
                  .green_shift = 8,
                  .green_mask = 0x00ff,
                  .blue_mask = 0x00ff}},
      {.id = 0x1ffff104U,
       .type = XCB_RENDER_PICT_TYPE_DIRECT,
       .depth = 32,
       .direct = {.red_shift = 16,
                  .red_mask = 0x00ff,
                  .green_shift = 8,
                  .green_mask = 0x00ff,
                  .blue_mask = 0x00ff,
                  .alpha_shift = 24,
                  .alpha_mask = 0x00ff}},
  };
  bool passed = reply && !error && reply->num_formats == 4 &&
                xcb_render_query_pict_formats_formats_length(reply) == 4;
  if (passed) {
    const xcb_render_pictforminfo_t *formats =
        xcb_render_query_pict_formats_formats(reply);
    for (size_t index = 0; index < 4; ++index) {
      passed = passed && formats[index].id == expected[index].id &&
               formats[index].type == expected[index].type &&
               formats[index].depth == expected[index].depth &&
               memcmp(&formats[index].direct, &expected[index].direct,
                      sizeof(formats[index].direct)) == 0 &&
               formats[index].colormap == XCB_NONE;
    }
  }
  if (!passed)
    probe_fail(probe, "RENDER QueryPictFormats mismatch");
  free(error);
  free(reply);
  return passed;
}

bool probe_render_pixels(xcb_connection_t *connection, xcb_screen_t *screen,
                         Probe *probe) {
  probe->render_formats = exact_formats(connection, probe);
  SharedSegment segment;
  if (!probe->render_formats || !open_segment(connection, &segment, probe))
    return false;
  const xcb_pixmap_t pixmap = xcb_generate_id(connection);
  const xcb_render_picture_t destination = xcb_generate_id(connection);
  const xcb_render_picture_t solid = xcb_generate_id(connection);
  bool passed = probe_checked(connection,
                              xcb_create_pixmap_checked(connection, 32, pixmap,
                                                        screen->root, 2, 2),
                              probe, "Create depth-32 RENDER pixmap") &&
                probe_checked(connection,
                              xcb_render_create_picture_checked(
                                  connection, destination, pixmap,
                                  kFormatArgb32, 0, NULL),
                              probe, "RENDER CreatePicture");
  const xcb_render_color_t initial = {0x0a0a, 0x1414, 0x1e1e, 0x4040};
  const xcb_rectangle_t rectangle = {0, 0, 1, 1};
  passed = passed && probe_checked(connection,
                                   xcb_render_fill_rectangles_checked(
                                       connection, XCB_RENDER_PICT_OP_SRC,
                                       destination, initial, 1, &rectangle),
                                   probe, "RENDER initial Src fill");
  const xcb_render_color_t source = {0x4040, 0x2020, 0x1010, 0x8080};
  passed = passed &&
           probe_checked(
               connection,
               xcb_render_create_solid_fill_checked(connection, solid, source),
               probe, "RENDER CreateSolidFill") &&
           probe_checked(connection,
                         xcb_render_composite_checked(
                             connection, XCB_RENDER_PICT_OP_OVER, solid,
                             XCB_RENDER_PICTURE_NONE, destination, 0, 0, 0, 0,
                             0, 0, 1, 1),
                         probe, "RENDER Composite Over");
  uint32_t pixel = 0;
  passed = passed && read_pixel(connection, pixmap, &segment, &pixel, probe);
  probe->render_pixel = pixel;
  /* MIT-SHM GetImage exports XRGB, so compare the exact composited RGB. */
  probe->render_exact_pixels = passed && pixel == 0x00452a1fU;
  if (!probe->render_exact_pixels)
    probe_fail(probe, "RENDER exact Over pixel mismatch");
  (void)probe_checked(connection,
                      xcb_render_free_picture_checked(connection, solid), probe,
                      "RENDER FreePicture solid");
  (void)probe_checked(connection,
                      xcb_render_free_picture_checked(connection, destination),
                      probe, "RENDER FreePicture destination");
  (void)probe_checked(connection, xcb_free_pixmap_checked(connection, pixmap),
                      probe, "Free RENDER pixmap");
  close_segment(connection, &segment, probe);
  return probe->render_formats && probe->render_exact_pixels;
}

bool probe_composite_lifetime(xcb_connection_t *connection,
                              xcb_screen_t *screen, Probe *probe) {
  SharedSegment segment;
  if (!open_segment(connection, &segment, probe))
    return false;
  const xcb_window_t window = xcb_generate_id(connection);
  const xcb_gcontext_t gc = xcb_generate_id(connection);
  const xcb_pixmap_t named = xcb_generate_id(connection);
  bool passed =
      probe_checked(connection,
                    xcb_create_window_checked(connection, screen->root_depth,
                                              window, screen->root, 0, 0, 2, 2,
                                              0, XCB_WINDOW_CLASS_INPUT_OUTPUT,
                                              screen->root_visual, 0, NULL),
                    probe, "Create COMPOSITE window") &&
      probe_checked(connection,
                    xcb_create_gc_checked(connection, gc, window, 0, NULL),
                    probe, "Create COMPOSITE window GC");
  const uint32_t pixels[] = {0x00112233U, 0x00112233U, 0x00112233U,
                             0x00112233U};
  passed =
      passed &&
      probe_checked(connection,
                    xcb_put_image_checked(connection, XCB_IMAGE_FORMAT_Z_PIXMAP,
                                          window, gc, 2, 2, 0, 0, 0,
                                          screen->root_depth, sizeof(pixels),
                                          (const uint8_t *)pixels),
                    probe, "Paint COMPOSITE window") &&
      probe_checked(
          connection,
          xcb_composite_redirect_subwindows_checked(
              connection, screen->root, XCB_COMPOSITE_REDIRECT_MANUAL),
          probe, "COMPOSITE RedirectSubwindows") &&
      probe_checked(
          connection,
          xcb_composite_name_window_pixmap_checked(connection, window, named),
          probe, "COMPOSITE NameWindowPixmap") &&
      probe_checked(
          connection,
          xcb_composite_unredirect_subwindows_checked(
              connection, screen->root, XCB_COMPOSITE_REDIRECT_MANUAL),
          probe, "COMPOSITE UnredirectSubwindows");
  uint32_t named_pixel = 0;
  passed =
      passed && read_pixel(connection, named, &segment, &named_pixel, probe);
  probe->named_pixel = named_pixel;
  probe->composite_named_lifetime = passed && named_pixel == pixels[0];
  if (!probe->composite_named_lifetime)
    probe_fail(probe, "COMPOSITE named pixmap did not survive unredirect");
  (void)probe_checked(connection, xcb_free_pixmap_checked(connection, named),
                      probe, "Free named COMPOSITE pixmap");
  (void)probe_checked(connection, xcb_free_gc_checked(connection, gc), probe,
                      "Free COMPOSITE GC");
  (void)probe_checked(connection,
                      xcb_destroy_window_checked(connection, window), probe,
                      "Destroy COMPOSITE window");
  close_segment(connection, &segment, probe);
  return probe->composite_named_lifetime;
}
