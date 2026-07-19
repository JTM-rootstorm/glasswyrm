#pragma once

#include "backends/headless/frame_dump.hpp"
#include "backends/headless/vrr_report.hpp"
#include "backends/headless/vrr_simulation.hpp"
#include "backends/output/presentation_backend.hpp"

#include <filesystem>
#include <optional>
#include <utility>

namespace glasswyrm::headless {

class Presenter final : public output::PresentationBackend {
 public:
  explicit Presenter(std::filesystem::path dump_directory)
      : dumper_(std::move(dump_directory)) {}
  Presenter(std::filesystem::path dump_directory,
            std::optional<VrrSimulation> vrr_simulation,
            std::optional<VrrReport> vrr_report = std::nullopt)
      : dumper_(std::move(dump_directory)),
        vrr_simulation_(std::move(vrr_simulation)),
        vrr_report_(std::move(vrr_report)) {}

  [[nodiscard]] std::optional<output::VrrPresentationCapability>
  vrr_capability(std::uint64_t output_id) const noexcept override;

  [[nodiscard]] output::PresentResult present(
      const output::SoftwareFrameView& frame) override;
  [[nodiscard]] output::PresentResult present(
      const output::SoftwareFrameSetView& frames) override;
  [[nodiscard]] int poll_fd() const noexcept override { return -1; }
  [[nodiscard]] short poll_events() const noexcept override { return 0; }
  [[nodiscard]] output::BackendEvent service(short revents) override;
  [[nodiscard]] output::BackendStateResult suspend(std::string& error) override;
  [[nodiscard]] output::PresentResult resume(
      const output::SoftwareFrameView& committed) override;
  [[nodiscard]] output::PresentResult resume(
      const output::SoftwareFrameSetView& committed) override;
  [[nodiscard]] output::BackendStateResult shutdown(
      std::string& error) noexcept override;

 private:
  [[nodiscard]] output::PresentResult present_frame_set(
      const output::SoftwareFrameSetView& frames, bool record_frame_set);
  FrameDumper dumper_;
  std::optional<VrrSimulation> vrr_simulation_;
  std::optional<VrrReport> vrr_report_;
};

}  // namespace glasswyrm::headless
