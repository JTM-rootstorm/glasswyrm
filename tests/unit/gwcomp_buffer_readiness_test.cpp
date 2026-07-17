#include "backends/output/presentation_backend.hpp"
#include "gwcomp/compositor.hpp"
#include "tests/helpers/test_support.hpp"

#include <fcntl.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/mman.h>
#include <unistd.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

namespace {

struct PresentedState {
  std::size_t count{};
  std::uint32_t first_pixel{};
};

class ImmediatePresenter final
    : public glasswyrm::output::PresentationBackend {
 public:
  explicit ImmediatePresenter(std::shared_ptr<PresentedState> state)
      : state_(std::move(state)) {}

  glasswyrm::output::PresentResult present(
      const glasswyrm::output::SoftwareFrameView& frame) override {
    ++state_->count;
    state_->first_pixel = frame.pixels.empty() ? 0 : frame.pixels.front();
    return {glasswyrm::output::PresentDisposition::Complete, 0,
            glasswyrm::output::hash_visible_xrgb8888(frame.pixels), {}};
  }
  int poll_fd() const noexcept override { return -1; }
  short poll_events() const noexcept override { return 0; }
  glasswyrm::output::BackendEvent service(short) override { return {}; }
  glasswyrm::output::BackendStateResult suspend(std::string& error) override {
    error.clear();
    return glasswyrm::output::BackendStateResult::Complete;
  }
  glasswyrm::output::PresentResult resume(
      const glasswyrm::output::SoftwareFrameView& frame) override {
    return present(frame);
  }
  glasswyrm::output::BackendStateResult shutdown(
      std::string& error) noexcept override {
    error.clear();
    return glasswyrm::output::BackendStateResult::Complete;
  }

 private:
  std::shared_ptr<PresentedState> state_;
};

gwipc_sdr_color_metadata srgb() {
  return {GWIPC_SDR_COLOR_SPACE_SRGB, GWIPC_TRANSFER_FUNCTION_SRGB,
          GWIPC_COLOR_PRIMARIES_SRGB, 0, 0, 0, 0};
}

gwipc_frame_commit frame() {
  gwipc_frame_commit value{};
  value.struct_size = sizeof(value);
  value.commit_id = value.producer_generation = value.output_id = 1;
  return value;
}

int pixel_buffer(const std::uint32_t pixel) {
  const int fd = ::memfd_create("gwcomp-readiness-test",
                                MFD_CLOEXEC | MFD_ALLOW_SEALING);
  const std::uint32_t pixels[4]{pixel, pixel, pixel, pixel};
  gw::test::require(
      fd >= 0 && ::ftruncate(fd, sizeof(pixels)) == 0 &&
          ::pwrite(fd, pixels, sizeof(pixels), 0) ==
              static_cast<ssize_t>(sizeof(pixels)) &&
          ::fcntl(fd, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_GROW) == 0,
      "create synchronized pixel buffer");
  return fd;
}

bool stage(gw::compositor::Compositor& compositor, const int pixel_fd,
           const int readiness_fd, const gwipc_synchronization_mode mode,
           std::string& error) {
  gwipc_output_upsert output{};
  output.struct_size = sizeof(output);
  output.output_id = 1;
  output.enabled = 1;
  output.logical_width = output.logical_height = 2;
  output.physical_pixel_width = output.physical_pixel_height = 2;
  output.refresh_millihertz = 60'000;
  output.scale_numerator = output.scale_denominator = 1;
  output.transform = GWIPC_TRANSFORM_NORMAL;
  output.color = srgb();

  gwipc_surface_upsert surface{};
  surface.struct_size = sizeof(surface);
  surface.surface_id = 1;
  surface.x11_window_id = 100;
  surface.output_id = 1;
  surface.logical_width = surface.logical_height = 2;
  surface.visible = 1;
  surface.opacity = GWIPC_OPACITY_ONE;
  surface.scale_numerator = surface.scale_denominator = 1;
  surface.transform = GWIPC_TRANSFORM_NORMAL;
  surface.color = srgb();

  gwipc_surface_policy_upsert policy{};
  policy.struct_size = sizeof(policy);
  policy.surface_id = 1;
  policy.x11_window_id = 100;
  policy.workspace_id = 1;
  policy.window_type = GWIPC_POLICY_WINDOW_NORMAL;
  policy.applied_state = GWIPC_POLICY_APPLIED_NORMAL;
  policy.managed = 1;
  policy.decoration_eligible = 1;

  gwipc_buffer_attach attachment{};
  attachment.struct_size = sizeof(attachment);
  attachment.buffer_id = 1;
  attachment.surface_id = 1;
  attachment.width = attachment.height = 2;
  attachment.stride = 8;
  attachment.storage_size = 16;
  attachment.pixel_format = GWIPC_PIXEL_FORMAT_XRGB8888;
  attachment.alpha_semantics = GWIPC_ALPHA_OPAQUE;
  attachment.color = srgb();
  attachment.synchronization = mode;

  const gwipc_damage_rectangle rectangle{0, 0, 2, 2};
  gwipc_surface_damage damage{};
  damage.struct_size = sizeof(damage);
  damage.surface_id = 1;
  damage.rectangles = &rectangle;
  damage.rectangle_count = 1;

  const int imported_pixel = ::fcntl(pixel_fd, F_DUPFD_CLOEXEC, 0);
  const int imported_readiness =
      readiness_fd >= 0 ? ::fcntl(readiness_fd, F_DUPFD_CLOEXEC, 0) : -1;
  return imported_pixel >= 0 &&
         (readiness_fd < 0 || imported_readiness >= 0) &&
         compositor.begin_snapshot() && compositor.apply(output) &&
         compositor.apply(surface) && compositor.apply(policy) &&
         compositor.attach(attachment, imported_pixel, imported_readiness,
                           error) &&
         compositor.apply(damage) && compositor.end_snapshot();
}

void readiness_defers_all_buffer_reads() {
  auto state = std::make_shared<PresentedState>();
  gw::compositor::Compositor compositor(
      std::make_unique<ImmediatePresenter>(state));
  compositor.set_peer_profile(
      gw::compositor::PeerProfile::M7BufferedProtocolServer);
  compositor.set_cpu_buffer_synchronization(true);
  const int pixels = pixel_buffer(0xff102030U);
  const int readiness = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  std::string error;
  gw::test::require(
      readiness >= 0 &&
          stage(compositor, pixels, readiness, GWIPC_SYNCHRONIZATION_EVENTFD,
                error) &&
          compositor.commit(frame(), error).disposition ==
              gw::compositor::PresentedFrame::Disposition::Pending &&
          state->count == 0 && compositor.presentation_poll_fd() >= 0,
      "unsignaled eventfd stages the frame without compositor reads");

  const std::uint32_t replacement[4]{0xff405060U, 0xff405060U, 0xff405060U,
                                     0xff405060U};
  const std::uint64_t token = 1;
  gw::test::require(
      ::pwrite(pixels, replacement, sizeof(replacement), 0) ==
              static_cast<ssize_t>(sizeof(replacement)) &&
          ::write(readiness, &token, sizeof(token)) ==
              static_cast<ssize_t>(sizeof(token)),
      "producer publishes pixels before one readiness token");
  const auto completion = compositor.service_presentation(POLLIN, error);
  gw::test::require(
      completion.kind ==
              gw::compositor::PresentationCompletionKind::Complete &&
          state->count == 1 && state->first_pixel == 0xff405060U,
      "compositor reads the synchronized buffer only after readiness");
  (void)::close(readiness);
  (void)::close(pixels);
}

void none_and_fatal_paths() {
  std::string error;
  {
    auto state = std::make_shared<PresentedState>();
    gw::compositor::Compositor compositor(
        std::make_unique<ImmediatePresenter>(state));
    compositor.set_peer_profile(
        gw::compositor::PeerProfile::M7BufferedProtocolServer);
    const int pixels = pixel_buffer(0xff112233U);
    gw::test::require(
        stage(compositor, pixels, -1, GWIPC_SYNCHRONIZATION_NONE, error) &&
            compositor.commit(frame(), error).disposition ==
                gw::compositor::PresentedFrame::Disposition::Complete &&
            state->count == 1,
        "historical synchronization-none buffers remain immediate");
    (void)::close(pixels);
  }
  {
    auto state = std::make_shared<PresentedState>();
    gw::compositor::Compositor compositor(
        std::make_unique<ImmediatePresenter>(state));
    compositor.set_peer_profile(
        gw::compositor::PeerProfile::M7BufferedProtocolServer);
    compositor.set_cpu_buffer_synchronization(true);
    const int pixels = pixel_buffer(0xff112233U);
    const int readiness = ::eventfd(2, EFD_NONBLOCK | EFD_CLOEXEC);
    gw::test::require(
        readiness >= 0 &&
            stage(compositor, pixels, readiness,
                  GWIPC_SYNCHRONIZATION_EVENTFD, error) &&
            compositor.commit(frame(), error).disposition ==
                gw::compositor::PresentedFrame::Disposition::Fatal &&
            state->count == 0,
        "extra readiness tokens are producer-protocol fatal");
    (void)::close(readiness);
    (void)::close(pixels);
  }
}

void bounded_wait_failures() {
  std::string error;
  {
    auto now = gw::compositor::PresentationTiming::Clock::time_point{};
    gw::compositor::PresentationTiming timing;
    timing.timeout = std::chrono::milliseconds(5);
    timing.now = [&now] { return now; };
    auto state = std::make_shared<PresentedState>();
    gw::compositor::Compositor compositor(
        std::make_unique<ImmediatePresenter>(state), std::nullopt, timing);
    compositor.set_peer_profile(
        gw::compositor::PeerProfile::M7BufferedProtocolServer);
    compositor.set_cpu_buffer_synchronization(true);
    const int pixels = pixel_buffer(0xff112233U);
    const int readiness = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    gw::test::require(
        stage(compositor, pixels, readiness,
              GWIPC_SYNCHRONIZATION_EVENTFD, error) &&
            compositor.commit(frame(), error).disposition ==
                gw::compositor::PresentedFrame::Disposition::Pending,
        "readiness timeout fixture begins pending");
    now += std::chrono::milliseconds(5);
    gw::test::require(
        compositor.service_presentation(0, error).kind ==
                gw::compositor::PresentationCompletionKind::Fatal &&
            state->count == 0,
        "bounded readiness timeout is fatal without rendering");
    (void)::close(readiness);
    (void)::close(pixels);
  }
  {
    auto state = std::make_shared<PresentedState>();
    gw::compositor::Compositor compositor(
        std::make_unique<ImmediatePresenter>(state));
    compositor.set_peer_profile(
        gw::compositor::PeerProfile::M7BufferedProtocolServer);
    compositor.set_cpu_buffer_synchronization(true);
    const int pixels = pixel_buffer(0xff112233U);
    const int readiness = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    gw::test::require(
        stage(compositor, pixels, readiness,
              GWIPC_SYNCHRONIZATION_EVENTFD, error) &&
            compositor.commit(frame(), error).disposition ==
                gw::compositor::PresentedFrame::Disposition::Pending &&
            compositor.service_presentation(POLLHUP, error).kind ==
                gw::compositor::PresentationCompletionKind::Fatal &&
            state->count == 0,
        "readiness descriptor failure is fatal without rendering");
    (void)::close(readiness);
    (void)::close(pixels);
  }
}

}  // namespace

int main() {
  readiness_defers_all_buffer_reads();
  none_and_fatal_paths();
  bounded_wait_failures();
  return 0;
}
