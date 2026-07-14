#include "backends/output/presentation_backend.hpp"
#include "gwcomp/compositor.hpp"

#include "tests/helpers/test_support.hpp"

#include <glasswyrm/ipc.h>

#include <fcntl.h>
#include <poll.h>
#include <sys/mman.h>
#include <unistd.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace {

using glasswyrm::output::BackendEvent;
using glasswyrm::output::BackendEventKind;
using glasswyrm::output::PresentDisposition;
using glasswyrm::output::PresentResult;

struct FakeState {
  int pipe_fds[2]{-1, -1};
  std::vector<PresentDisposition> dispositions;
  std::size_t present_count{};
  std::size_t staged_diagnostics{};
  std::size_t committed_diagnostics{};
  std::size_t aborted_diagnostics{};
  std::uint64_t pending_token{};
  std::uint64_t pending_hash{};
  std::optional<std::uint64_t> completion_token;
  std::optional<std::uint64_t> completion_hash;
  BackendEventKind event_kind{BackendEventKind::Complete};
  bool pending_stage{};
  bool shutdown{};

  FakeState() {
    gw::test::require(::pipe2(pipe_fds, O_CLOEXEC | O_NONBLOCK) == 0,
                      "fake presenter event pipe");
  }
  ~FakeState() {
    if (pipe_fds[0] >= 0) (void)::close(pipe_fds[0]);
    if (pipe_fds[1] >= 0) (void)::close(pipe_fds[1]);
  }
  void signal() const {
    const std::uint8_t byte = 1;
    gw::test::require(::write(pipe_fds[1], &byte, sizeof(byte)) == 1,
                      "signal fake presentation completion");
  }
};

class FakePresenter final
    : public glasswyrm::output::PresentationBackend {
 public:
  explicit FakePresenter(std::shared_ptr<FakeState> state)
      : state_(std::move(state)) {}

  PresentResult present(
      const glasswyrm::output::SoftwareFrameView& frame) override {
    ++state_->present_count;
    ++state_->staged_diagnostics;
    const auto disposition =
        state_->dispositions.at(state_->present_count - 1);
    const auto hash =
        glasswyrm::output::hash_visible_xrgb8888(frame.pixels);
    if (disposition == PresentDisposition::Complete) {
      ++state_->committed_diagnostics;
      return {disposition, 0, hash, {}};
    }
    if (disposition == PresentDisposition::Pending) {
      state_->pending_token = 100 + state_->present_count;
      state_->pending_hash = hash;
      state_->pending_stage = true;
      return {disposition, state_->pending_token, 0, {}};
    }
    ++state_->aborted_diagnostics;
    return {disposition, 0, 0, "injected presentation rejection"};
  }

  int poll_fd() const noexcept override { return state_->pipe_fds[0]; }
  short poll_events() const noexcept override { return POLLIN; }
  BackendEvent service(const short revents) override {
    if ((revents & POLLIN) == 0) return {};
    std::uint8_t bytes[8];
    while (::read(state_->pipe_fds[0], bytes, sizeof(bytes)) > 0) {}
    if (state_->event_kind == BackendEventKind::Fatal) {
      return {BackendEventKind::Fatal, 0, 0, "injected backend fatal"};
    }
    return {BackendEventKind::Complete,
            state_->completion_token.value_or(state_->pending_token),
            state_->completion_hash.value_or(state_->pending_hash), {}};
  }
  bool finalize_pending(const std::uint64_t token,
                        std::string& error) override {
    gw::test::require(token == state_->pending_token && state_->pending_stage,
                      "finalize matching staged diagnostics");
    state_->pending_stage = false;
    ++state_->committed_diagnostics;
    error.clear();
    return true;
  }
  void abort_pending(const std::uint64_t token,
                     const std::string_view) noexcept override {
    if (state_->pending_stage && token == state_->pending_token) {
      state_->pending_stage = false;
      ++state_->aborted_diagnostics;
    }
  }
  glasswyrm::output::BackendStateResult suspend(std::string& error) override {
    error.clear();
    return glasswyrm::output::BackendStateResult::Complete;
  }
  PresentResult resume(
      const glasswyrm::output::SoftwareFrameView& frame) override {
    return present(frame);
  }
  glasswyrm::output::BackendStateResult shutdown(
      std::string& error) noexcept override {
    if (state_->pending_stage) {
      state_->pending_stage = false;
      ++state_->aborted_diagnostics;
    }
    state_->shutdown = true;
    error.clear();
    return glasswyrm::output::BackendStateResult::Complete;
  }

 private:
  std::shared_ptr<FakeState> state_;
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
  value.surface_id = 1;
  value.output_id = 1;
  value.logical_width = value.logical_height = 2;
  value.visible = 1;
  value.transform = GWIPC_TRANSFORM_NORMAL;
  value.opacity = GWIPC_OPACITY_ONE;
  value.scale_numerator = value.scale_denominator = 1;
  value.color = srgb();
  return value;
}

gwipc_frame_commit frame(const std::uint64_t id) {
  gwipc_frame_commit value{};
  value.struct_size = sizeof(value);
  value.commit_id = id;
  value.output_id = 1;
  value.producer_generation = id;
  return value;
}

