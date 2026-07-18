#include "backends/headless/presenter.hpp"
#include "backends/output/software_frame.hpp"
#include "backends/output/software_frame_set.hpp"

#include "tests/helpers/test_support.hpp"

#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <unistd.h>

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
  const auto resumed = presenter.resume(frames.view());
  gw::test::require(
      resumed.disposition == PresentDisposition::Complete &&
          resumed.visible_hash == frames.aggregate_hash(),
      "headless frame-set resume atomically replaces its output artifacts");
  const auto manifest_path = directory / "frames.jsonl";
  std::ifstream manifest_input(manifest_path, std::ios::binary);
  const std::string manifest_before{
      std::istreambuf_iterator<char>(manifest_input), {}};
  gw::test::require(
      manifest_before.find("\"output_id\":30") != std::string::npos &&
          manifest_before.find("\"output_id\":31") != std::string::npos,
      "headless frame-set manifest records each sorted output");

  glasswyrm::output::SoftwareFrameSet failing;
  for (const auto id : {UINT64_C(32), UINT64_C(33)}) {
    glasswyrm::output::OutputFrameResult output;
    gw::test::require(output.frame.configure(id, 1, 1, error), error);
    output.output = output.frame.spec(60'000);
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
  gw::test::require(
      atomic_rejection.disposition == PresentDisposition::Rejected &&
          !std::filesystem::exists(
              directory / "frame-000003-output-0000000000000032.ppm") &&
          manifest_after == manifest_before,
      "headless frame-set publication rolls back every output on failure");

  gw::test::require(
      presenter.shutdown(error) ==
              glasswyrm::output::BackendStateResult::Complete &&
          error.empty(),
      "headless shutdown completes without restoration work");
  std::filesystem::remove_all(directory);
  return 0;
}
