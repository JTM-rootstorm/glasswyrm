#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

[[noreturn]] void fail(const char* message) {
  std::fprintf(stderr, "gwcomp golden test: %s\n", message);
  std::exit(1);
}

void require(bool condition, const char* message) {
  if (!condition) fail(message);
}

std::vector<unsigned char> read_file(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

}  // namespace

int main(int argc, char** argv) {
  require(argc == 3,
          "usage: gwcomp_golden_test /path/to/gwcomp /path/to/producer");
  char temporary[] = "/tmp/gwcomp-golden-test-XXXXXX";
  require(::mkdtemp(temporary) != nullptr, "create temporary directory");
  const std::filesystem::path root = temporary;
  const auto socket = (root / "gwcomp.sock").string();
  const auto dumps = (root / "dumps").string();

  const pid_t compositor = ::fork();
  require(compositor >= 0, "fork gwcomp");
  if (compositor == 0) {
    ::execl(argv[1], argv[1], "--ipc-socket", socket.c_str(), "--dump-dir",
            dumps.c_str(), "--max-frames", "1", nullptr);
    _exit(127);
  }
  struct stat status {};
  bool ready = false;
  for (int attempt = 0; attempt < 200; ++attempt) {
    if (::lstat(socket.c_str(), &status) == 0 && S_ISSOCK(status.st_mode)) {
      ready = true;
      break;
    }
    int child_status = 0;
    require(::waitpid(compositor, &child_status, WNOHANG) == 0,
            "gwcomp remains alive during startup");
    (void)::usleep(10'000);
  }
  require(ready, "gwcomp listener becomes ready");

  const pid_t producer = ::fork();
  require(producer >= 0, "fork synthetic producer");
  if (producer == 0) {
    ::execl(argv[2], argv[2], "--socket", socket.c_str(), "--scenario", "basic",
            nullptr);
    _exit(127);
  }
  int producer_status = 0;
  require(::waitpid(producer, &producer_status, 0) == producer,
          "wait for synthetic producer");
  require(WIFEXITED(producer_status) && WEXITSTATUS(producer_status) == 0,
          "producer receives accepted correlated acknowledgement");
  int compositor_status = 0;
  require(::waitpid(compositor, &compositor_status, 0) == compositor,
          "wait for gwcomp");
  require(WIFEXITED(compositor_status) && WEXITSTATUS(compositor_status) == 0,
          "gwcomp exits after maximum accepted frames");

  const auto ppm = read_file(
      root / "dumps/frame-000001-output-0000000000000001.ppm");
  constexpr std::size_t kHeaderSize = 15;  // P6\n320 200\n255\n
  require(ppm.size() == kHeaderSize + 320U * 200U * 3U,
          "frame dump has exact dimensions");
  require(std::string(ppm.begin(), ppm.begin() + kHeaderSize) ==
              "P6\n320 200\n255\n",
          "frame dump has canonical PPM header");
  const auto pixel = [&](std::size_t x, std::size_t y) {
    return kHeaderSize + (y * 320U + x) * 3U;
  };
  const auto background = pixel(0, 0);
  require(ppm[background] == 0x20 && ppm[background + 1] == 0x40 &&
              ppm[background + 2] == 0x60,
          "XRGB background renders exactly");
  const auto overlay = pixel(80, 50);
  require(ppm[overlay] == 0x90 && ppm[overlay + 1] == 0x20 &&
              ppm[overlay + 2] == 0x30,
          "premultiplied ARGB overlay blends exactly");
  const auto after_overlay = pixel(160, 50);
  require(ppm[after_overlay] == 0x20 && ppm[after_overlay + 1] == 0x40 &&
              ppm[after_overlay + 2] == 0x60,
          "overlay bounds are clipped at the expected edge");

  const auto manifest_bytes = read_file(root / "dumps/frames.jsonl");
  const std::string manifest(manifest_bytes.begin(), manifest_bytes.end());
  require(manifest.find("\"frame\":1") != std::string::npos &&
              manifest.find("\"commit_id\":100") != std::string::npos &&
              manifest.find("\"generation\":1") != std::string::npos &&
              manifest.find("\"damage_rectangles\":1") != std::string::npos &&
              manifest.find("\"fnv1a64\":\"") != std::string::npos,
          "manifest records identity, damage, and deterministic hash");

  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);
  return 0;
}