int buffer_fd(const std::uint32_t pixel) {
  constexpr std::size_t size = 2U * 2U * sizeof(std::uint32_t);
  const int fd = ::memfd_create("gwcomp-async-test",
                                MFD_CLOEXEC | MFD_ALLOW_SEALING);
  gw::test::require(fd >= 0 && ::ftruncate(fd, size) == 0,
                    "create fake producer buffer");
  const std::uint32_t pixels[4]{pixel, pixel, pixel, pixel};
  gw::test::require(::pwrite(fd, pixels, size, 0) ==
                        static_cast<ssize_t>(size) &&
                        ::fcntl(fd, F_ADD_SEALS,
                                F_SEAL_SHRINK | F_SEAL_GROW) == 0,
                    "populate and seal fake producer buffer");
  return fd;
}

bool snapshot(gw::compositor::Compositor& compositor,
              const std::uint64_t buffer_id, const std::uint32_t pixel,
              std::string& error) {
  const auto staged_surface = surface();
  gwipc_buffer_attach attach{};
  attach.struct_size = sizeof(attach);
  attach.buffer_id = buffer_id;
  attach.surface_id = staged_surface.surface_id;
  attach.width = attach.height = 2;
  attach.stride = 8;
  attach.storage_size = 16;
  attach.pixel_format = GWIPC_PIXEL_FORMAT_XRGB8888;
  attach.alpha_semantics = GWIPC_ALPHA_OPAQUE;
  attach.color = srgb();
  return compositor.begin_snapshot() && compositor.apply(output()) &&
         compositor.apply(staged_surface) &&
         compositor.attach(attach, buffer_fd(pixel), error) &&
         compositor.end_snapshot();
}

short ready_events(const FakeState& state) {
  pollfd descriptor{state.pipe_fds[0], POLLIN, 0};
  gw::test::require(::poll(&descriptor, 1, 100) == 1,
                    "fake backend event FD becomes readable");
  return descriptor.revents;
}

}  // namespace

