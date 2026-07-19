#include <SDL.h>

#include <stdio.h>
#include <string.h>

static void json_string(FILE *output, const char *value) {
  fputc('"', output);
  for (const unsigned char *cursor = (const unsigned char *)value; *cursor;
       ++cursor) {
    if (*cursor == '"' || *cursor == '\\') fputc('\\', output);
    if (*cursor >= 0x20 && *cursor < 0x7f) fputc(*cursor, output);
  }
  fputc('"', output);
}

int main(int argc, char **argv) {
  const char *path = NULL;
  if (argc == 3 && strcmp(argv[1], "--output") == 0) path = argv[2];
  if (!path) {
    fprintf(stderr, "Usage: %s --output RESULT.json\n", argv[0]);
    return 2;
  }
  FILE *output = fopen(path, "w");
  if (!output) return 1;
  int passed = SDL_Init(SDL_INIT_VIDEO) == 0;
  const char *driver = passed ? SDL_GetCurrentVideoDriver() : NULL;
  const int count = passed ? SDL_GetNumVideoDisplays() : -1;
  passed = passed && driver && strcmp(driver, "x11") == 0 && count == 2;
  fprintf(output,
          "{\"schema\":1,\"probe\":\"m13_sdl_display_probe\","
          "\"sdl_version\":\"%u.%u.%u\",\"driver\":",
          SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_PATCHLEVEL);
  json_string(output, driver ? driver : "");
  fprintf(output, ",\"display_count\":%d,\"displays\":[", count);
  for (int index = 0; index < count; ++index) {
    SDL_DisplayMode mode = {0};
    const int mode_count = SDL_GetNumDisplayModes(index);
    const int mode_ok = SDL_GetCurrentDisplayMode(index, &mode) == 0 &&
                        mode.w > 0 && mode.h > 0 && mode_count > 0;
    passed = passed && mode_ok;
    if (index) fputc(',', output);
    fputs("{\"index\":", output);
    fprintf(output, "%d,\"name\":", index);
    json_string(output, SDL_GetDisplayName(index) ? SDL_GetDisplayName(index) : "");
    fprintf(output,
            ",\"mode_count\":%d,\"current_mode\":{\"width\":%d,"
            "\"height\":%d,\"refresh_rate\":%d}}",
            mode_count, mode.w, mode.h, mode.refresh_rate);
  }
  fprintf(output, "],\"passed\":%s}\n", passed ? "true" : "false");
  const int write_failed = fclose(output) != 0;
  SDL_Quit();
  return passed && !write_failed ? 0 : 1;
}
