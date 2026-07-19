#pragma once

#include "backends/drm/inventory.hpp"
#include "gwcomp/options.hpp"

#include <memory>
#include <optional>
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
  std::unique_ptr<drm::DrmReport> vrr_report;
  std::unique_ptr<headless::FrameDumper> mirror;
  std::unique_ptr<session::LinuxVirtualTerminalApi> vt_api;

  DrmRuntimeResources();
  ~DrmRuntimeResources();
  DrmRuntimeResources(const DrmRuntimeResources&) = delete;
  DrmRuntimeResources& operator=(const DrmRuntimeResources&) = delete;
};

[[nodiscard]] std::optional<drm::DrmInventorySelection>
resolve_drm_output_selection(const drm::DeviceSnapshot& snapshot,
                             const Options& options);

[[nodiscard]] bool create_drm_presenter(
    const Options& options, DrmRuntimeResources& resources,
    drm::DrmOutputInventory& inventory,
    std::unique_ptr<output::PresentationBackend>& presenter,
    std::string& error);

[[nodiscard]] bool create_drm_presenter(
    const Options& options, DrmRuntimeResources& resources,
    std::unique_ptr<output::PresentationBackend>& presenter,
    std::string& error);

}  // namespace glasswyrm::compositor
