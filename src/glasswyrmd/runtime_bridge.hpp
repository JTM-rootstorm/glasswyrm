#pragma once

#include "glasswyrmd/compositor_peer.hpp"
#include "glasswyrmd/policy_peer.hpp"

#include <chrono>
#include <string>

namespace glasswyrm::server {

class RuntimeBridge {
public:
  using Clock = std::chrono::steady_clock;

  RuntimeBridge(std::string policy_path, std::string compositor_path,
                gw::protocol::x11::ScreenModel screen,
                std::chrono::milliseconds deadline = std::chrono::seconds(10),
                bool software_content = false,
                bool session_state = false);

  void start(Clock::time_point now = Clock::now()) noexcept;
  [[nodiscard]] bool service(short policy_revents, short compositor_revents,
                             Clock::time_point now, std::string &error);
  [[nodiscard]] int policy_fd() const noexcept { return policy_.fd(); }
  [[nodiscard]] short policy_events() const noexcept {
    return policy_.wanted_events();
  }
  [[nodiscard]] int compositor_fd() const noexcept { return compositor_.fd(); }
  [[nodiscard]] short compositor_events() const noexcept {
    return compositor_.wanted_events();
  }
  [[nodiscard]] bool ready() const noexcept;
  [[nodiscard]] int poll_timeout_ms(Clock::time_point now) const noexcept;
  [[nodiscard]] bool submit_policy(const PolicySnapshotSubmission& submission,
                                   std::string& error);
  [[nodiscard]] bool policy_result_ready() const noexcept;
  [[nodiscard]] bool policy_rejected_ready() const noexcept;
  [[nodiscard]] const PolicySnapshotResult& policy_result() const noexcept {
    return policy_.result();
  }
  [[nodiscard]] bool submit_compositor(
      const CompositorSnapshotSubmission& submission, std::string& error);
  [[nodiscard]] bool submit_content(
      const CompositorContentSubmission& submission, std::string& error);
  [[nodiscard]] bool submit_cursor(
      const CompositorCursorSubmission& submission, std::uint64_t commit_id,
      std::uint64_t generation, std::string& error);
  [[nodiscard]] bool submit_replay(
      const CompositorSnapshotSubmission& submission, std::string& error);
  [[nodiscard]] std::vector<CompositorBufferRelease> take_buffer_releases() {
    return compositor_.take_releases();
  }
  [[nodiscard]] std::vector<CompositorSessionStateChange>
  take_session_state_changes() {
    return compositor_.take_session_state_changes();
  }
  [[nodiscard]] bool acknowledge_session_state(
      const CompositorSessionStateChange& request,
      gwipc_session_state_result result, std::string& error) {
    return compositor_.acknowledge_session_state(request, result, error);
  }
  [[nodiscard]] bool compositor_result_ready() const noexcept;
  [[nodiscard]] bool compositor_rejected_ready() const noexcept;
  [[nodiscard]] bool content_result_ready() const noexcept;
  [[nodiscard]] bool content_rejected_ready() const noexcept;
  [[nodiscard]] bool cursor_result_ready() const noexcept;
  [[nodiscard]] bool cursor_rejected_ready() const noexcept;
  [[nodiscard]] bool replay_result_ready() const noexcept;
  [[nodiscard]] bool replay_rejected_ready() const noexcept;
  [[nodiscard]] bool transaction_idle() const noexcept;
  [[nodiscard]] bool take_compositor_reset() noexcept {
    const bool result = compositor_reset_;
    compositor_reset_ = false;
    return result;
  }
  [[nodiscard]] bool prepare_rollback() noexcept;
  void clear_transaction_result() noexcept;

private:
  enum class Stage { Policy, Compositor, Ready, Failed };
  void schedule_retry(Clock::time_point now) noexcept;

  PolicyPeer policy_;
  CompositorPeer compositor_;
  Stage stage_{Stage::Policy};
  Clock::time_point deadline_{};
  Clock::time_point retry_at_{};
  std::chrono::milliseconds deadline_duration_;
  unsigned retry_index_{};
  enum class TransactionStage { None, Policy, PolicyReady, PolicyRejected,
                                Compositor, Content, Cursor, Complete,
                                ContentComplete, CursorComplete,
                                CompositorRejected, ContentRejected,
                                CursorRejected, Replay,
                                ReplayComplete, ReplayRejected };
  TransactionStage transaction_stage_{TransactionStage::None};
  TransactionStage resume_transaction_stage_{TransactionStage::None};
  PolicySnapshotSubmission pending_policy_;
  CompositorSnapshotSubmission pending_compositor_;
  CompositorContentSubmission pending_content_;
  bool recovering_{};
  bool compositor_reset_{};
};

} // namespace glasswyrm::server
