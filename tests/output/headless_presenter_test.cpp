#include "backends/headless/presenter.hpp"
#include "backends/output/software_frame.hpp"
#include "backends/output/software_frame_set.hpp"

#include "tests/helpers/test_support.hpp"

#include <array>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>
#include <unistd.h>

namespace {

std::string hex64(const std::uint64_t value) {
  std::ostringstream output;
  output << std::hex << std::setfill('0') << std::setw(16) << value;
  return output.str();
}

std::size_t occurrences(const std::string& text,
                        const std::string_view needle) {
  std::size_t count = 0;
  for (std::size_t offset = 0;
       (offset = text.find(needle, offset)) != std::string::npos;
       offset += needle.size())
    ++count;
  return count;
}

} // namespace

int main() {
  using glasswyrm::headless::Presenter;
  using glasswyrm::output::BackendEventKind;
  using glasswyrm::output::BackendStateResult;
  using glasswyrm::output::PresentDisposition;
  using glasswyrm::output::SoftwareFrame;
  using glasswyrm::output::SoftwareFrameView;

  const auto directory = std::filesystem::temp_directory_path() /
      ("glasswyrm-headless-presenter-" +
       std::to_string(static_cast<long long>(::getpid())));
  std::filesystem::remove_all(directory);

  SoftwareFrame frame;
  std::string error;
  gw::test::require(frame.configure(26, 2, 2, error),
                    "presenter frame configures");
  frame.pixels()[0] = 0xff112233U;
  frame.pixels()[1] = 0x00445566U;
  frame.pixels()[2] = 0xff778899U;
  frame.pixels()[3] = 0xffaabbccU;
  const std::array<gw::compositor::Rectangle, 3> damage{
      gw::compositor::Rectangle{0, 0, 1, 1},
      gw::compositor::Rectangle{1, 0, 1, 1},
      gw::compositor::Rectangle{0, 1, 2, 1}};
  const SoftwareFrameView view{frame.spec(60'000), frame.pixels(), damage,
                               100, 7, 1};

  Presenter presenter(directory);
  const auto result = presenter.present(view);
  gw::test::require(result.disposition == PresentDisposition::Complete &&
                        result.token == 0 &&
                        result.visible_hash == 0x4d1416c2755838b5ULL,
                    "headless presentation completes synchronously");
  gw::test::require(presenter.poll_fd() == -1 &&
                        presenter.poll_events() == 0,
                    "headless presenter has no event source");
  gw::test::require(presenter.service(0).kind == BackendEventKind::None &&
                        presenter.service(1).kind == BackendEventKind::Fatal,
                    "headless event servicing rejects impossible events");
  gw::test::require(presenter.suspend(error) == BackendStateResult::Complete &&
                        error.empty(),
                    "headless suspension completes synchronously");

  auto invalid = view;
  invalid.ordinal = 0;
  const auto rejected = presenter.present(invalid);
  gw::test::require(rejected.disposition == PresentDisposition::Rejected &&
                        !rejected.error.empty(),
                    "invalid presentation is rejected without publication");

  glasswyrm::output::SoftwareFrameSet frames;
  for (const auto id : {UINT64_C(30), UINT64_C(31)}) {
    glasswyrm::output::OutputFrameResult output;
    gw::test::require(output.frame.configure(id, 1, 1, error), error);
    output.frame.pixels()[0] =
        id == 30 ? UINT32_C(0xff102030) : UINT32_C(0xff405060);
    output.output = output.frame.spec(60'000);
    output.logical = {static_cast<std::int32_t>(id - 30), 0, 1, 1};
    output.damage = {{0, 0, 1, 1}};
    gw::test::require(frames.append(std::move(output), error), error);
  }
  gw::test::require(frames.finalize(8, 30, 200, 9, 2, error), error);
  const auto multi = presenter.present(frames.view());
  gw::test::require(
          multi.disposition == PresentDisposition::Complete &&
          multi.visible_hash == frames.aggregate_hash() &&
          std::filesystem::exists(
              directory / "frame-000002-output-0000000000000030.ppm") &&
          std::filesystem::exists(
              directory / "frame-000002-output-0000000000000031.ppm"),
      "headless frame-set presentation publishes every output together: " +
          multi.error);
  const std::string expected_set_manifest =
      "{\"schema_version\":13,\"transaction_ordinal\":2,\"commit_id\":200,"
      "\"generation\":9,\"layout_generation\":8,\"primary_output_id\":"
      "\"000000000000001e\",\"output_count\":2,\"aggregate_hash\":\"" +
      hex64(frames.aggregate_hash()) +
      "\",\"outputs\":[{\"output_id\":\"000000000000001e\",\"file\":"
      "\"frame-000002-output-0000000000000030.ppm\",\"fnv1a64\":\"" +
      hex64(frames.outputs().at(30).visible_hash) +
      "\",\"physical\":{\"width\":1,\"height\":1},\"logical\":{\"x\":0,"
      "\"y\":0,\"width\":1,\"height\":1},\"scale\":{\"numerator\":1,"
      "\"denominator\":1},\"transform\":\"normal\",\"damage\":[{\"x\":0,"
      "\"y\":0,\"width\":1,\"height\":1}]},{\"output_id\":"
      "\"000000000000001f\",\"file\":"
      "\"frame-000002-output-0000000000000031.ppm\",\"fnv1a64\":\"" +
      hex64(frames.outputs().at(31).visible_hash) +
      "\",\"physical\":{\"width\":1,\"height\":1},\"logical\":{\"x\":1,"
      "\"y\":0,\"width\":1,\"height\":1},\"scale\":{\"numerator\":1,"
      "\"denominator\":1},\"transform\":\"normal\",\"damage\":[{\"x\":0,"
      "\"y\":0,\"width\":1,\"height\":1}]}]}\n";
  std::ifstream set_manifest_input(directory / "frame-sets.jsonl",
                                   std::ios::binary);
  const std::string set_manifest_once{
      std::istreambuf_iterator<char>(set_manifest_input), {}};
  gw::test::require(set_manifest_once == expected_set_manifest,
                    "headless frame-set manifest is deterministic and complete");
  const auto resumed = presenter.resume(frames.view());
  gw::test::require(
      resumed.disposition == PresentDisposition::Complete &&
          resumed.visible_hash == frames.aggregate_hash(),
      "headless frame-set resume atomically replaces its output artifacts");
  const auto manifest_path = directory / "frames.jsonl";
  std::ifstream manifest_input(manifest_path, std::ios::binary);
  const std::string manifest_before{
      std::istreambuf_iterator<char>(manifest_input), {}};
  std::ifstream sets_before_input(directory / "frame-sets.jsonl",
                                  std::ios::binary);
  const std::string sets_before{
      std::istreambuf_iterator<char>(sets_before_input), {}};
  gw::test::require(
      occurrences(manifest_before, "\"output_id\":30") == 2 &&
          occurrences(manifest_before, "\"output_id\":31") == 2 &&
          sets_before == expected_set_manifest,
      "resume republishes legacy frames but not a duplicate transaction");

  auto invalid_set = frames.view();
  invalid_set.aggregate_hash ^= 1U;
  const auto invalid_metadata = presenter.present(invalid_set);
  std::ifstream invalid_sets_input(directory / "frame-sets.jsonl",
                                   std::ios::binary);
  const std::string invalid_sets{
      std::istreambuf_iterator<char>(invalid_sets_input), {}};
  gw::test::require(
      invalid_metadata.disposition == PresentDisposition::Rejected &&
          invalid_sets == sets_before,
      "inconsistent aggregate metadata publishes no frame-set record");

  glasswyrm::output::SoftwareFrameSet failing;
  for (const auto id : {UINT64_C(32), UINT64_C(33)}) {
    glasswyrm::output::OutputFrameResult output;
    gw::test::require(output.frame.configure(id, 1, 1, error), error);
    output.output = output.frame.spec(60'000);
    output.logical = {static_cast<std::int32_t>(id - 32), 0, 1, 1};
    gw::test::require(failing.append(std::move(output), error), error);
  }
  gw::test::require(failing.finalize(8, 32, 201, 10, 3, error), error);
  const auto collision = directory /
      (".frame-000003-output-0000000000000033.ppm.tmp." +
       std::to_string(static_cast<long long>(::getpid())));
  std::ofstream(collision, std::ios::binary) << "foreign";
  const auto atomic_rejection = presenter.present(failing.view());
  std::ifstream manifest_after_input(manifest_path, std::ios::binary);
  const std::string manifest_after{
      std::istreambuf_iterator<char>(manifest_after_input), {}};
  std::ifstream sets_after_input(directory / "frame-sets.jsonl",
                                 std::ios::binary);
  const std::string sets_after{
      std::istreambuf_iterator<char>(sets_after_input), {}};
  gw::test::require(
      atomic_rejection.disposition == PresentDisposition::Rejected &&
          !std::filesystem::exists(
              directory / "frame-000003-output-0000000000000032.ppm") &&
          manifest_after == manifest_before && sets_after == sets_before,
      "headless frame-set publication rolls back every output on failure");

  gw::test::require(
      presenter.shutdown(error) ==
              glasswyrm::output::BackendStateResult::Complete &&
          error.empty(),
      "headless shutdown completes without restoration work");
  std::filesystem::remove_all(directory);
  return 0;
}
