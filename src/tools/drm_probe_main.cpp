#include "tools/drm_probe.hpp"

#include <iostream>

int main(int argc, char** argv) {
  glasswyrm::tools::DrmProbeOptions options;
  const auto parsed = glasswyrm::tools::parse_drm_probe_options(
      argc, argv, options, std::cout, std::cerr);
  if (parsed == glasswyrm::tools::DrmProbeParseResult::ExitSuccess) return 0;
  if (parsed == glasswyrm::tools::DrmProbeParseResult::ExitFailure) return 2;
  auto api = glasswyrm::drm::make_real_drm_api();
  return glasswyrm::tools::run_drm_probe(*api, options, std::cerr);
}
