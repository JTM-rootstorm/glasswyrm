#include <SDL.h>

#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct ProbeResult {
  bool initialized;
  bool x11_driver;
  bool display_mode;
  bool window_created;
  bool surface_updated;
  bool clipboard_round_trip;
  bool custom_cursor;
  bool fullscreen_entered;
  bool fullscreen_exited;
  bool geometry_restored;
  bool borderless;
  bool close_event;
  int display_count;
  int mode_width;
  int mode_height;
  int windowed_x;
  int windowed_y;
  int windowed_width;
  int windowed_height;
  char error[256];
} ProbeResult;

static void remember_error(ProbeResult *result, const char *operation) {
  if (result->error[0] != '\0') return;
  (void)snprintf(result->error, sizeof(result->error), "%s: %s", operation,
                 SDL_GetError());
}

static void pump_events(void) {
  SDL_Event event;
  SDL_PumpEvents();
  while (SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_FIRSTEVENT,
                        SDL_LASTEVENT) > 0) {
  }
}

static bool control_path(char *path, size_t size, const char *directory,
                         const char *name) {
  const int written = snprintf(path, size, "%s/%s", directory, name);
  return written > 0 && (size_t)written < size;
}

static bool write_marker(const char *directory, const char *name) {
  char path[512];
  if (!control_path(path, sizeof(path), directory, name)) return false;
  FILE *stream = fopen(path, "wx");
  if (!stream) return false;
  const bool wrote = fprintf(stream, "ready\n") == 6;
  return fclose(stream) == 0 && wrote;
}

static bool wait_for_marker(SDL_Window *window, const char *directory,
                            const char *name) {
  char path[512];
  if (!control_path(path, sizeof(path), directory, name)) return false;
  const uint32_t deadline = SDL_GetTicks() + 180000U;
  do {
    pump_events();
    (void)SDL_UpdateWindowSurface(window);
    if (access(path, F_OK) == 0) return true;
    if (errno != ENOENT) return false;
    SDL_Delay(10);
  } while ((int32_t)(SDL_GetTicks() - deadline) < 0);
  return false;
}

static bool wait_for_real_close(SDL_Window *window) {
  const uint32_t id = SDL_GetWindowID(window);
  const uint32_t deadline = SDL_GetTicks() + 180000U;
  do {
    SDL_Event event;
    (void)SDL_UpdateWindowSurface(window);
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_QUIT ||
          (event.type == SDL_WINDOWEVENT && event.window.windowID == id &&
           event.window.event == SDL_WINDOWEVENT_CLOSE))
        return true;
    }
    SDL_Delay(10);
  } while ((int32_t)(SDL_GetTicks() - deadline) < 0);
  return false;
}

