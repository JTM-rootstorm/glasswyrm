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
  gw::test::require(
      !std::filesystem::exists(directory / "frame-sets.jsonl"),
      "historical single-output commits do not create an M13 manifest");

  glasswyrm::output::SoftwareFrameSet frames;
  std::vector<StagedFrameDump> staged_set(2);
  std::size_t staged_index = 0;
  for (const auto id : {UINT64_C(30), UINT64_C(31)}) {
    glasswyrm::output::OutputFrameResult output;
    gw::test::require(output.frame.configure(id, 1, 1, error), error);
    output.frame.pixels()[0] =
        id == 30 ? UINT32_C(0xff102030) : UINT32_C(0xff405060);
    output.output = output.frame.spec(60'000);
    output.logical = {static_cast<std::int32_t>(id - 30), 0, 1, 1};
    output.damage = {{0, 0, 1, 1}};
    const FrameDumpMetadata set_metadata{2, 200, 9, id, 1, 1, 1};
    gw::test::require(
        dumper.stage(set_metadata, output.frame.pixels(),
                     staged_set[staged_index++], error),
        error);
    gw::test::require(frames.append(std::move(output), error), error);
  }
  gw::test::require(frames.finalize(8, 30, 200, 9, 2, error), error);
  const auto first_final = staged_set[0].final_path();
  const std::string old_output = "previous-output";
  {
    std::ofstream previous(first_final, std::ios::binary);
    previous << old_output;
  }
  const std::string old_sets = "{\"old\":true}\n";
  {
    std::ofstream previous(directory / "frame-sets.jsonl", std::ios::binary);
    previous << old_sets;
  }
  std::filesystem::remove(staged_set[1].temporary_path());
  std::vector<FrameDumpResult> rejected_results;
  gw::test::require(
      !dumper.commit_all(staged_set, frames.view(), rejected_results, error) &&
          rejected_results.empty(),
      "mid-publication failure rejects the complete frame set");
  const auto restored_output = read_bytes(first_final);
  const auto restored_sets = read_bytes(directory / "frame-sets.jsonl");
  gw::test::require(
      std::string(restored_output.begin(), restored_output.end()) == old_output &&
          read_bytes(directory / "frames.jsonl") == manifest &&
          std::string(restored_sets.begin(), restored_sets.end()) == old_sets,
      "frame-set rollback restores output and both manifests");

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
