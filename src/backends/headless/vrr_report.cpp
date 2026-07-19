#include "backends/headless/vrr_report.hpp"

#include "output/vrr/reasons.hpp"
#include "output/vrr/timing_stats.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <sstream>
#include <unistd.h>
#include <utility>

namespace glasswyrm::headless {
namespace {

std::string boolean(const bool value) { return value ? "true" : "false"; }

std::string reason_names(const output::vrr::ReasonMask mask) {
  std::ostringstream names;
  names << '[';
  bool first = true;
  for (const auto reason : output::vrr::reason_precedence()) {
    if (!output::vrr::has_reason(mask, reason))
      continue;
    if (!first)
      names << ',';
    first = false;
    names << '\"' << output::vrr::reason_name(reason) << '\"';
  }
  names << ']';
  return names.str();
}

} // namespace

VrrReport::VrrReport(VrrReport &&other) noexcept
    : fd_(std::exchange(other.fd_, -1)), finished_(other.finished_),
      summaries_(std::move(other.summaries_)) {}

VrrReport &VrrReport::operator=(VrrReport &&other) noexcept {
  if (this == &other)
    return *this;
  if (fd_ >= 0)
    ::close(fd_);
  fd_ = std::exchange(other.fd_, -1);
  finished_ = other.finished_;
  summaries_ = std::move(other.summaries_);
  return *this;
}

VrrReport::~VrrReport() {
  if (fd_ >= 0)
    ::close(fd_);
}

std::optional<VrrReport> VrrReport::create(const std::filesystem::path &path,
                                           std::string &error) {
  error.clear();
  const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC,
                        0600);
  if (fd < 0) {
    error = "cannot create headless VRR report: " +
            std::string(std::strerror(errno));
    return std::nullopt;
  }
  return VrrReport(fd);
}

bool VrrReport::append(const std::string &record, std::string &error) noexcept {
  error.clear();
  std::size_t written = 0;
  while (written < record.size()) {
    const auto result =
        ::write(fd_, record.data() + written, record.size() - written);
    if (result < 0 && errno == EINTR)
      continue;
    if (result <= 0) {
      error = "cannot write headless VRR report: " +
              std::string(std::strerror(errno));
      return false;
    }
    written += static_cast<std::size_t>(result);
  }
  return true;
}

bool VrrReport::record_capability(
    const VrrSimulationCapability &capability, std::string &error) {
  std::ostringstream record;
  record << "{\"record\":\"capability\",\"backend\":\"headless\","
            "\"device\":\"simulated\",\"driver\":\"headless\","
            "\"connector\":\"simulated\",\"crtc\":0,"
            "\"mode\":\"configured\","
            "\"output_id\":"
         << capability.output_id.value
         << ",\"connector_property_present\":"
         << boolean(capability.connector_property_present)
         << ",\"connector_property_value\":1,\"hardware_capable\":"
         << boolean(capability.hardware_capable)
         << ",\"crtc_property_present\":true,\"crtc_property_id\":0,"
            "\"original_value\":0,\"atomic_test_off\":true,"
            "\"atomic_test_on\":true,\"range_source\":\"configured\","
            "\"minimum_refresh_millihertz\":"
         << capability.minimum_refresh_millihertz
         << ",\"maximum_refresh_millihertz\":"
         << capability.maximum_refresh_millihertz
         << ",\"controllable\":" << boolean(capability.kms_controllable)
         << ",\"simulated\":true}\n";
  return append(record.str(), error);
}

