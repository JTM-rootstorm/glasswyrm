#include <glasswyrm/ipc.h>

#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
  if (argc != 2) {
    fprintf(stderr,
            "usage: gwipc_wire_probe --print-api-version|--print-wire-version\n");
    return 2;
  }
  if (strcmp(argv[1], "--print-api-version") == 0) {
    const gwipc_api_version version = gwipc_get_api_version();
    printf("%u.%u.%u\n", (unsigned int)version.major,
           (unsigned int)version.minor, (unsigned int)version.patch);
    return 0;
  }
  if (strcmp(argv[1], "--print-wire-version") == 0) {
    const gwipc_wire_version version = gwipc_get_max_wire_version();
    printf("%u.%u\n", (unsigned int)version.major, (unsigned int)version.minor);
    return 0;
  }
  fprintf(stderr, "gwipc_wire_probe: unknown option: %s\n", argv[1]);
  return 2;
}
