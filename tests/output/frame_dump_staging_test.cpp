#include "backends/headless/frame_dump.hpp"

#include "tests/helpers/test_support.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

std::vector<std::uint8_t> read_bytes(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

}  // namespace

int main() {
  using glasswyrm::headless::FrameDumpMetadata;
  using glasswyrm::headless::FrameDumpResult;
  using glasswyrm::headless::FrameDumper;
  using glasswyrm::headless::StagedFrameDump;

  const auto directory = std::filesystem::temp_directory_path() /
      ("glasswyrm-frame-staging-" +
       std::to_string(static_cast<long long>(::getpid())));
  std::filesystem::remove_all(directory);
  FrameDumper dumper(directory);
  const std::array<std::uint32_t, 4> pixels{
      0xff112233U, 0x00445566U, 0xff778899U, 0xffaabbccU};
  const FrameDumpMetadata metadata{1, 100, 7, 26, 2, 2, 3};
  std::string error;

  StagedFrameDump staged;
  gw::test::require(dumper.stage(metadata, pixels, staged, error),
                    "frame dump stages");
  gw::test::require(staged.active() &&
                        std::filesystem::exists(staged.temporary_path()),
                    "staged frame exists only as a temporary file");
  gw::test::require(!std::filesystem::exists(staged.final_path()) &&
                        !std::filesystem::exists(directory / "frames.jsonl"),
                    "staging publishes neither frame nor manifest");
  dumper.abort(staged);
  gw::test::require(!staged.active() &&
                        !std::filesystem::exists(staged.temporary_path()) &&
                        !std::filesystem::exists(staged.final_path()),
                    "abort removes staged evidence");

  gw::test::require(dumper.stage(metadata, pixels, staged, error),
                    "frame dump restages after abort");
  FrameDumpResult result;
  gw::test::require(dumper.commit(staged, result, error),
                    "staged frame commits");
  gw::test::require(result.fnv1a64 == 0x4d1416c2755838b5ULL,
                    "committed RGB hash remains stable");
  const std::array<std::uint8_t, 23> expected{
      'P', '6', '\n', '2', ' ', '2', '\n', '2', '5', '5', '\n',
      0x11, 0x22, 0x33, 0x44, 0x55, 0x66,
      0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc};
  gw::test::require(read_bytes(result.file) ==
                        std::vector<std::uint8_t>(expected.begin(), expected.end()),
                    "committed PPM bytes remain stable");
  const auto manifest = read_bytes(directory / "frames.jsonl");
  const std::string expected_manifest =
      "{\"frame\":1,\"commit_id\":100,\"generation\":7,\"output_id\":26,"
      "\"width\":2,\"height\":2,\"damage_rectangles\":3,"
      "\"fnv1a64\":\"4d1416c2755838b5\","
      "\"file\":\"frame-000001-output-0000000000000026.ppm\"}\n";
  gw::test::require(std::string(manifest.begin(), manifest.end()) ==
                        expected_manifest,
                    "committed manifest bytes remain stable");

  {
    StagedFrameDump abandoned;
    auto second = metadata;
    second.frame = 2;
    gw::test::require(dumper.stage(second, pixels, abandoned, error),
                      "frame stages for RAII cleanup");
  }
  gw::test::require(
      !std::filesystem::exists(
          directory / "frame-000002-output-0000000000000026.ppm"),
      "destroying a staged frame leaves no final artifact");

  std::filesystem::remove_all(directory);
  return 0;
}
