#pragma once

#include "config.hpp"
#include "glasswyrmd/server.hpp"

#ifdef GW_SERVER_HAS_IPC
#include "glasswyrmd/content_presenter.hpp"
#include "glasswyrmd/cursor_presenter.hpp"
#include "glasswyrmd/event_router.hpp"
#include "glasswyrmd/lifecycle_coordinator.hpp"
#include "glasswyrmd/output_control_peer.hpp"
#include "glasswyrmd/runtime_bridge.hpp"
#include "glasswyrmd/synthetic_input_peer.hpp"
#include "glasswyrmd/vrr_events.hpp"
#include "input/input_state.hpp"
#if GW_HAS_LIBINPUT_BACKEND
#include "glasswyrmd/real_input_controller.hpp"
#include "wm/interactive_policy.hpp"
#endif
#endif

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <poll.h>
#include <vector>

namespace glasswyrm::server {

class SignalRuntime {
 public:
  SignalRuntime() = default;
  ~SignalRuntime();

  SignalRuntime(const SignalRuntime&) = delete;
  SignalRuntime& operator=(const SignalRuntime&) = delete;

  [[nodiscard]] bool start();
  void close() noexcept;
  [[nodiscard]] int read_descriptor() const noexcept { return descriptors_[0]; }
  [[nodiscard]] static bool stop_requested() noexcept;
  static void request_stop() noexcept;

 private:
  int descriptors_[2]{-1, -1};
};

class ServerRuntime {
 public:
  explicit ServerRuntime(Server& server) : server_(server) {}

  [[nodiscard]] int run();

 private:
  [[nodiscard]] bool initialize_trace();
  [[nodiscard]] int initialize_integrated(SignalRuntime& signals);
  [[nodiscard]] int event_loop(SignalRuntime& signals);
  void shutdown(SignalRuntime& signals);

#ifdef GW_SERVER_HAS_IPC
  struct PendingFocusInput {
    SyntheticInputRecord record;
    std::uint32_t old_focus{};
    std::size_t delivered{};
    bool provider_connected{true};
  };

  struct PendingMutation {
    std::optional<WindowCreateSpec> create;
    ClientId owner{};
    std::uint32_t resource_base{};
    std::uint32_t resource_mask{};
    std::uint64_t creation_serial{};
    std::optional<WindowDestroyPlan> destroy;
    std::optional<ClientCleanupPlan> cleanup;
    std::optional<DeferredPropertyMutation> property;
    std::optional<WindowScaleState> scale;
    std::optional<DeferredVrrMutation> vrr;
    std::optional<WindowVrrState> vrr_before;
  };

  void initialize_lifecycle();
  [[nodiscard]] bool send_policy(const LifecycleSnapshot& snapshot);
  [[nodiscard]] bool send_compositor(const LifecycleSnapshot& snapshot);
  [[nodiscard]] bool commit_lifecycle(const LifecycleSnapshot& snapshot);
  void complete_lifecycle(std::uint64_t token, bool success);
  [[nodiscard]] bool defer_lifecycle(ClientConnection& client,
                                     const DispatchResult& result);
  void cancel_client_lifecycle(std::uint64_t client,
                               std::uint32_t resource_base);
  [[nodiscard]] bool service_integrated(short policy_events,
                                        short compositor_events);
  [[nodiscard]] bool service_peer_readiness(short policy_events,
                                            short compositor_events,
                                            bool& compositor_reset);
  [[nodiscard]] bool service_input_session_work(
      bool compositor_reset,
      std::vector<CompositorBufferRelease>& compositor_releases);
  [[nodiscard]] bool service_peer_replay(std::string& error);
  [[nodiscard]] bool service_lifecycle_work(
      const std::vector<CompositorBufferRelease>& compositor_releases);
  [[nodiscard]] bool service_output_control_work();
  [[nodiscard]] bool submit_output_policy(
      const LifecycleSnapshot& snapshot, const output::OutputLayout& layout,
      bool rollback);
  [[nodiscard]] bool submit_output_compositor(
      const LifecycleSnapshot& snapshot, const output::OutputLayout& layout,
      bool rollback);
  [[nodiscard]] bool begin_output_rollback(
      gw::ipc::wire::OutputConfigurationResult result,
      bool compositor_accepted);
  [[nodiscard]] bool install_output_configuration(
      const LifecycleSnapshot& snapshot, const output::OutputLayout& layout,
      bool emit_events);
  [[nodiscard]] bool promote_output_configuration();
  [[nodiscard]] bool finish_output_configuration();
  [[nodiscard]] bool output_configuration_active() const noexcept;
  void submit_pending_content(std::string& error);
  void service_input(short listener_events, short connection_events);
  [[nodiscard]] bool warp_pointer(std::int32_t x, std::int32_t y);
  [[nodiscard]] bool service_cursor();
  void mark_cursor_dirty() noexcept { cursor_dirty_ = true; }
  [[nodiscard]] std::shared_ptr<const glasswyrm::input::CursorImage>
  current_cursor_image() const noexcept;
#if GW_HAS_LIBINPUT_BACKEND
  [[nodiscard]] bool initialize_real_input();
  [[nodiscard]] bool service_real_input(short input_events,
                                        short repeat_events);
  [[nodiscard]] bool service_session_changes();
  [[nodiscard]] bool suspend_real_input_for_compositor_reset(bool reset);
  [[nodiscard]] bool resume_real_input_after_compositor_reset();
  void deliver_real_input();
  void complete_real_focus(bool success);
  [[nodiscard]] bool initialize_interactive_policy();
  [[nodiscard]] bool begin_interactive_pointer(const RealInputEvent& event);
  [[nodiscard]] bool update_interactive_geometry();
  [[nodiscard]] bool handle_interactive_close(const RealInputEvent& event);
  void complete_interactive_lifecycle(const LifecycleOperation& operation,
                                      bool success);
  void complete_interactive_cursor_publication();
  void abort_interactive() noexcept;
#endif

