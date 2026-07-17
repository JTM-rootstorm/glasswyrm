#pragma once

#include "backends/drm/device.hpp"
#include "backends/drm/damage_copy.hpp"
#include "backends/drm/drm_report.hpp"
#include "backends/drm/dumb_buffer.hpp"
#include "backends/drm/kms_api.hpp"
#include "backends/drm/kms_state.hpp"
#include "backends/headless/frame_dump.hpp"
#include "backends/output/presentation_backend.hpp"
#include "backends/session/vt_session.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace glasswyrm::drm {

enum class DrmPresentationApi { Auto, Atomic, Legacy };

struct DrmPresenterConfig {
  output::OutputSpec output;
  std::optional<std::string> connector;
  std::optional<std::uint32_t> refresh_millihz;
  DrmPresentationApi api{DrmPresentationApi::Auto};
  std::string tty_path;
  session::VirtualTerminalSignals vt_signals;
  bool damage_aware_copy{};
};

class DrmPresenter final : public output::PresentationBackend,
                           public session::DisplaySessionControl {
 public:
  static constexpr int kPageFlipTimeoutMilliseconds = 2000;
  static constexpr std::size_t kMaximumDiagnosticBytes = 512;

  DrmPresenter(Device device, KmsApi& kms, DrmReport* report = nullptr,
               headless::FrameDumper* mirror = nullptr) noexcept;
  ~DrmPresenter() override;
  DrmPresenter(const DrmPresenter&) = delete;
  DrmPresenter& operator=(const DrmPresenter&) = delete;

  [[nodiscard]] bool initialize(const DrmPresenterConfig& config,
                                session::VirtualTerminalApi* vt_api,
                                std::string& error);

  [[nodiscard]] output::PresentResult present(
      const output::SoftwareFrameView& frame) override;
  [[nodiscard]] int poll_fd() const noexcept override;
  [[nodiscard]] short poll_events() const noexcept override;
  [[nodiscard]] output::BackendEvent service(short revents) override;
  [[nodiscard]] bool finalize_pending(std::uint64_t token,
                                      std::string& error) override;
  void abort_pending(std::uint64_t token,
                     std::string_view reason = {}) noexcept override;
  [[nodiscard]] output::BackendStateResult suspend(std::string& error) override;
  [[nodiscard]] output::PresentResult resume(
      const output::SoftwareFrameView& committed) override;
  [[nodiscard]] output::BackendStateResult shutdown(
      std::string& error) noexcept override;

  [[nodiscard]] bool quiesce_pending_flip(std::string& error) override;
  [[nodiscard]] bool acquire_master(std::string& error) override;
  [[nodiscard]] bool drop_master(std::string& error) override;
  [[nodiscard]] bool present_committed_frame(std::string& error) override;
  [[nodiscard]] bool restore_original_display(std::string& error) override;
  [[nodiscard]] bool release_scanout_resources(std::string& error) override;

  [[nodiscard]] ReportApiPath selected_api() const noexcept {
    return selected_api_;
  }
  [[nodiscard]] PipelineIds pipeline() const noexcept { return pipeline_; }
  [[nodiscard]] std::uint32_t front_framebuffer() const noexcept {
    return buffers_.front().framebuffer_id();
  }
  [[nodiscard]] bool initialized() const noexcept { return initialized_; }
  [[nodiscard]] const std::string& fallback_reason() const noexcept {
    return fallback_reason_;
  }

 private:
  struct PendingPresentation;

  [[nodiscard]] bool select_pipeline(std::string& error);
  [[nodiscard]] bool configure_api(std::string& error);
  [[nodiscard]] bool live_connector_ids(std::vector<std::uint32_t>& ids,
                                        std::string& error);
  [[nodiscard]] bool try_atomic(std::string& error);
  [[nodiscard]] bool capture_legacy(std::string& error);
  [[nodiscard]] bool initialize_report(std::string& error);
  [[nodiscard]] bool stage_mirror(const output::SoftwareFrameView& frame,
                                  headless::StagedFrameDump& staged,
                                  std::string& error) const;
  [[nodiscard]] bool commit_evidence(headless::StagedFrameDump& mirror,
                                     StagedDrmReport& report,
                                     std::string& error);
  [[nodiscard]] bool blocking_modeset(DumbBuffer& buffer, std::string& error);
  [[nodiscard]] output::PresentResult present_initial(
      const output::SoftwareFrameView& frame, std::uint64_t hash);
  [[nodiscard]] output::PresentResult present_flip(
      const output::SoftwareFrameView& frame, std::uint64_t hash);
  [[nodiscard]] output::BackendEvent fatal_event(std::string stage,
                                                 std::string reason);
  void record_fatal(std::string stage, std::string reason) noexcept;
  [[nodiscard]] bool append_report(const DrmReportRecord& record,
                                   std::string& error);
  [[nodiscard]] bool copy_frame_to(
      DumbBuffer& target, const output::SoftwareFrameView& frame,
      FullCopyReason forced_reason, DamageCopyPlan& plan,
      std::string& error);
  [[nodiscard]] DamageCopyReport damage_copy_report(
      const DumbBuffer& target, const DamageCopyPlan& plan,
      std::uint64_t generation, std::uint32_t buffer_index) const;
  void complete_damage_copy(DumbBuffer& target, const DamageCopyPlan& plan,
                            std::uint64_t generation);
  void clear_pending() noexcept;

  Device device_;
  KmsApi& kms_;
  DrmReport* report_{};
  headless::FrameDumper* mirror_{};
  KmsDumbBufferApi dumb_api_;
  DumbBufferPair buffers_;
  DrmPresenterConfig config_;
  PipelineIds pipeline_;
  Mode selected_mode_;
  KmsMode kms_mode_;
  SavedKmsState saved_;
  ModeBlob mode_blob_;
  ReportApiPath selected_api_{ReportApiPath::Legacy};
  std::unique_ptr<session::DirectVirtualTerminalSession> direct_session_;
  std::unique_ptr<PendingPresentation> pending_;
  std::unique_ptr<DamageCopyHistory> damage_history_;
  std::vector<std::uint32_t> committed_pixels_;
  std::uint64_t committed_hash_{};
  std::uint64_t committed_generation_{};
  std::uint64_t next_token_{1};
  std::uint64_t cumulative_full_frame_bytes_{};
  std::uint64_t cumulative_copied_bytes_{};
  std::string fallback_reason_;
  std::string shutdown_error_;
  std::size_t front_index_{};
  bool initialized_{};
  bool initial_modeset_{};
  bool display_taken_{};
  bool suspended_{};
  bool fatal_{};
  bool shutdown_{};
  output::BackendStateResult shutdown_result_{output::BackendStateResult::Complete};
  bool master_owned_{};
  bool scanout_released_{};
  bool scanout_cleanup_success_{};
};

}  // namespace glasswyrm::drm
