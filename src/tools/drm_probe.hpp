#pragma once

#include "backends/drm/drm_api.hpp"

#include <cstdint>
#include <iosfwd>
#include <optional>
#include <string>
#include <vector>

namespace glasswyrm::tools {

struct DrmProbeOptions {
  std::string device{"auto"};
  std::optional<std::string> connector;
  std::optional<std::uint32_t> required_width;
  std::optional<std::uint32_t> required_height;
  std::string output_path;
  bool list{false};
  bool snapshot_state{false};
  bool expect_active{false};
  std::optional<std::string> expected_restored_path;
  // Test-only override for deterministic auto-discovery without /dev/dri.
  std::vector<std::string> auto_device_paths;
};

enum class DrmProbeParseResult { Run, ExitSuccess, ExitFailure };

[[nodiscard]] DrmProbeParseResult parse_drm_probe_options(
    int argc, char** argv, DrmProbeOptions& options, std::ostream& output,
    std::ostream& error);

[[nodiscard]] int run_drm_probe(drm::DrmApi& api,
                                const DrmProbeOptions& options,
                                std::ostream& error);

}  // namespace glasswyrm::tools
