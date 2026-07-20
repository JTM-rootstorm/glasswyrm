#include "backends/output/presentation_backend.hpp"
#include "gwcomp/compositor.hpp"
#include "tests/helpers/test_support.hpp"

#include <glasswyrm/ipc.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <fcntl.h>
#include <memory>
#include <span>
#include <string>
#include <sys/mman.h>
#include <unistd.h>
#include <vector>

namespace {

using glasswyrm::output::BackendEvent;
using glasswyrm::output::BackendStateResult;
using glasswyrm::output::PresentDisposition;
using glasswyrm::output::PresentResult;
using glasswyrm::output::PresentationBackend;
using glasswyrm::output::SoftwareFrameSetView;
using glasswyrm::output::SoftwareFrameView;
using glasswyrm::output::VrrPresentationCapability;
using glasswyrm::output::VrrPresentationRequest;
using gw::test::require;

struct ObservedPresentation {
  bool reject_next{};
  std::vector<VrrPresentationRequest> requests;
  std::vector<std::uint64_t> hashes;
};

class FakeVrrPresenter final : public PresentationBackend {
 public:
  explicit FakeVrrPresenter(std::shared_ptr<ObservedPresentation> observed)
      : observed_(std::move(observed)) {}

  std::optional<VrrPresentationCapability> vrr_capability(
      const std::uint64_t output_id) const noexcept override {
    if (output_id != 1) return std::nullopt;
    VrrPresentationCapability result;
    result.output_enabled = true;
    result.connected = true;
    result.kms_controllable = true;
    result.simulated = true;
    result.session_active = true;
    result.timing_available = true;
    return result;
  }

  PresentResult present(const SoftwareFrameView&) override {
    return {PresentDisposition::Rejected, 0, 0,
            "M14 presenter requires an output frame set"};
  }

