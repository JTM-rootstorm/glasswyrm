#include "tools/drm_probe.hpp"

#include <cstdlib>
#include <iostream>
#include <string_view>

int main(int argc, char** argv) {
  glasswyrm::tools::DrmProbeOptions options;
  const auto parsed = glasswyrm::tools::parse_drm_probe_options(
      argc, argv, options, std::cout, std::cerr);
  if (parsed == glasswyrm::tools::DrmProbeParseResult::ExitSuccess) return 0;
  if (parsed == glasswyrm::tools::DrmProbeParseResult::ExitFailure) return 2;
  const char* hardware_opt_in = std::getenv("GW_ALLOW_HARDWARE_TESTS");
  if (hardware_opt_in == nullptr || std::string_view(hardware_opt_in) != "1") {
    std::cerr << "gw_drm_probe: live DRM access requires exactly "
                 "GW_ALLOW_HARDWARE_TESTS=1\n";
    return 1;
  }
  auto api = glasswyrm::drm::make_real_drm_api();
  return glasswyrm::tools::run_drm_probe(*api, options, std::cerr);
}