bool VrrReport::record_presentation(
    const output::SoftwareFrameSetView &frames,
    const output::VrrPresentationFeedbackMap &feedback, std::string &error) {
  if (!frames.valid() || frames.outputs == nullptr) {
    error = "headless VRR report requires a valid frame set";
    return false;
  }
  for (const auto &[output_id, state] : feedback) {
    const auto frame = frames.outputs->find(output_id);
    if (frame == frames.outputs->end()) {
      error = "headless VRR feedback references an unknown frame output";
      return false;
    }
    const auto &request = frame->second.vrr;
    std::ostringstream decision;
    decision << "{\"record\":\"decision\",\"commit_id\":"
             << frames.commit_id << ",\"generation\":" << frames.generation
             << ",\"output_id\":" << output_id << ",\"policy_mode\":"
             << static_cast<unsigned>(request.requested_mode)
             << ",\"candidate_window_id\":" << request.candidate_window_id
             << ",\"candidate_surface_id\":" << request.candidate_surface_id
             << ",\"desired_enabled\":"
             << boolean(request.desired_enabled)
             << ",\"effective_enabled\":"
             << boolean(state.effective_enabled) << ",\"reason_mask\":"
             << request.reason_flags << ",\"reason_names\":"
             << reason_names(request.reason_flags)
             << ",\"session_active\":" << boolean(state.session_active)
             << ",\"transition_serial\":" << request.transition_serial
             << ",\"simulated\":true}\n";
    if (!append(decision.str(), error))
      return false;

    const auto target = request.target_interval_nanoseconds != 0
                            ? request.target_interval_nanoseconds
                            : state.interval_nanoseconds;
    const auto tolerance = output::vrr::timing_tolerance(target);
    const auto difference = state.interval_nanoseconds > target
                                ? state.interval_nanoseconds - target
                                : target - state.interval_nanoseconds;
    std::ostringstream timing;
    timing << "{\"record\":\"timing\",\"commit_id\":" << frames.commit_id
           << ",\"generation\":" << frames.generation
           << ",\"output_id\":" << output_id << ",\"sequence\":"
           << state.flip_sequence << ",\"kernel_timestamp_nanoseconds\":"
           << state.kernel_timestamp_nanoseconds
           << ",\"interval_nanoseconds\":" << state.interval_nanoseconds
           << ",\"target_interval_nanoseconds\":" << target
           << ",\"effective_enabled\":"
           << boolean(state.effective_enabled) << ",\"within_threshold\":"
           << boolean(difference <= tolerance) << ",\"simulated\":true}\n";
    if (!append(timing.str(), error))
      return false;

    auto &summary = summaries_[output_id];
    ++summary.sample_count;
    if (state.effective_enabled)
      ++summary.enabled_count;
    else
      ++summary.disabled_count;
    summary.minimum_interval =
        summary.minimum_interval == 0
            ? state.interval_nanoseconds
            : std::min(summary.minimum_interval, state.interval_nanoseconds);
    summary.maximum_interval =
        std::max(summary.maximum_interval, state.interval_nanoseconds);
    if (summary.interval_sum <=
        std::numeric_limits<std::uint64_t>::max() - state.interval_nanoseconds)
      summary.interval_sum += state.interval_nanoseconds;
    if (difference <= tolerance)
      ++summary.pass_count;
    summary.intervals.push_back(state.interval_nanoseconds);
    summary.absolute_errors.push_back(difference);
  }
  return true;
}

bool VrrReport::finish(std::string &error) noexcept {
  if (finished_) {
    error.clear();
    return true;
  }
  for (const auto &[output_id, summary] : summaries_) {
    auto intervals = summary.intervals;
    auto absolute_errors = summary.absolute_errors;
    std::sort(intervals.begin(), intervals.end());
    std::sort(absolute_errors.begin(), absolute_errors.end());
    const auto median = intervals.empty() ? 0 : intervals[intervals.size() / 2];
    const auto p95_index = absolute_errors.empty()
                               ? 0
                               : ((absolute_errors.size() * 95U + 99U) / 100U) -
                                     1U;
    const auto p95 = absolute_errors.empty() ? 0 : absolute_errors[p95_index];
    std::ostringstream record;
    record << "{\"record\":\"summary\",\"output_id\":" << output_id
           << ",\"sample_count\":" << summary.sample_count
           << ",\"pass_count\":" << summary.pass_count
           << ",\"pass_basis_points\":"
           << (summary.sample_count == 0
                   ? 0
                   : summary.pass_count * UINT64_C(10'000) /
                         summary.sample_count)
           << ",\"enabled_periods\":" << summary.enabled_count
           << ",\"disabled_periods\":" << summary.disabled_count
           << ",\"minimum_nanoseconds\":" << summary.minimum_interval
           << ",\"maximum_nanoseconds\":" << summary.maximum_interval
           << ",\"mean_nanoseconds\":"
           << (summary.sample_count == 0
                   ? 0
                   : summary.interval_sum / summary.sample_count)
           << ",\"median_nanoseconds\":" << median
           << ",\"p95_absolute_error_nanoseconds\":" << p95
           << ",\"simulated\":true}\n";
    if (!append(record.str(), error))
      return false;
  }
  if (!append("{\"record\":\"restore\",\"original_value\":0,"
              "\"restored_value\":0,\"readback_success\":true,"
              "\"kms_status\":\"not_applicable\","
              "\"vt_status\":\"not_applicable\","
              "\"getty_status\":\"not_applicable\","
              "\"simulated\":true}\n",
              error))
    return false;
  finished_ = true;
  return true;
}

} // namespace glasswyrm::headless
