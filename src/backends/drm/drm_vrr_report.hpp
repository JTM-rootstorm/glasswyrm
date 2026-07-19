#pragma once

#include "output/vrr/reasons.hpp"
#include "output/vrr/types.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>

namespace glasswyrm::drm {

struct DrmVrrCapabilityReport {
  std::string device;
  std::string driver;
  std::string connector;
  std::uint32_t crtc_id{};
  std::string mode;
  bool connector_property_present{};
  bool connector_property_value{};
  bool crtc_property_present{};
  std::uint32_t crtc_property_id{};
  bool original_enabled{};
  bool atomic_test_off_passed{};
  bool atomic_test_on_passed{};
  std::string range_source{"none"};
  std::uint32_t minimum_refresh_millihertz{};
  std::uint32_t maximum_refresh_millihertz{};
  bool controllable{};
};

struct DrmVrrDecisionReport {
  std::uint64_t commit_id{};
  std::uint64_t generation{};
  std::uint64_t output_id{};
  output::vrr::PolicyMode policy_mode{output::vrr::PolicyMode::Off};
  std::uint32_t candidate_window_id{};
  std::uint64_t candidate_surface_id{};
  bool desired_enabled{};
  bool effective_enabled{};
  output::vrr::ReasonMask reason_flags{};
  bool session_active{};
  std::uint64_t transition_serial{};
};

struct DrmVrrTimingReport {
  std::uint64_t commit_id{};
  std::uint64_t generation{};
  std::uint32_t sequence{};
  std::uint64_t kernel_timestamp_nanoseconds{};
  std::uint64_t interval_nanoseconds{};
  std::uint64_t target_interval_nanoseconds{};
  bool effective_enabled{};
  bool within_threshold{};
};

struct DrmVrrSummaryReport {
  std::size_t sample_count{};
  std::size_t pass_count{};
  std::uint32_t pass_basis_points{};
  std::uint64_t minimum_nanoseconds{};
  std::uint64_t maximum_nanoseconds{};
  std::uint64_t mean_nanoseconds{};
  std::uint64_t median_nanoseconds{};
  std::uint64_t p95_absolute_error_nanoseconds{};
  std::size_t enabled_period_count{};
  std::size_t disabled_period_count{};
};

struct DrmVrrRestoreReport {
  bool original_enabled{};
  bool restored_enabled{};
  bool readback_success{};
  bool kms_restore{};
  bool vt_restore{};
  bool getty_restore{};
};

using DrmVrrReportRecord =
    std::variant<DrmVrrCapabilityReport, DrmVrrDecisionReport,
                 DrmVrrTimingReport, DrmVrrSummaryReport,
                 DrmVrrRestoreReport>;

[[nodiscard]] bool valid_drm_vrr_report_record(
    const DrmVrrReportRecord &record) noexcept;
[[nodiscard]] std::string serialize_drm_vrr_report_record(
    const DrmVrrReportRecord &record);

} // namespace glasswyrm::drm
