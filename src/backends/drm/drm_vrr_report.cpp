#include "backends/drm/drm_vrr_report.hpp"

#include <iomanip>
#include <sstream>
#include <string_view>

namespace glasswyrm::drm {
namespace {

const char *boolean(const bool value) noexcept { return value ? "true" : "false"; }

std::string quote(const std::string_view value) {
  std::ostringstream stream;
  stream << '"';
  for (const unsigned char byte : value) {
    switch (byte) {
    case '"':
      stream << "\\\"";
      break;
    case '\\':
      stream << "\\\\";
      break;
    case '\n':
      stream << "\\n";
      break;
    case '\r':
      stream << "\\r";
      break;
    case '\t':
      stream << "\\t";
      break;
    default:
      if (byte < 0x20) {
        stream << "\\u00" << std::hex << std::setfill('0') << std::setw(2)
               << static_cast<unsigned>(byte) << std::dec;
      } else {
        stream << static_cast<char>(byte);
      }
    }
  }
  stream << '"';
  return stream.str();
}

const char *policy_name(const output::vrr::PolicyMode value) noexcept {
  switch (value) {
  case output::vrr::PolicyMode::Off:
    return "off";
  case output::vrr::PolicyMode::Fullscreen:
    return "fullscreen";
  case output::vrr::PolicyMode::Focused:
    return "focused";
  case output::vrr::PolicyMode::AppRequested:
    return "app-requested";
  case output::vrr::PolicyMode::AlwaysEligible:
    return "always-eligible";
  }
  return "invalid";
}

void reasons(std::ostringstream &stream, const output::vrr::ReasonMask mask) {
  stream << '[';
  bool first = true;
  for (const auto reason : output::vrr::reason_precedence()) {
    if (!output::vrr::has_reason(mask, reason))
      continue;
    if (!first)
      stream << ',';
    first = false;
    stream << quote(output::vrr::reason_name(reason));
  }
  stream << ']';
}

std::string serialize(const DrmVrrCapabilityReport &value) {
  std::ostringstream stream;
  stream << "{\"record\":\"vrr-capability\",\"device\":"
         << quote(value.device) << ",\"driver\":" << quote(value.driver)
         << ",\"connector\":" << quote(value.connector)
         << ",\"crtc_id\":" << value.crtc_id << ",\"mode\":"
         << quote(value.mode) << ",\"connector_property_present\":"
         << boolean(value.connector_property_present)
         << ",\"connector_property_value\":"
         << boolean(value.connector_property_value)
         << ",\"crtc_property_present\":"
         << boolean(value.crtc_property_present)
         << ",\"crtc_property_id\":" << value.crtc_property_id
         << ",\"original_enabled\":" << boolean(value.original_enabled)
         << ",\"atomic_test_off\":"
         << boolean(value.atomic_test_off_passed)
         << ",\"atomic_test_on\":" << boolean(value.atomic_test_on_passed)
         << ",\"range_source\":" << quote(value.range_source)
         << ",\"minimum_refresh_millihertz\":"
         << value.minimum_refresh_millihertz
         << ",\"maximum_refresh_millihertz\":"
         << value.maximum_refresh_millihertz
         << ",\"controllable\":" << boolean(value.controllable) << '}';
  return stream.str();
}

std::string serialize(const DrmVrrDecisionReport &value) {
  std::ostringstream stream;
  stream << "{\"record\":\"vrr-decision\",\"commit_id\":"
         << value.commit_id << ",\"generation\":" << value.generation
         << ",\"output_id\":" << value.output_id << ",\"policy\":"
         << quote(policy_name(value.policy_mode))
         << ",\"candidate_window_id\":" << value.candidate_window_id
         << ",\"candidate_surface_id\":" << value.candidate_surface_id
         << ",\"desired_enabled\":" << boolean(value.desired_enabled)
         << ",\"effective_enabled\":" << boolean(value.effective_enabled)
         << ",\"reason_mask\":" << value.reason_flags
         << ",\"reasons\":";
  reasons(stream, value.reason_flags);
  stream << ",\"session_active\":" << boolean(value.session_active)
         << ",\"transition_serial\":" << value.transition_serial << '}';
  return stream.str();
}

std::string serialize(const DrmVrrTimingReport &value) {
  std::ostringstream stream;
  stream << "{\"record\":\"vrr-timing\",\"commit_id\":"
         << value.commit_id << ",\"generation\":" << value.generation
         << ",\"sequence\":" << value.sequence
         << ",\"kernel_timestamp_nanoseconds\":"
         << value.kernel_timestamp_nanoseconds
         << ",\"interval_nanoseconds\":" << value.interval_nanoseconds
         << ",\"target_interval_nanoseconds\":"
         << value.target_interval_nanoseconds << ",\"effective_enabled\":"
         << boolean(value.effective_enabled) << ",\"within_threshold\":"
         << boolean(value.within_threshold) << '}';
  return stream.str();
}

std::string serialize(const DrmVrrSummaryReport &value) {
  std::ostringstream stream;
  stream << "{\"record\":\"vrr-summary\",\"sample_count\":"
         << value.sample_count << ",\"pass_count\":" << value.pass_count
         << ",\"pass_basis_points\":" << value.pass_basis_points
         << ",\"minimum_nanoseconds\":" << value.minimum_nanoseconds
         << ",\"maximum_nanoseconds\":" << value.maximum_nanoseconds
         << ",\"mean_nanoseconds\":" << value.mean_nanoseconds
         << ",\"median_nanoseconds\":" << value.median_nanoseconds
         << ",\"p95_absolute_error_nanoseconds\":"
         << value.p95_absolute_error_nanoseconds
         << ",\"enabled_period_count\":" << value.enabled_period_count
         << ",\"disabled_period_count\":" << value.disabled_period_count
         << '}';
  return stream.str();
}

std::string serialize(const DrmVrrRestoreReport &value) {
  std::ostringstream stream;
  stream << "{\"record\":\"vrr-restore\",\"original_enabled\":"
         << boolean(value.original_enabled) << ",\"restored_enabled\":"
         << boolean(value.restored_enabled) << ",\"readback_success\":"
         << boolean(value.readback_success) << ",\"kms_restore\":"
         << boolean(value.kms_restore) << ",\"vt_restore\":"
         << boolean(value.vt_restore) << ",\"getty_restore\":"
         << boolean(value.getty_restore) << '}';
  return stream.str();
}

bool valid(const DrmVrrCapabilityReport &value) noexcept {
  return !value.device.empty() && !value.driver.empty() &&
         !value.connector.empty() && value.crtc_id != 0 &&
         !value.mode.empty() && !value.range_source.empty() &&
         (!value.crtc_property_present || value.crtc_property_id != 0) &&
         (value.minimum_refresh_millihertz == 0 ||
          value.minimum_refresh_millihertz <= value.maximum_refresh_millihertz);
}

bool valid(const DrmVrrDecisionReport &value) noexcept {
  return value.commit_id != 0 && value.generation != 0 &&
         value.output_id != 0 &&
         output::vrr::valid_policy_mode(value.policy_mode) &&
         output::vrr::valid_reason_mask(value.reason_flags);
}

bool valid(const DrmVrrTimingReport &value) noexcept {
  return value.commit_id != 0 && value.generation != 0 && value.sequence != 0;
}

bool valid(const DrmVrrSummaryReport &value) noexcept {
  return value.pass_count <= value.sample_count &&
         value.pass_basis_points <= 10'000;
}

bool valid(const DrmVrrRestoreReport &) noexcept { return true; }

} // namespace

bool valid_drm_vrr_report_record(const DrmVrrReportRecord &record) noexcept {
  return std::visit([](const auto &value) { return valid(value); }, record);
}

std::string serialize_drm_vrr_report_record(const DrmVrrReportRecord &record) {
  return std::visit([](const auto &value) { return serialize(value); }, record);
}

} // namespace glasswyrm::drm
