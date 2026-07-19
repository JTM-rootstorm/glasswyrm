#include "backends/headless/inventory.hpp"
#include "backends/headless/presenter.hpp"
#include "backends/headless/vrr_simulation.hpp"
#include "backends/output/software_frame_set.hpp"

#include "tests/helpers/test_support.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <unistd.h>
#include <utility>
#include <vector>

int main() {
  using namespace glasswyrm;
  std::string error;
  auto inventory = headless::OutputInventory::build(
      std::vector{headless::OutputRequest{"LEFT", 1, 1, 60'000},
                  headless::OutputRequest{"RIGHT", 1, 1, 75'000}},
      error);
  gw::test::require(inventory.has_value(), error);
  auto simulation = headless::VrrSimulation::build(
      inventory->layout(),
      std::vector{headless::VrrSimulationRequest{"LEFT", 40'000, 60'000},
                  headless::VrrSimulationRequest{"RIGHT", 48'000, 75'000}},
      error);
  gw::test::require(simulation.has_value(), error);

  const auto directory = std::filesystem::temp_directory_path() /
      ("glasswyrm-headless-vrr-presenter-" +
       std::to_string(static_cast<long long>(::getpid())));
  std::filesystem::remove_all(directory);
  const auto left = inventory->layout().output_order[0].value;
  const auto right = inventory->layout().output_order[1].value;
  std::filesystem::create_directories(directory);
  const auto report_path = directory / "vrr.jsonl";
  auto report = headless::VrrReport::create(report_path, error);
  gw::test::require(report.has_value(), error);
  std::string duplicate_error;
  gw::test::require(!headless::VrrReport::create(report_path, duplicate_error) &&
                        !duplicate_error.empty(),
                    "VRR report creation never overwrites existing evidence");
  for (const auto id : inventory->layout().output_order) {
    const auto capability = simulation->capability(id);
    gw::test::require(capability.has_value(),
                      "configured output has simulation capability");
    gw::test::require(report->record_capability(*capability, error), error);
  }
  headless::Presenter presenter(directory, std::move(simulation),
                                std::move(report));
  const auto left_capability = presenter.vrr_capability(left);
  gw::test::require(
      left_capability && left_capability->output_enabled &&
          left_capability->connected && !left_capability->drm &&
          left_capability->connector_property_present &&
          !left_capability->hardware_capable &&
          !left_capability->atomic_kms_available &&
          left_capability->atomic_test_passed &&
          left_capability->kms_controllable && left_capability->simulated &&
          left_capability->timing_available,
      "presenter exposes simulated capability without a hardware claim");

  output::SoftwareFrameSet frames;
  for (const auto id : {left, right}) {
    output::OutputFrameResult frame;
    gw::test::require(frame.frame.configure(id, 1, 1, error), error);
    frame.output = frame.frame.spec(
        id == left ? UINT32_C(60'000) : UINT32_C(75'000));
    frame.logical = {id == left ? 0 : 1, 0, 1, 1};
    frame.damage = {{0, 0, 1, 1}};
    gw::test::require(frames.append(std::move(frame), error), error);
  }
  gw::test::require(frames.finalize(1, left, 7, 9, 1, error), error);
  std::map<std::uint64_t, output::VrrPresentationRequest> requests;
  requests[left] = {true,
                    output::vrr::PolicyMode::AlwaysEligible,
                    output::vrr::Decision::Enabled,
                    true,
                    0,
                    0,
                    output::vrr::reason_bit(
                        output::vrr::Reason::SimulatedHeadless),
                    9,
                    1,
                    20'000'000};
  requests[right] = {true,
                     output::vrr::PolicyMode::Off,
                     output::vrr::Decision::Disabled,
                     false,
                     0,
                     0,
                     output::vrr::reason_bit(output::vrr::Reason::PolicyOff),
                     9,
                     1,
                     0};
  gw::test::require(frames.set_vrr_requests(requests, error), error);

  const auto presented = presenter.present(frames.view());
  gw::test::require(
      presented.disposition == output::PresentDisposition::Complete &&
          presented.vrr_feedback.size() == 2,
      "simulated VRR frame set completes synchronously");
  const auto &left_feedback = presented.vrr_feedback.at(left);
  const auto &right_feedback = presented.vrr_feedback.at(right);
  gw::test::require(
      left_feedback.effective_enabled && left_feedback.property_readback_valid &&
          left_feedback.session_active && left_feedback.flip_sequence == 1 &&
          left_feedback.interval_nanoseconds == 20'000'000 &&
          left_feedback.timestamp_available &&
          (left_feedback.flags & output::kVrrPresentationFeedbackSimulated) != 0,
      "enabled output returns requested deterministic simulated timing");
  gw::test::require(
      !right_feedback.effective_enabled && right_feedback.flip_sequence == 1 &&
          right_feedback.interval_nanoseconds ==
              headless::refresh_interval_nanoseconds(75'000),
      "disabled output remains independently on nominal cadence");

  gw::test::require(
      presenter.shutdown(error) == output::BackendStateResult::Complete, error);
  std::ifstream report_input(report_path, std::ios::binary);
  const std::string report_text{std::istreambuf_iterator<char>(report_input),
                                {}};
  gw::test::require(
      report_text.find("\"record\":\"capability\"") != std::string::npos &&
          report_text.find("\"record\":\"decision\"") !=
              std::string::npos &&
          report_text.find("\"record\":\"timing\"") != std::string::npos &&
          report_text.find("\"record\":\"summary\"") !=
              std::string::npos &&
          report_text.find("\"record\":\"restore\"") !=
              std::string::npos &&
          report_text.find("wall_clock") == std::string::npos,
      "headless report contains deterministic lifecycle records only");

  std::filesystem::remove_all(directory);
}