static bool wait_for_fullscreen(SDL_Window *window, bool expected,
                                int width, int height) {
  const uint32_t deadline = SDL_GetTicks() + 3000U;
  do {
    int current_width = 0, current_height = 0;
    pump_events();
    SDL_GetWindowSize(window, &current_width, &current_height);
    const bool fullscreen =
        (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
    if (fullscreen == expected &&
        (!expected || (current_width == width && current_height == height)))
      return true;
    SDL_Delay(10);
  } while ((int32_t)(SDL_GetTicks() - deadline) < 0);
  return false;
}

static bool wait_for_geometry(SDL_Window *window, int x, int y, int width,
                              int height) {
  const uint32_t deadline = SDL_GetTicks() + 3000U;
  do {
    int current_x = 0, current_y = 0, current_width = 0, current_height = 0;
    pump_events();
    SDL_GetWindowPosition(window, &current_x, &current_y);
    SDL_GetWindowSize(window, &current_width, &current_height);
    if (current_x == x && current_y == y && current_width == width &&
        current_height == height)
      return true;
    SDL_Delay(10);
  } while ((int32_t)(SDL_GetTicks() - deadline) < 0);
  return false;
}

static void json_string(FILE *stream, const char *value) {
  fputc('"', stream);
  for (const unsigned char *cursor = (const unsigned char *)value; *cursor;
       ++cursor) {
    switch (*cursor) {
      case '"': fputs("\\\"", stream); break;
      case '\\': fputs("\\\\", stream); break;
      case '\n': fputs("\\n", stream); break;
      case '\r': fputs("\\r", stream); break;
      case '\t': fputs("\\t", stream); break;
      default:
        if (*cursor < 0x20U)
          (void)fprintf(stream, "\\u%04x", *cursor);
        else
          fputc(*cursor, stream);
    }
  }
  fputc('"', stream);
}

static bool write_result(const char *path, const ProbeResult *result) {
  FILE *stream = fopen(path, "w");
  if (!stream) return false;
  const bool passed =
      result->initialized && result->x11_driver && result->display_mode &&
      result->window_created && result->surface_updated &&
      result->clipboard_round_trip && result->custom_cursor &&
      result->fullscreen_entered && result->fullscreen_exited &&
      result->geometry_restored && result->borderless && result->close_event;
  fprintf(stream,
          "{\n  \"schema\": 1,\n  \"probe\": \"m12_sdl_probe\",\n"
          "  \"sdl_version\": \"%u.%u.%u\",\n"
          "  \"video_driver\": ",
          SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_PATCHLEVEL);
  json_string(stream, SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver()
                                                   : "");
  fprintf(stream,
          ",\n  \"display_count\": %d,\n"
          "  \"display_mode\": {\"width\": %d, \"height\": %d},\n"
          "  \"windowed_geometry\": {\"x\": %d, \"y\": %d, "
          "\"width\": %d, \"height\": %d},\n"
          "  \"checks\": {\n"
          "    \"initialized\": %s,\n    \"x11_driver\": %s,\n"
          "    \"display_mode\": %s,\n    \"window_created\": %s,\n"
          "    \"surface_updated\": %s,\n"
          "    \"clipboard_round_trip\": %s,\n"
          "    \"custom_cursor\": %s,\n"
          "    \"fullscreen_entered\": %s,\n"
          "    \"fullscreen_exited\": %s,\n"
          "    \"geometry_restored\": %s,\n"
          "    \"borderless\": %s,\n    \"close_event\": %s\n  },\n"
          "  \"passed\": %s,\n  \"error\": ",
          result->display_count, result->mode_width, result->mode_height,
          result->windowed_x, result->windowed_y, result->windowed_width,
          result->windowed_height, result->initialized ? "true" : "false",
          result->x11_driver ? "true" : "false",
          result->display_mode ? "true" : "false",
          result->window_created ? "true" : "false",
          result->surface_updated ? "true" : "false",
          result->clipboard_round_trip ? "true" : "false",
          result->custom_cursor ? "true" : "false",
          result->fullscreen_entered ? "true" : "false",
          result->fullscreen_exited ? "true" : "false",
          result->geometry_restored ? "true" : "false",
          result->borderless ? "true" : "false",
          result->close_event ? "true" : "false", passed ? "true" : "false");
  json_string(stream, result->error);
  fputs("\n}\n", stream);
  return fclose(stream) == 0;
}

int main(int argc, char **argv) {
  const char *output = NULL;
  const char *control = NULL;
  for (int index = 1; index < argc; ++index) {
    if (strcmp(argv[index], "--output") == 0 && index + 1 < argc)
      output = argv[++index];
    else if (strcmp(argv[index], "--control-dir") == 0 && index + 1 < argc)
      control = argv[++index];
    else {
      output = NULL;
      break;
    }
  }
  if (!output) {
    fprintf(stderr, "Usage: %s --output RESULT.json [--control-dir DIRECTORY]\n",
            argv[0]);
    return 2;
  }
  if (control && mkdir(control, 0700) != 0 && errno != EEXIST) {
    fprintf(stderr, "could not create SDL control directory: %s\n", control);
    return 2;
  }

  ProbeResult result = {0};
  SDL_Window *window = NULL;
  SDL_Cursor *cursor = NULL;
  if (SDL_SetHint(SDL_HINT_FRAMEBUFFER_ACCELERATION, "0") != SDL_TRUE)
    remember_error(&result, "SDL_SetHint");
  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    remember_error(&result, "SDL_Init");
    (void)write_result(output, &result);
    return 1;
  }
  result.initialized = true;
  result.x11_driver = SDL_GetCurrentVideoDriver() &&
                      strcmp(SDL_GetCurrentVideoDriver(), "x11") == 0;
  if (!result.x11_driver) remember_error(&result, "X11 video driver");
  result.display_count = SDL_GetNumVideoDisplays();
  SDL_DisplayMode mode = {0};
  if (result.display_count == 1 && SDL_GetCurrentDisplayMode(0, &mode) == 0 &&
      mode.w > 0 && mode.h > 0) {
    result.display_mode = true;
    result.mode_width = mode.w;
    result.mode_height = mode.h;
  } else {
    remember_error(&result, "SDL_GetCurrentDisplayMode");
  }

  window = SDL_CreateWindow("Glasswyrm M12 SDL Probe", 64, 64, 640, 480,
                            SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
  if (!window) {
    remember_error(&result, "SDL_CreateWindow");
    goto done;
  }
  result.window_created = true;
  SDL_GetWindowPosition(window, &result.windowed_x, &result.windowed_y);
  SDL_GetWindowSize(window, &result.windowed_width, &result.windowed_height);

  SDL_Surface *surface = SDL_GetWindowSurface(window);
  if (!surface) {
    remember_error(&result, "SDL_GetWindowSurface");
  } else {
    const SDL_Rect left = {0, 0, surface->w / 2, surface->h};
    const SDL_Rect right = {surface->w / 2, 0, surface->w - surface->w / 2,
                            surface->h};
    if (SDL_FillRect(surface, &left,
                     SDL_MapRGB(surface->format, 0x18, 0x36, 0x5a)) == 0 &&
        SDL_FillRect(surface, &right,
                     SDL_MapRGB(surface->format, 0xd4, 0x86, 0x32)) == 0) {
      const SDL_Rect dirty[2] = {{32, 24, 160, 120}, {448, 336, 160, 120}};
      result.surface_updated = SDL_UpdateWindowSurfaceRects(window, dirty, 2) == 0;
    }
    if (!result.surface_updated) remember_error(&result, "SDL_UpdateWindowSurfaceRects");
  }

  if (SDL_SetClipboardText("glasswyrm-m12-clipboard") == 0) {
    char *text = SDL_GetClipboardText();
    result.clipboard_round_trip =
        text && strcmp(text, "glasswyrm-m12-clipboard") == 0;
    SDL_free(text);
  }
  if (!result.clipboard_round_trip) remember_error(&result, "clipboard round trip");

  const uint8_t cursor_data[8] = {0x18, 0x3c, 0x7e, 0xff,
                                  0xff, 0x7e, 0x3c, 0x18};
  const uint8_t cursor_mask[8] = {0x18, 0x3c, 0x7e, 0xff,
                                  0xff, 0x7e, 0x3c, 0x18};
  cursor = SDL_CreateCursor(cursor_data, cursor_mask, 8, 8, 3, 3);
  if (cursor) {
    SDL_SetCursor(cursor);
    result.custom_cursor = true;
  } else {
    remember_error(&result, "SDL_CreateCursor");
  }

  if (control && (!write_marker(control, "windowed-ready") ||
                  !wait_for_marker(window, control, "enter-fullscreen"))) {
    remember_error(&result, "windowed control handshake");
    goto done;
  }

  if (result.display_mode && SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP) == 0)
    result.fullscreen_entered = wait_for_fullscreen(
        window, true, result.mode_width, result.mode_height);
  if (!result.fullscreen_entered) remember_error(&result, "fullscreen desktop enter");
  if (control && (!write_marker(control, "fullscreen-ready") ||
                  !wait_for_marker(window, control, "exit-fullscreen"))) {
    remember_error(&result, "fullscreen control handshake");
    goto done;
  }
  if (SDL_SetWindowFullscreen(window, 0) == 0) {
    result.fullscreen_exited = wait_for_fullscreen(window, false, 0, 0);
    SDL_SetWindowPosition(window, result.windowed_x, result.windowed_y);
    SDL_SetWindowSize(window, result.windowed_width, result.windowed_height);
    result.geometry_restored = wait_for_geometry(
        window, result.windowed_x, result.windowed_y, result.windowed_width,
        result.windowed_height);
  }
  if (!result.fullscreen_exited) remember_error(&result, "fullscreen desktop exit");
  if (!result.geometry_restored) remember_error(&result, "window geometry restore");

  SDL_SetWindowBordered(window, SDL_FALSE);
  pump_events();
  result.borderless = (SDL_GetWindowFlags(window) & SDL_WINDOW_BORDERLESS) != 0 &&
                      wait_for_geometry(window, result.windowed_x,
                                        result.windowed_y,
                                        result.windowed_width,
                                        result.windowed_height);
  if (!result.borderless) remember_error(&result, "borderless window");

  if (control) {
    SDL_RaiseWindow(window);
    pump_events();
    if (write_marker(control, "borderless-ready"))
      result.close_event = wait_for_real_close(window);
  } else {
    SDL_Event close_event;
    SDL_zero(close_event);
    close_event.type = SDL_WINDOWEVENT;
    close_event.window.type = SDL_WINDOWEVENT;
    close_event.window.windowID = SDL_GetWindowID(window);
    close_event.window.event = SDL_WINDOWEVENT_CLOSE;
    if (SDL_PushEvent(&close_event) == 1) {
      SDL_Event received;
      while (SDL_PollEvent(&received))
        if (received.type == SDL_WINDOWEVENT &&
            received.window.windowID == close_event.window.windowID &&
            received.window.event == SDL_WINDOWEVENT_CLOSE)
          result.close_event = true;
    }
  }
  if (!result.close_event) remember_error(&result, "close event");

done:
  if (cursor) SDL_FreeCursor(cursor);
  if (window) SDL_DestroyWindow(window);
  if (!write_result(output, &result)) {
    fprintf(stderr, "could not write SDL result JSON: %s\n", output);
    SDL_Quit();
    return 1;
  }
  SDL_Quit();
  return result.initialized && result.x11_driver && result.display_mode &&
                 result.window_created && result.surface_updated &&
                 result.clipboard_round_trip && result.custom_cursor &&
                 result.fullscreen_entered && result.fullscreen_exited &&
                 result.geometry_restored && result.borderless &&
                 result.close_event
             ? 0
             : 1;
}
