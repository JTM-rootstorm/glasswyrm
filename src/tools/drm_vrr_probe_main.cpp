#include "tools/drm_vrr_probe.hpp"

#include <cstdlib>
#include <iostream>
#include <string_view>

int main(int argc, char** argv) {
  glasswyrm::tools::DrmVrrProbeOptions options;
  const auto parsed = glasswyrm::tools::parse_drm_vrr_probe_options(
      argc, argv, options, std::cout, std::cerr);
  if (parsed == glasswyrm::tools::DrmVrrProbeParseResult::ExitSuccess) return 0;
  if (parsed == glasswyrm::tools::DrmVrrProbeParseResult::ExitFailure) return 2;
  const char* hardware_opt_in = std::getenv("GW_ALLOW_HARDWARE_TESTS");
  if (hardware_opt_in == nullptr || std::string_view(hardware_opt_in) != "1") {
    std::cerr << "gw_drm_vrr_probe: live hardware execution requires exactly "
                 "GW_ALLOW_HARDWARE_TESTS=1\n";
    return 1;
  }
  auto drm = glasswyrm::drm::make_real_drm_api();
  auto kms = glasswyrm::drm::make_real_kms_api();
  return glasswyrm::tools::run_drm_vrr_probe(
      *drm, *kms, glasswyrm::tools::real_drm_vrr_probe_platform(), options,
      std::cerr);
}
