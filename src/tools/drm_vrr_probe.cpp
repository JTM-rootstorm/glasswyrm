#include "tools/drm_vrr_probe.hpp"

#include "backends/drm/connector_name.hpp"
#include "backends/drm/connector_selector.hpp"
#include "backends/drm/dumb_buffer.hpp"
#include "backends/drm/kms_state.hpp"
#include "backends/drm/kms_vrr_state.hpp"
#include "backends/drm/mode_selector.hpp"
#include "backends/drm/pipeline_selector.hpp"

#include <algorithm>
#include <charconv>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <memory>
#include <poll.h>
#include <set>
#include <span>
#include <sstream>
#include <string_view>
#include <time.h>
#include <unistd.h>
#include <vector>

namespace glasswyrm::tools {
namespace {

constexpr std::string_view kSchema = "glasswyrm.m14-nvidia-vrr-probe.v1";

class Report {
 public:
  ~Report() { if (fd_ >= 0) ::close(fd_); }
  bool open(const std::string& path, std::string& error) {
    fd_ = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (fd_ >= 0) return true;
    error = "cannot create probe report: " + std::string(std::strerror(errno));
    return false;
  }
  bool append(const std::string_view value, std::string& error) {
    std::size_t offset{};
    while (offset < value.size()) {
      const auto written = ::write(fd_, value.data() + offset,
                                   value.size() - offset);
      if (written <= 0) {
        error = "cannot append probe report: " +
                std::string(std::strerror(errno));
        return false;
      }
      offset += static_cast<std::size_t>(written);
    }
    if (::fsync(fd_) == 0) return true;
    error = "cannot sync probe report: " + std::string(std::strerror(errno));
    return false;
  }
 private:
  int fd_{-1};
};

void json_string(std::ostream& output, const std::string_view value) {
  output << '"';
  for (const unsigned char byte : value) {
    if (byte == '"' || byte == '\\') output << '\\' << byte;
    else if (byte == '\n') output << "\\n";
    else if (byte >= 0x20) output << static_cast<char>(byte);
  }
  output << '"';
}

bool parse_unsigned(const std::string_view text, std::uint32_t& value,
                    const std::uint32_t minimum,
                    const std::uint32_t maximum) {
  const auto [end, status] =
      std::from_chars(text.data(), text.data() + text.size(), value);
  return status == std::errc{} && end == text.data() + text.size() &&
         value >= minimum && value <= maximum;
}

bool take(int argc, char** argv, int& index, std::string& value,
          std::ostream& error, const std::string_view option) {
  if (++index >= argc || argv[index][0] == '\0' ||
      std::string_view(argv[index]).starts_with("--")) {
    error << "gw_drm_vrr_probe: " << option << " requires a value\n";
    return false;
  }
  value = argv[index];
  return true;
}

void usage(std::ostream& output) {
  output << "Usage: gw_drm_vrr_probe --device /dev/dri/cardN "
            "--connector NAME --mode WIDTHxHEIGHT@MILLIHERTZ\n"
            "       --run-id 32-LOWERCASE-HEX --output NEW.jsonl "
            "[--target-hz N] [--warmup N] [--samples N]\n";
}

struct Selection {
  drm::PipelineIds pipeline;
  drm::Mode mode;
  const drm::Connector* connector{};
  std::vector<std::uint32_t> live_connectors;
};

bool select(const drm::DeviceSnapshot& snapshot,
            const DrmVrrProbeOptions& options, Selection& result,
            std::string& error) {
  const auto separator = options.mode.find('x');
  const auto refresh_separator = options.mode.find('@');
  std::uint32_t width{}, height{}, refresh{};
  if (separator == std::string::npos || refresh_separator == std::string::npos ||
      separator >= refresh_separator ||
      !parse_unsigned(std::string_view(options.mode).substr(0, separator),
                      width, 1, 16384) ||
      !parse_unsigned(std::string_view(options.mode).substr(
                          separator + 1, refresh_separator - separator - 1),
                      height, 1, 16384) ||
      !parse_unsigned(std::string_view(options.mode).substr(
                          refresh_separator + 1),
                      refresh, 1, 1'000'000)) {
    error = "probe mode must match WIDTHxHEIGHT@MILLIHERTZ";
    return false;
  }
  const auto connector = drm::select_connector(
      snapshot.connectors, snapshot.crtcs, width, height, options.connector);
  if (connector.status != drm::ConnectorSelectionStatus::Success) {
    error = "probe connector selection failed";
    return false;
  }
  const auto& selected = snapshot.connectors[connector.connector_index];
  const auto mode = drm::select_mode(selected.modes,
                                     {width, height, refresh, std::nullopt});
  const auto crtc = drm::select_crtc(selected, snapshot.crtcs);
  if (mode.status != drm::ModeSelectionStatus::Success ||
      crtc.status != drm::CrtcSelectionStatus::Success) {
    error = "probe mode or CRTC selection failed";
    return false;
  }
  const auto plane = drm::select_primary_plane(snapshot.crtcs[crtc.crtc_index],
                                                snapshot.planes);
  if (plane.status != drm::PlaneSelectionStatus::Success) {
    error = "probe primary-plane selection failed";
    return false;
  }
  result.pipeline = {selected.id, snapshot.crtcs[crtc.crtc_index].id,
                     snapshot.planes[plane.plane_index].id};
  result.mode = selected.modes[mode.mode_index];
  result.connector = &selected;
  result.live_connectors = snapshot.crtcs[crtc.crtc_index].connector_ids;
  return true;
}

std::string start_record(const DrmVrrProbeOptions& options,
                         const Selection& selected,
                         const drm::KmsVrrState& vrr) {
  std::ostringstream output;
  output << "{\"schema\":"; json_string(output, kSchema);
  output << ",\"record\":\"probe-start\",\"run_id\":";
  json_string(output, options.run_id);
  output << ",\"connector\":"; json_string(output, options.connector);
  output << ",\"crtc_id\":" << selected.pipeline.crtc << ",\"mode\":";
  json_string(output, options.mode);
  output << ",\"hardware_capable\":" << (vrr.hardware_capable ? "true" : "false")
         << ",\"atomic_test_off\":" << (vrr.test_off_passed ? "true" : "false")
         << ",\"atomic_test_on\":" << (vrr.test_on_passed ? "true" : "false")
         << ",\"target_refresh_hz\":" << options.target_refresh_hz
         << ",\"warmup_flips\":" << options.warmup_flips
         << ",\"recorded_flips\":" << options.recorded_flips
         << ",\"restore_confirmation_flips\":1}\n";
  return output.str();
}

std::string query_json(const drm::CrtcSequenceSample& sample,
                       const drm::VrrTimingSource source) {
  if (sample.source != source) return "{}";
  std::ostringstream output;
  output << "{\"sequence\":" << sample.sequence
         << ",\"timestamp_nanoseconds\":" << sample.timestamp_nanoseconds
         << ",\"cadence_eligible\":"
         << (sample.cadence_eligible ? "true" : "false") << '}';
  return output.str();
}

std::string flip_record(const DrmVrrProbeOptions& options,
                        const std::string_view phase, const std::uint32_t ordinal,
                        const bool recorded, const std::uint32_t framebuffer,
                        const std::uint64_t token, const bool requested,
                        const bool readback, const drm::DrmEvent& event,
                        const std::optional<std::uint64_t> previous,
                        const std::uint64_t transition,
                        const std::uint64_t deadline,
                        const std::uint64_t submitted,
                        const std::uint64_t dequeued,
                        const std::string_view atomic_status,
                        const int atomic_errno,
                        const std::string_view readback_status) {
  const auto timestamp = event.timestamp_available
                             ? event.kernel_timestamp_nanoseconds : 0;
  std::ostringstream output;
  output << "{\"schema\":"; json_string(output, kSchema);
  output << ",\"record\":\"flip\",\"run_id\":";
  json_string(output, options.run_id);
  output << ",\"phase\":"; json_string(output, phase);
  output << ",\"ordinal\":" << ordinal << ",\"sample_kind\":\""
         << (recorded ? "recorded" : "warmup") << "\",\"framebuffer_id\":"
         << framebuffer << ",\"atomic_request_token\":" << token
         << ",\"requested_vrr_enabled\":" << (requested ? "true" : "false")
         << ",\"readback_vrr_enabled\":" << (readback ? "true" : "false")
         << ",\"crtc_id\":" << event.crtc_id
         << ",\"raw_event_sequence\":" << event.sequence
         << ",\"raw_event_seconds\":" << timestamp / 1'000'000'000ULL
         << ",\"raw_event_microseconds\":"
         << timestamp % 1'000'000'000ULL / 1'000ULL
         << ",\"raw_kernel_timestamp_nanoseconds\":" << timestamp
         << ",\"raw_timestamp_available\":"
         << (event.timestamp_available ? "true" : "false")
         << ",\"raw_timestamp_invalid_reason\":\""
         << (event.timestamp_available ? "" : "unavailable-or-regressed")
         << "\",\"raw_interval_nanoseconds\":";
  if (event.timestamp_available && previous)
    output << timestamp - *previous;
  else
    output << "null";
  output << ",\"transition_serial\":" << transition
         << ",\"submit_deadline_nanoseconds\":" << deadline
         << ",\"submit_monotonic_timestamp_nanoseconds\":" << submitted
         << ",\"completion_dequeue_timestamp_nanoseconds\":" << dequeued
         << ",\"crtc_sequence_query\":"
         << query_json(event.crtc_sequence_sample,
                       drm::VrrTimingSource::CrtcSequenceQuery)
         << ",\"legacy_vblank_query\":"
         << query_json(event.crtc_sequence_sample,
                       drm::VrrTimingSource::LegacyVBlankQuery)
         << ",\"atomic_status\":"; json_string(output, atomic_status);
  output << ",\"atomic_errno\":" << atomic_errno
         << ",\"property_readback_status\":";
  json_string(output, readback_status);
  output << "}\n";
  return output.str();
}

std::string restore_record(const DrmVrrProbeOptions& options,
                           const bool restored, const std::string_view error) {
  std::ostringstream output;
  output << "{\"schema\":"; json_string(output, kSchema);
  output << ",\"record\":\"restore\",\"run_id\":";
  json_string(output, options.run_id);
  output << ",\"kms_state_equal\":" << (restored ? "true" : "false")
         << ",\"vrr_property_restored\":" << (restored ? "true" : "false")
         << ",\"passed\":" << (restored ? "true" : "false")
         << ",\"error\":"; json_string(output, error); output << "}\n";
  return output.str();
}

class RealPlatform final : public DrmVrrProbePlatform {
 public:
  bool monotonic_nanoseconds(std::uint64_t& value, std::string& error) override {
    timespec time{};
    if (::clock_gettime(CLOCK_MONOTONIC, &time) != 0) {
      error = "CLOCK_MONOTONIC query failed: " +
              std::string(std::strerror(errno));
      return false;
    }
    value = static_cast<std::uint64_t>(time.tv_sec) * 1'000'000'000ULL +
            static_cast<std::uint64_t>(time.tv_nsec);
    return true;
  }
  bool sleep_until(const std::uint64_t deadline, std::string& error) override {
    const timespec time{static_cast<time_t>(deadline / 1'000'000'000ULL),
                        static_cast<long>(deadline % 1'000'000'000ULL)};
    int status{};
    do status = ::clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &time, nullptr);
    while (status == EINTR);
    if (status == 0) return true;
    error = "absolute probe sleep failed: " + std::string(std::strerror(status));
    return false;
  }
  void submitted(std::uint64_t, std::uint32_t, bool, std::uint32_t) override {}
};

}  // namespace

DrmVrrProbeParseResult parse_drm_vrr_probe_options(
    const int argc, char** argv, DrmVrrProbeOptions& options,
    std::ostream& output, std::ostream& error) {
  options = {};
  std::set<std::string> seen;
  for (int index = 1; index < argc; ++index) {
    const std::string argument(argv[index]);
    if (argument == "--help") { usage(output); return DrmVrrProbeParseResult::ExitSuccess; }
    if (!seen.insert(argument).second) {
      error << "gw_drm_vrr_probe: duplicate option: " << argument << '\n';
      return DrmVrrProbeParseResult::ExitFailure;
    }
    std::string value;
    if (argument == "--device" || argument == "--connector" ||
        argument == "--mode" || argument == "--run-id" ||
        argument == "--output") {
      if (!take(argc, argv, index, value, error, argument))
        return DrmVrrProbeParseResult::ExitFailure;
      if (argument == "--device") options.device = value;
      else if (argument == "--connector") options.connector = value;
      else if (argument == "--mode") options.mode = value;
      else if (argument == "--run-id") options.run_id = value;
      else options.output_path = value;
    } else if (argument == "--target-hz" || argument == "--warmup" ||
               argument == "--samples" || argument == "--event-timeout-ms") {
      if (!take(argc, argv, index, value, error, argument))
        return DrmVrrProbeParseResult::ExitFailure;
      std::uint32_t parsed{};
      const auto minimum = argument == "--warmup" ? 0U : 1U;
      const auto maximum = argument == "--event-timeout-ms" ? 30'000U : 512U;
      if (!parse_unsigned(value, parsed, minimum, maximum)) {
        error << "gw_drm_vrr_probe: invalid bounded value for " << argument << '\n';
        return DrmVrrProbeParseResult::ExitFailure;
      }
      if (argument == "--target-hz") options.target_refresh_hz = parsed;
      else if (argument == "--warmup") options.warmup_flips = parsed;
      else if (argument == "--samples") options.recorded_flips = parsed;
      else options.event_timeout_milliseconds = parsed;
    } else {
      error << "gw_drm_vrr_probe: unknown option: " << argument << '\n';
      return DrmVrrProbeParseResult::ExitFailure;
    }
  }
  if (options.device.empty() || options.connector.empty() || options.mode.empty() ||
      options.output_path.empty() || options.run_id.size() != 32 ||
      !std::ranges::all_of(options.run_id, [](const unsigned char value) {
        return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f');
      })) {
    error << "gw_drm_vrr_probe: exact device, connector, mode, run ID, and output are required\n";
    return DrmVrrProbeParseResult::ExitFailure;
  }
  return DrmVrrProbeParseResult::Run;
}

int run_drm_vrr_probe(drm::DrmApi& drm_api, drm::KmsApi& kms,
                      DrmVrrProbePlatform& platform,
                      const DrmVrrProbeOptions& options,
                      std::ostream& errors) {
  Report report;
  std::string error;
  if (!report.open(options.output_path, error)) { errors << error << '\n'; return 1; }
  drm::DeviceDiscovery discovery;
  auto device = drm::Device::open(drm_api, options.device, {}, discovery);
  if (!device) { errors << discovery.error << '\n'; return 1; }
  Selection selected;
  if (!select(device->snapshot(), options, selected, error) ||
      !device->snapshot().atomic || !device->snapshot().timestamp_monotonic) {
    if (error.empty()) error = "probe requires atomic KMS and monotonic timestamps";
    errors << error << '\n'; return 1;
  }
  bool was_master{};
  bool acquired{};
  if (!kms.is_master(device->borrowed_kms_fd(), was_master, error) ||
      (!was_master && !(acquired = kms.acquire_master(
                            device->borrowed_kms_fd(), error)))) {
    errors << error << '\n'; return 1;
  }
  drm::KmsDumbBufferApi dumb_api(kms, device->borrowed_kms_fd());
  drm::DumbBufferPair buffers;
  drm::SavedKmsState saved;
  drm::ModeBlob blob;
  const auto finish_master = [&] {
    std::string ignored;
    if (acquired) (void)kms.drop_master(device->borrowed_kms_fd(), ignored);
  };
  if (!drm::DumbBufferPair::create(dumb_api, selected.mode.width,
                                   selected.mode.height, buffers, error) ||
      !drm::capture_saved_state(kms, device->borrowed_kms_fd(),
                                selected.pipeline, selected.live_connectors,
                                true, saved, error) ||
      !blob.create(kms, device->borrowed_kms_fd(),
                   drm::kms_mode_from_discovered(selected.mode), error)) {
    errors << error << '\n'; finish_master(); return 1;
  }
  auto base = drm::atomic_initial_request(
      selected.pipeline, saved.properties, blob.id(),
      buffers.front().framebuffer_id(), selected.mode.width,
      selected.mode.height, false);
  const auto vrr = drm::probe_kms_vrr_state(
      kms, device->borrowed_kms_fd(), *selected.connector,
      selected.pipeline, saved, base);
  if (!report.append(start_record(options, selected, vrr), error)) {
    errors << error << '\n'; finish_master(); return 1;
  }
  bool display_taken{};
  bool success = vrr.controllable;
  if (!success) error = vrr.diagnostic;
  const auto pixel_count = static_cast<std::size_t>(selected.mode.width) *
                           selected.mode.height;
  std::vector<std::uint32_t> pixels(pixel_count);
  for (std::size_t index = 0; index < pixels.size(); ++index)
    pixels[index] = UINT32_C(0xff000000) |
                    static_cast<std::uint32_t>((index * 2654435761U) & 0xffffffU);
  if (success && (!buffers.front().copy_from(pixels, error) ||
                  !buffers.back().copy_from(pixels, error))) success = false;
  auto initial = drm::make_vrr_atomic_request(base, vrr, false);
  if (success && !kms.atomic_commit(device->borrowed_kms_fd(), initial,
                                    drm::AtomicAllowModeset, nullptr, error))
    success = false;
  if (success) {
    display_taken = true;
    success = drm::verify_kms_vrr_enabled(
        kms, device->borrowed_kms_fd(), selected.pipeline, vrr, false, error);
  }
  std::uint64_t token{1};
  std::uint64_t transition{1};
  auto run_phase = [&](const std::string_view phase, const bool enabled,
                       const std::uint32_t count,
                       const std::uint32_t warmup) -> bool {
    std::optional<std::uint64_t> previous;
    std::uint64_t started{};
    if (!platform.monotonic_nanoseconds(started, error)) return false;
    for (std::uint32_t ordinal = 0; ordinal < count; ++ordinal) {
      const auto deadline = started +
          (static_cast<std::uint64_t>(ordinal + 1) * 1'000'000'000ULL) /
              options.target_refresh_hz;
      if (!platform.sleep_until(deadline, error)) return false;
      auto& target = buffers.back();
      auto cookie = std::make_shared<drm::PageFlipCookie>(token);
      if (!device->arm_page_flip(cookie, error)) return false;
      auto request = drm::atomic_flip_request(
          selected.pipeline, saved.properties, target.framebuffer_id());
      request = drm::make_vrr_atomic_request(request, vrr, enabled);
      std::uint64_t submitted{};
      if (!platform.monotonic_nanoseconds(submitted, error)) return false;
      errno = 0;
      if (!kms.atomic_commit(device->borrowed_kms_fd(), request,
                             drm::AtomicNonblock | drm::AtomicPageFlipEvent,
                             cookie.get(), error)) {
        const int atomic_errno = errno;
        device->cancel_page_flip(cookie);
        drm::DrmEvent failed{drm::DrmEventKind::PageFlip, token,
                             selected.pipeline.crtc, 0};
        bool readback{};
        std::string readback_error;
        const bool readback_ok = drm::read_kms_vrr_enabled(
            kms, device->borrowed_kms_fd(), selected.pipeline, vrr,
            readback, readback_error);
        std::string report_error;
        (void)report.append(flip_record(
            options, phase, ordinal, ordinal >= warmup,
            target.framebuffer_id(), token, enabled, readback, failed,
            previous, transition, deadline, submitted, submitted, "failed",
            atomic_errno, readback_ok ? "success" : "failed"),
            report_error);
        return false;
      }
      platform.submitted(token, selected.pipeline.crtc, enabled, ordinal);
      pollfd descriptor{device->poll_fd(), POLLIN, 0};
      const int ready = ::poll(&descriptor, 1,
                               static_cast<int>(options.event_timeout_milliseconds));
      if (ready != 1) { error = "probe page-flip event timed out"; return false; }
      auto event = device->service_events(descriptor.revents);
      std::uint64_t dequeued{};
      if (!platform.monotonic_nanoseconds(dequeued, error)) return false;
      if (event.kind != drm::DrmEventKind::PageFlip || event.token != token ||
          event.crtc_id != selected.pipeline.crtc) {
        error = "probe page-flip event identity diverged";
        return false;
      }
      bool readback{};
      const bool readback_ok = drm::read_kms_vrr_enabled(
          kms, device->borrowed_kms_fd(), selected.pipeline, vrr,
          readback, error);
      if (!report.append(flip_record(
              options, phase, ordinal, ordinal >= warmup,
              target.framebuffer_id(), token, enabled, readback, event,
              previous, transition, deadline, submitted, dequeued, "success",
              0, readback_ok ? "success" : "failed"), error)) return false;
      if (!readback_ok || readback != enabled) {
        if (error.empty()) error = "probe VRR readback diverged";
        return false;
      }
      if (event.timestamp_available)
        previous = event.kernel_timestamp_nanoseconds;
      buffers.promote_back();
      ++token;
    }
    ++transition;
    return true;
  };
  if (success)
    success = run_phase("off", false,
                        options.warmup_flips + options.recorded_flips,
                        options.warmup_flips);
  if (success)
    success = run_phase("on", true,
                        options.warmup_flips + options.recorded_flips,
                        options.warmup_flips);
  if (success) success = run_phase("restore-off", false, 1, 1);

  std::string restore_error;
  bool restored = true;
  if (display_taken)
    restored = drm::restore_saved_state(kms, device->borrowed_kms_fd(), saved,
                                        restore_error);
  else
    restored = drm::verify_saved_state(kms, device->borrowed_kms_fd(), saved,
                                       restore_error);
  if (!report.append(restore_record(options, restored, restore_error),
                     restore_error)) restored = false;
  std::string release_error;
  if (!buffers.release(release_error)) restored = false;
  blob.reset();
  finish_master();
  if (!success || !restored) {
    errors << (error.empty() ? restore_error : error) << '\n';
    return 1;
  }
  return 0;
}

DrmVrrProbePlatform& real_drm_vrr_probe_platform() {
  static RealPlatform platform;
  return platform;
}

}  // namespace glasswyrm::tools
