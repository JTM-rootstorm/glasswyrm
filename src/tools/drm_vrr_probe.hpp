#pragma once

#include "backends/drm/device.hpp"
#include "backends/drm/kms_api.hpp"

#include <cstdint>
#include <iosfwd>
#include <string>

namespace glasswyrm::tools {

struct DrmVrrProbeOptions {
  std::string device;
  std::string connector;
  std::string mode;
  std::string run_id;
  std::string output_path;
  std::uint32_t target_refresh_hz{70};
  std::uint32_t warmup_flips{10};
  std::uint32_t recorded_flips{140};
  std::uint32_t event_timeout_milliseconds{3000};
};

enum class DrmVrrProbeParseResult { Run, ExitSuccess, ExitFailure };

class DrmVrrProbePlatform {
 public:
  virtual ~DrmVrrProbePlatform() = default;
  [[nodiscard]] virtual bool monotonic_nanoseconds(std::uint64_t& value,
                                                   std::string& error) = 0;
  [[nodiscard]] virtual bool sleep_until(std::uint64_t deadline_nanoseconds,
                                         std::string& error) = 0;
  virtual void submitted(std::uint64_t token, std::uint32_t crtc_id,
                         bool enabled, std::uint32_t ordinal) = 0;
};

[[nodiscard]] DrmVrrProbeParseResult parse_drm_vrr_probe_options(
    int argc, char** argv, DrmVrrProbeOptions& options, std::ostream& output,
    std::ostream& error);

[[nodiscard]] int run_drm_vrr_probe(
    drm::DrmApi& drm_api, drm::KmsApi& kms_api,
    DrmVrrProbePlatform& platform, const DrmVrrProbeOptions& options,
    std::ostream& error);

[[nodiscard]] DrmVrrProbePlatform& real_drm_vrr_probe_platform();

}  // namespace glasswyrm::tools
