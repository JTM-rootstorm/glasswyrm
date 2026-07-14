#include "backends/headless/presenter.hpp"
#include "backends/output/software_frame.hpp"

#include "tests/helpers/test_support.hpp"

#include <array>
#include <filesystem>
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

  gw::test::require(
      presenter.shutdown(error) ==
              glasswyrm::output::BackendStateResult::Complete &&
          error.empty(),
      "headless shutdown completes without restoration work");
  std::filesystem::remove_all(directory);
  return 0;
}
