#pragma once

#include "gwcomp/options.hpp"

#include <memory>
#include <string>

namespace glasswyrm::drm {
class DrmApi;
class KmsApi;
class DrmReport;
}
namespace glasswyrm::headless {
class FrameDumper;
}
namespace glasswyrm::output {
class PresentationBackend;
}
namespace glasswyrm::session {
class LinuxVirtualTerminalApi;
}

namespace glasswyrm::compositor {

struct DrmRuntimeResources {
  std::unique_ptr<drm::DrmApi> drm_api;
  std::unique_ptr<drm::KmsApi> kms_api;
  std::unique_ptr<drm::DrmReport> report;
  std::unique_ptr<headless::FrameDumper> mirror;
  std::unique_ptr<session::LinuxVirtualTerminalApi> vt_api;

  DrmRuntimeResources();
  ~DrmRuntimeResources();
  DrmRuntimeResources(const DrmRuntimeResources&) = delete;
  DrmRuntimeResources& operator=(const DrmRuntimeResources&) = delete;
};

[[nodiscard]] bool create_drm_presenter(
    const Options& options, DrmRuntimeResources& resources,
    std::unique_ptr<output::PresentationBackend>& presenter,
    std::string& error);

}  // namespace glasswyrm::compositor
