#include "backends/headless/frame_dump.hpp"
#include "backends/headless/output.hpp"

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
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

}  // namespace

int main() {
  using glasswyrm::headless::FrameDumpMetadata;
  using glasswyrm::headless::FrameDumpResult;
  using glasswyrm::headless::FrameDumper;
  using glasswyrm::headless::Output;

  std::string error;
  Output output;
  gw::test::require(!output.configure(0, 1, 1, error), "assertion");
  gw::test::require(!output.configure(1, 4097, 1, error), "assertion");
  gw::test::require(output.configure(0x1a, 2, 2, error), "assertion");
  gw::test::require(output.enabled(), "assertion");
  gw::test::require((output.pixels().size()) == (4U), "equality assertion");
  for (const auto pixel : output.pixels()) gw::test::require((pixel) == (Output::kClearPixel), "equality assertion");

  output.pixels()[0] = 0xff112233U;
  output.pixels()[1] = 0x00445566U;
  output.pixels()[2] = 0xff778899U;
  output.pixels()[3] = 0xffaabbccU;

  const auto directory = std::filesystem::temp_directory_path() /
      ("glasswyrm-headless-output-" + std::to_string(static_cast<long long>(::getpid())));
  std::filesystem::remove_all(directory);
  FrameDumper dumper(directory);
  FrameDumpResult result;
  const FrameDumpMetadata metadata{1, 100, 7, output.id(), output.width(),
                                   output.height(), 3};
  gw::test::require(dumper.dump(metadata, output.pixels(), result, error), "assertion");
  gw::test::require((result.fnv1a64) == (0x4d1416c2755838b5ULL), "equality assertion");
  gw::test::require(
      result.file.filename().string() ==
          std::string("frame-000001-output-0000000000000026.ppm"),
      "frame dump name");

  const std::array<std::uint8_t, 23> expected{
      'P', '6', '\n', '2', ' ', '2', '\n', '2', '5', '5', '\n',
      0x11, 0x22, 0x33, 0x44, 0x55, 0x66,
      0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc};
  const auto ppm = read_bytes(result.file);
  gw::test::require((ppm.size()) == (expected.size()), "equality assertion");
  gw::test::require(std::equal(ppm.begin(), ppm.end(), expected.begin()), "assertion");

  const auto manifest = read_bytes(directory / "frames.jsonl");
  const std::string expected_manifest =
      "{\"frame\":1,\"commit_id\":100,\"generation\":7,\"output_id\":26,"
      "\"width\":2,\"height\":2,\"damage_rectangles\":3,"
      "\"fnv1a64\":\"4d1416c2755838b5\","
      "\"file\":\"frame-000001-output-0000000000000026.ppm\"}\n";
  gw::test::require((std::string(manifest.begin(), manifest.end())) == (expected_manifest), "equality assertion");

  FrameDumpResult invalid_result;
  auto invalid = metadata;
  invalid.frame = 2;
  invalid.width = 3;
  gw::test::require(!dumper.dump(invalid, output.pixels(), invalid_result, error), "assertion");
  gw::test::require(
      !std::filesystem::exists(
          directory / "frame-000002-output-0000000000000026.ppm"),
      "invalid dump must not create a frame");

  output.disable();
  gw::test::require(!output.enabled(), "assertion");
  gw::test::require(output.pixels().empty(), "assertion");
  std::filesystem::remove_all(directory);
  return 0;
}