  PresentResult present(const SoftwareFrameSetView& frame) override {
    if (!frame.valid() || frame.outputs->size() != 1)
      return {PresentDisposition::Fatal, 0, 0, "invalid test frame set"};
    const auto& output = frame.outputs->at(1);
    observed_->requests.push_back(output.vrr);
    observed_->hashes.push_back(frame.aggregate_hash);
    if (observed_->reject_next) {
      observed_->reject_next = false;
      return {PresentDisposition::Rejected, 0, 0,
              "injected VRR presenter rejection"};
    }
    glasswyrm::output::VrrPresentationFeedback feedback;
    feedback.output_id = 1;
    feedback.effective_enabled = output.vrr.desired_enabled;
    feedback.property_readback_valid = true;
    feedback.session_active = true;
    feedback.flip_sequence = static_cast<std::uint32_t>(observed_->hashes.size());
    feedback.flags =
        glasswyrm::output::kVrrPresentationFeedbackSimulated;
    feedback.kernel_timestamp_nanoseconds =
        UINT64_C(1'000'000'000) + observed_->hashes.size() * UINT64_C(16'666'667);
    feedback.interval_nanoseconds = UINT64_C(16'666'667);
    feedback.timestamp_available = true;
    return {PresentDisposition::Complete, 0, frame.aggregate_hash, {},
            {{1, feedback}}};
  }

  int poll_fd() const noexcept override { return -1; }
  short poll_events() const noexcept override { return 0; }
  BackendEvent service(short) override { return {}; }
  BackendStateResult suspend(std::string& error) override {
    error.clear();
    return BackendStateResult::Complete;
  }
  PresentResult resume(const SoftwareFrameView&) override { return {}; }
  BackendStateResult shutdown(std::string& error) noexcept override {
    error.clear();
    return BackendStateResult::Complete;
  }

 private:
  std::shared_ptr<ObservedPresentation> observed_;
};

gwipc_sdr_color_metadata srgb() {
  return {GWIPC_SDR_COLOR_SPACE_SRGB, GWIPC_TRANSFER_FUNCTION_SRGB,
          GWIPC_COLOR_PRIMARIES_SRGB, 0, 0, 0, 0};
}

gwipc_output_upsert output() {
  gwipc_output_upsert value{};
  value.struct_size = sizeof(value);
  value.output_id = 1;
  value.enabled = 1;
  value.logical_width = value.physical_pixel_width = 2;
  value.logical_height = value.physical_pixel_height = 2;
  value.refresh_millihertz = 60'000;
  value.scale_numerator = value.scale_denominator = 1;
  value.transform = GWIPC_TRANSFORM_NORMAL;
  value.color = srgb();
  return value;
}

gwipc_surface_upsert surface() {
  gwipc_surface_upsert value{};
  value.struct_size = sizeof(value);
  value.surface_id = 10;
  value.x11_window_id = 42;
  value.output_id = 1;
  value.logical_width = value.logical_height = 2;
  value.visible = 1;
  value.transform = GWIPC_TRANSFORM_NORMAL;
  value.opacity = GWIPC_OPACITY_ONE;
  value.scale_numerator = value.scale_denominator = 1;
  value.color = srgb();
  value.fullscreen_eligible = GWIPC_TRI_STATE_TRUE;
  value.direct_scanout_eligible = GWIPC_TRI_STATE_UNKNOWN;
  return value;
}

gwipc_surface_policy_upsert surface_policy() {
  gwipc_surface_policy_upsert value{};
  value.struct_size = sizeof(value);
  value.surface_id = 10;
  value.x11_window_id = 42;
  value.workspace_id = 1;
  value.window_type = GWIPC_POLICY_WINDOW_NORMAL;
  value.applied_state = GWIPC_POLICY_APPLIED_FULLSCREEN;
  value.managed = 1;
  value.fullscreen_eligible = GWIPC_TRI_STATE_TRUE;
  value.direct_scanout_eligible = GWIPC_TRI_STATE_UNKNOWN;
  return value;
}

gwipc_surface_output_state membership() {
  static constexpr std::array<std::uint64_t, 1> outputs{1};
  gwipc_surface_output_state value{};
  value.struct_size = sizeof(value);
  value.surface_id = 10;
  value.primary_output_id = 1;
  value.output_ids = outputs.data();
  value.output_count = outputs.size();
  value.preferred_scale_numerator = value.preferred_scale_denominator = 1;
  value.client_buffer_scale = 1;
  value.scale_mode = GWIPC_SURFACE_SCALE_LEGACY;
  value.layout_generation = 7;
  return value;
}

gwipc_output_vrr_policy_upsert vrr_policy(
    const gwipc_vrr_policy_mode mode) {
  gwipc_output_vrr_policy_upsert value{};
  value.struct_size = sizeof(value);
  value.output_id = 1;
  value.mode = mode;
  return value;
}

gwipc_surface_vrr_state vrr_surface(const std::uint64_t generation,
                                    const bool selected) {
  gwipc_surface_vrr_state value{};
  value.struct_size = sizeof(value);
  value.surface_id = 10;
  value.window_id = 42;
  value.output_id = 1;
  value.preference = GWIPC_VRR_PREFERENCE_ALLOW;
  value.policy_selected = selected;
  value.policy_eligible = 1;
  value.focused = 1;
  value.fullscreen = 1;
  value.exclusive_output_membership = 1;
  value.policy_generation = generation;
  return value;
}

gwipc_buffer_attach attachment(const std::uint64_t buffer_id = 100) {
  gwipc_buffer_attach value{};
  value.struct_size = sizeof(value);
  value.buffer_id = buffer_id;
  value.surface_id = 10;
  value.width = value.height = 2;
  value.stride = 8;
  value.storage_size = 16;
  value.pixel_format = GWIPC_PIXEL_FORMAT_XRGB8888;
  value.alpha_semantics = GWIPC_ALPHA_OPAQUE;
  value.color = srgb();
  value.synchronization = GWIPC_SYNCHRONIZATION_NONE;
  return value;
}

int buffer_fd() {
  constexpr std::array<std::uint32_t, 4> pixels{
      UINT32_C(0xff204060), UINT32_C(0xff204060),
      UINT32_C(0xff204060), UINT32_C(0xff204060)};
  const int fd = ::memfd_create("gwcomp-vrr-presentation-test",
                                MFD_CLOEXEC | MFD_ALLOW_SEALING);
  require(fd >= 0 && ::ftruncate(fd, sizeof(pixels)) == 0 &&
              ::pwrite(fd, pixels.data(), sizeof(pixels), 0) ==
                  static_cast<ssize_t>(sizeof(pixels)) &&
              ::fcntl(fd, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_GROW) == 0,
          "create sealed M14 presentation buffer");
  return fd;
}

gwipc_frame_commit frame(const std::uint64_t id) {
  gwipc_frame_commit value{};
  value.struct_size = sizeof(value);
  value.commit_id = id;
  value.producer_generation = id;
  return value;
}

void stage(gw::compositor::Compositor& compositor,
           const gwipc_vrr_policy_mode mode, const std::uint64_t generation,
           const bool attach_buffer, std::string& error,
           const std::uint64_t buffer_id = 100) {
  require(compositor.begin_snapshot(7) && compositor.apply(output()) &&
              compositor.apply(surface()) &&
              compositor.apply(membership()) &&
              compositor.apply(surface_policy()) &&
              compositor.apply(vrr_policy(mode)) &&
              compositor.apply(vrr_surface(
                  generation, mode != GWIPC_VRR_POLICY_OFF)),
          "stage complete M14 snapshot");
  if (attach_buffer)
    require(compositor.attach(attachment(buffer_id), buffer_fd(), error),
            "attach M14 window buffer");
  require(compositor.end_snapshot(), "finish complete M14 snapshot");
}

bool is_release(const gw::compositor::VrrResponseMessage& response,
                const std::uint64_t buffer_id,
                const gwipc_buffer_release_reason reason) {
  gwipc_buffer_release release{};
  release.struct_size = sizeof(release);
  release.buffer_id = buffer_id;
  release.reason = reason;
  gwipc_contract_payload* raw = nullptr;
  if (gwipc_contract_encode_buffer_release(&release, &raw) != GWIPC_STATUS_OK)
    return false;
  const std::unique_ptr<gwipc_contract_payload,
                        decltype(&gwipc_contract_payload_destroy)>
      expected(raw, gwipc_contract_payload_destroy);
  std::size_t expected_size = 0;
  const auto* expected_bytes =
      gwipc_contract_payload_data(expected.get(), &expected_size);
  return response.payload.size() == expected_size && expected_bytes != nullptr &&
         std::equal(response.payload.begin(), response.payload.end(),
                    expected_bytes);
}

}  // namespace

int main() {
  std::string error;
  auto observed = std::make_shared<ObservedPresentation>();
  auto presenter = std::make_unique<FakeVrrPresenter>(observed);
  gw::compositor::Compositor compositor(std::move(presenter));
  compositor.set_peer_profile(
      gw::compositor::PeerProfile::M7BufferedProtocolServer);
  gw::test::require(compositor.configure_vrr_contract(true, error), error);
  require(compositor.configure_scene_profile(
              gw::compositor::SceneProfile::OutputModel, 1, 7),
          "configure M14 output-model compositor");

  stage(compositor, GWIPC_VRR_POLICY_FULLSCREEN, 1, true, error);
  const auto enabled = compositor.commit(frame(1), error);
  require(enabled.result == GWIPC_FRAME_ACCEPTED &&
              enabled.disposition ==
                  gw::compositor::PresentedFrame::Disposition::Complete &&
              enabled.vrr_response.size() == 3 &&
              enabled.vrr_response[0].type ==
                  GWIPC_MESSAGE_OUTPUT_VRR_STATE_UPSERT &&
              enabled.vrr_response[1].type ==
                  GWIPC_MESSAGE_PRESENTATION_TIMING &&
              enabled.vrr_response[2].type ==
                  GWIPC_MESSAGE_FRAME_ACKNOWLEDGED &&
              observed->requests.at(0).desired_enabled &&
              observed->requests.at(0).transition_serial == 1,
          "same-frame VRR request completes with exact ordered response: " +
              error);

  stage(compositor, GWIPC_VRR_POLICY_OFF, 2, false, error);
  observed->reject_next = true;
  const auto rejected = compositor.commit(frame(2), error);
  require(rejected.disposition ==
              gw::compositor::PresentedFrame::Disposition::Rejected &&
              compositor.accepted_frames() == 1 &&
              observed->requests.at(1).transition_serial == 2,
          "presenter rejection preserves the committed frame and VRR state");

  const auto disabled = compositor.commit(frame(3), error);
  require(disabled.result == GWIPC_FRAME_ACCEPTED &&
              disabled.disposition ==
                  gw::compositor::PresentedFrame::Disposition::Complete &&
              compositor.accepted_frames() == 2 &&
              disabled.vrr_response.size() == 3 &&
              !observed->requests.at(2).desired_enabled &&
              observed->requests.at(2).transition_serial == 2 &&
              observed->hashes.at(0) == observed->hashes.at(1) &&
              observed->hashes.at(1) == observed->hashes.at(2),
          "unchanged pixels still present the policy transition after retry: " +
              error);

  stage(compositor, GWIPC_VRR_POLICY_OFF, 2, true, error, 101);
  const auto replaced = compositor.commit(frame(4), error);
  require(replaced.result == GWIPC_FRAME_ACCEPTED &&
              replaced.disposition ==
                  gw::compositor::PresentedFrame::Disposition::Complete &&
              replaced.vrr_response.size() == 4 &&
              replaced.vrr_response[0].type ==
                  GWIPC_MESSAGE_OUTPUT_VRR_STATE_UPSERT &&
              replaced.vrr_response[1].type ==
                  GWIPC_MESSAGE_PRESENTATION_TIMING &&
              replaced.vrr_response[2].type ==
                  GWIPC_MESSAGE_FRAME_ACKNOWLEDGED &&
              replaced.vrr_response[3].type == GWIPC_MESSAGE_BUFFER_RELEASE &&
              is_release(replaced.vrr_response[3], 100,
                         GWIPC_BUFFER_RELEASE_REPLACED),
          "replacement M14 response releases only the old buffer as replaced: " +
              error);
}