int main() {
  using gw::compositor::PresentationCompletionKind;
  using Disposition = gw::compositor::PresentedFrame::Disposition;
  std::string error;

  auto state = std::make_shared<FakeState>();
  state->dispositions = {PresentDisposition::Complete,
                         PresentDisposition::Pending,
                         PresentDisposition::Pending};
  gw::compositor::Compositor compositor(
      std::make_unique<FakePresenter>(state));
  gw::test::require(snapshot(compositor, 11, 0xff112233U, error),
                    "initial producer snapshot stages");
  const auto initial = compositor.commit(frame(1), error);
  gw::test::require(initial.disposition == Disposition::Complete &&
                        initial.result == GWIPC_FRAME_ACCEPTED &&
                        compositor.accepted_frames() == 1 &&
                        state->committed_diagnostics == 1,
                    "blocking initial modeset completes synchronously");

  gw::test::require(snapshot(compositor, 12, 0xff445566U, error),
                    "replacement snapshot stages");
  const auto pending = compositor.commit(frame(2), error);
  gw::test::require(pending.disposition == Disposition::Pending &&
                        pending.result == GWIPC_FRAME_ACCEPTED &&
                        compositor.presentation_pending() &&
                        compositor.accepted_frames() == 1 &&
                        compositor.releases().empty() &&
                        state->committed_diagnostics == 1 &&
                        state->pending_stage,
                    "pending flip has no early promotion release count or diagnostic commit");
  gwipc_surface_damage damage{};
  damage.struct_size = sizeof(damage);
  damage.surface_id = 1;
  gw::test::require(!compositor.apply(damage) &&
                        compositor.commit(frame(3), error).disposition ==
                            Disposition::Rejected,
                    "later application contracts cannot overtake pending frame");
  gw::test::require(compositor.service_presentation(0, error).kind ==
                        PresentationCompletionKind::None,
                    "no completion is invented before event readiness");

  state->signal();
  const auto completed =
      compositor.service_presentation(ready_events(*state), error);
  gw::test::require(completed.kind == PresentationCompletionKind::Complete &&
                        completed.commit.commit_id == 2 &&
                        completed.frame.disposition == Disposition::Complete &&
                        completed.frame.ordinal == 2 &&
                        compositor.accepted_frames() == 2 &&
                        compositor.releases().at(11) ==
                            GWIPC_BUFFER_RELEASE_REPLACED &&
                        state->committed_diagnostics == 2,
                    "matching event atomically promotes frame diagnostics and release set");
  compositor.clear_releases();

  gw::test::require(snapshot(compositor, 13, 0xff778899U, error),
                    "token mismatch snapshot stages");
  gw::test::require(compositor.commit(frame(3), error).disposition ==
                        Disposition::Pending,
                    "third frame becomes pending");
  state->completion_token = state->pending_token + 1;
  state->signal();
  const auto mismatch =
      compositor.service_presentation(ready_events(*state), error);
  gw::test::require(mismatch.kind == PresentationCompletionKind::Fatal &&
                        compositor.accepted_frames() == 2 && !state->shutdown &&
                        state->aborted_diagnostics == 1,
                    "token mismatch aborts staged diagnostics before restore");
  gw::test::require(compositor.shutdown_presentation(error) &&
                        state->shutdown,
                    "fatal presentation restores through explicit shutdown");

  auto now = gw::compositor::PresentationTiming::Clock::time_point{};
  auto timeout_state = std::make_shared<FakeState>();
  timeout_state->dispositions = {PresentDisposition::Pending};
  gw::compositor::PresentationTiming timing;
  timing.timeout = std::chrono::milliseconds(2000);
  timing.now = [&now] { return now; };
  gw::compositor::Compositor timeout_compositor(
      std::make_unique<FakePresenter>(timeout_state), std::nullopt, timing);
  gw::test::require(snapshot(timeout_compositor, 21, 0xff010203U, error) &&
                        timeout_compositor.commit(frame(1), error).disposition ==
                            Disposition::Pending &&
                        timeout_compositor.presentation_timeout_ms() == 2000,
                    "2000ms pending deadline is injectable");
  now += std::chrono::milliseconds(1999);
  gw::test::require(timeout_compositor.presentation_timeout_ms() == 1 &&
                        timeout_compositor.service_presentation(0, error).kind ==
                            PresentationCompletionKind::None,
                    "frame remains pending before deadline");
  now += std::chrono::milliseconds(1);
  gw::test::require(timeout_compositor.service_presentation(0, error).kind ==
                            PresentationCompletionKind::Fatal &&
                        timeout_compositor.accepted_frames() == 0 &&
                        timeout_state->aborted_diagnostics == 1,
                    "timeout is fatal without false completion accounting");

  auto fatal_state = std::make_shared<FakeState>();
  fatal_state->dispositions = {PresentDisposition::Pending};
  fatal_state->event_kind = BackendEventKind::Fatal;
  gw::compositor::Compositor fatal_compositor(
      std::make_unique<FakePresenter>(fatal_state));
  gw::test::require(snapshot(fatal_compositor, 25, 0xff010203U, error) &&
                        fatal_compositor.commit(frame(1), error).disposition ==
                            Disposition::Pending,
                    "backend-fatal snapshot becomes pending");
  fatal_state->signal();
  gw::test::require(
      fatal_compositor.service_presentation(ready_events(*fatal_state), error)
                  .kind == PresentationCompletionKind::Fatal &&
          fatal_compositor.accepted_frames() == 0 &&
          fatal_state->aborted_diagnostics == 1,
      "backend fatal event aborts without acknowledgement accounting");

  auto hup_state = std::make_shared<FakeState>();
  hup_state->dispositions = {PresentDisposition::Pending};
  gw::compositor::Compositor hup_compositor(
      std::make_unique<FakePresenter>(hup_state));
  gw::test::require(snapshot(hup_compositor, 31, 0xff010203U, error) &&
                        hup_compositor.commit(frame(1), error).disposition ==
                            Disposition::Pending &&
                        hup_compositor.service_presentation(POLLHUP, error).kind ==
                            PresentationCompletionKind::Fatal &&
                        hup_state->aborted_diagnostics == 1,
                    "backend HUP is fatal and aborts staged diagnostics");

  auto rejected_state = std::make_shared<FakeState>();
  rejected_state->dispositions = {PresentDisposition::Rejected};
  gw::compositor::Compositor rejected(
      std::make_unique<FakePresenter>(rejected_state));
  gw::test::require(snapshot(rejected, 41, 0xff010203U, error) &&
                        rejected.commit(frame(1), error).disposition ==
                            Disposition::Rejected &&
                        !rejected.presentation_pending() &&
                        rejected.accepted_frames() == 0 &&
                        rejected_state->aborted_diagnostics == 1,
                    "pre-submission rejection preserves committed state and frame count");

  auto immediate_fatal_state = std::make_shared<FakeState>();
  immediate_fatal_state->dispositions = {PresentDisposition::Fatal};
  gw::compositor::Compositor immediate_fatal(
      std::make_unique<FakePresenter>(immediate_fatal_state));
  gw::test::require(
      snapshot(immediate_fatal, 51, 0xff010203U, error) &&
          immediate_fatal.commit(frame(1), error).disposition ==
              Disposition::Fatal &&
          immediate_fatal.accepted_frames() == 0,
      "fatal submission result never promotes or counts a frame");

  auto vt_state = std::make_shared<FakeState>();
  vt_state->dispositions = {PresentDisposition::Complete,
                            PresentDisposition::Complete};
  gw::compositor::Compositor vt_compositor(
      std::make_unique<FakePresenter>(vt_state));
  gw::test::require(snapshot(vt_compositor, 61, 0xffabcdefU, error) &&
                        vt_compositor.commit(frame(1), error).disposition ==
                            Disposition::Complete &&
                        vt_compositor.suspend_presentation(error) &&
                        vt_compositor.presentation_suspended(),
                    "completed presentation may enter suspended state");
  gw::test::require(vt_compositor.resume_presentation(error) &&
                        !vt_compositor.presentation_suspended() &&
                        vt_compositor.accepted_frames() == 1,
                    "resume re-presents without a new producer frame count");
  return 0;
}