  std::unique_ptr<RuntimeBridge> bridge_;
  std::unique_ptr<LifecycleCoordinator> lifecycle_;
  std::unique_ptr<ContentPresenter> content_presenter_;
  std::unique_ptr<CursorPresenter> cursor_presenter_;
  std::unique_ptr<SyntheticInputPeer> input_peer_;
  std::unique_ptr<OutputControlPeer> output_control_peer_;
  enum class OutputConfigurationRuntimeStage {
    Idle,
    AwaitingPolicy,
    AwaitingCompositor,
    RollingBackPolicy,
    RollingBackCompositor,
  };
  OutputConfigurationRuntimeStage output_configuration_stage_{
      OutputConfigurationRuntimeStage::Idle};
  std::optional<LifecycleSnapshot> output_configuration_before_;
  std::optional<LifecycleSnapshot> output_configuration_evaluated_;
  std::optional<VrrStateCache> output_configuration_vrr_before_;
  std::vector<ProtocolEventIntent> output_configuration_events_;
  bool output_configuration_peer_reset_{};
  glasswyrm::input::InputState input_state_;
  std::uint64_t expected_input_id_{1};
  std::optional<PendingFocusInput> pending_focus_input_;
  bool cursor_dirty_{};
  bool cursor_force_buffer_{};
  bool cursor_replay_attempted_{};
  struct PendingCursorDiagnostic {
    glasswyrm::input::CursorKind kind{glasswyrm::input::CursorKind::Pixmap};
    std::int32_t x{};
    std::int32_t y{};
    bool visible{};
    bool buffer_attached{};
  };
  std::optional<PendingCursorDiagnostic> cursor_submission_diagnostic_;
#if GW_HAS_LIBINPUT_BACKEND
  struct PendingRealFocus {
    std::uint32_t old_focus{};
  };
  std::unique_ptr<RealInputController> real_input_;
  std::optional<PendingRealFocus> pending_real_focus_;
  std::optional<glasswyrm::wm::InteractiveBindings> interactive_bindings_;
  std::optional<glasswyrm::wm::InteractivePolicy> interactive_policy_;
  std::optional<std::uint64_t> interactive_geometry_token_;
  bool cursor_submission_interactive_{};
  bool real_input_suspended_for_compositor_reset_{};
  std::shared_ptr<const glasswyrm::input::CursorImage> move_cursor_;
  std::shared_ptr<const glasswyrm::input::CursorImage> resize_cursor_;
#endif
  std::uint64_t next_lifecycle_token_{1};
  // Runtime bootstrap owns commit/generation 1.
  std::uint64_t next_policy_commit_{2};
  std::uint64_t next_policy_generation_{2};
  std::uint64_t next_compositor_commit_{2};
  std::uint64_t next_compositor_generation_{2};
  std::uint64_t policy_commit_{0};
  std::uint64_t policy_generation_{0};
  std::optional<StructuralEventState> transition_before_;
  std::optional<WindowScaleState> scale_transition_before_;
  std::optional<InputTransitionState> input_transition_before_;
  std::optional<VrrEventBatch> vrr_event_batch_;
  std::optional<VrrStateCache> lifecycle_vrr_before_;
  std::optional<LifecycleSnapshot> lifecycle_policy_reconciliation_;
  bool content_replay_attempted_{false};
  std::map<std::uint64_t, PendingMutation> pending_mutations_;
#endif

  Server& server_;
};

}  // namespace glasswyrm::server
