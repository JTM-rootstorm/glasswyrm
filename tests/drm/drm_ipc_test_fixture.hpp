#pragma once

#include "backends/drm/drm_report.hpp"
#include "backends/drm/fake_drm_api.hpp"
#include "backends/drm/fake_kms_api.hpp"
#include "backends/drm/presenter.hpp"
#include "backends/headless/frame_dump.hpp"
#include "backends/session/vt_api.hpp"
#include "gwcomp/contract_dispatch.hpp"
#include "gwcomp/compositor.hpp"

#include <glasswyrm/ipc.h>

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace gw::test::drm_ipc {

enum class ProducerKind { M4, ProtocolServer };

struct ClientEvent {
  std::uint16_t type{};
  std::uint64_t commit_id{};
  std::uint64_t buffer_id{};
  gwipc_frame_result frame_result{GWIPC_FRAME_REJECTED_INCOMPLETE_METADATA};
  gwipc_buffer_release_reason release_reason{GWIPC_BUFFER_RELEASE_INVALID};
};

class FakeVirtualTerminal final
    : public glasswyrm::session::VirtualTerminalApi {
 public:
  int open_terminal(std::string_view) override;
  bool identify(int, glasswyrm::session::DeviceIdentity&) override;
  bool get_state(int, glasswyrm::session::VirtualTerminalState&) override;
  bool get_mode(int, glasswyrm::session::VirtualTerminalMode&) override;
  bool get_kd_mode(int, int&) override;
  bool get_keyboard_mode(int, int&) override;
  bool activate(int, unsigned) override;
  bool wait_until_active(int, unsigned) override;
  bool set_process_mode(int, int, int) override;
  bool set_mode(int, const glasswyrm::session::VirtualTerminalMode&) override;
  bool set_graphics_mode(int) override;
  bool set_kd_mode(int, int) override;
  bool set_keyboard_mode(int, int) override;
  bool acknowledge_release(int) override;
  bool acknowledge_acquire(int) override;
  void close_terminal(int) noexcept override;
  std::string last_error() const override;

  std::vector<std::string> calls;
};

class PresenterHarness {
 public:
  explicit PresenterHarness(gw::compositor::PresentationTiming timing = {},
                            bool enable_scene_manifest = false);
  ~PresenterHarness();

  PresenterHarness(const PresenterHarness&) = delete;
  PresenterHarness& operator=(const PresenterHarness&) = delete;

  [[nodiscard]] std::filesystem::path mirror_frame(
      std::uint64_t ordinal) const;
  [[nodiscard]] std::string report_contents() const;
  [[nodiscard]] std::filesystem::path scene_manifest_path() const;
  [[nodiscard]] std::string scene_manifest_contents() const;
  [[nodiscard]] bool shutdown(std::string& error);

  std::filesystem::path root;
  glasswyrm::drm::FakeDrmApi drm;
  glasswyrm::drm::FakeKmsApi kms;
  glasswyrm::drm::DrmReport report;
  glasswyrm::headless::FrameDumper mirror;
  FakeVirtualTerminal vt;
  glasswyrm::drm::DrmPresenter* presenter{};
  std::unique_ptr<gw::compositor::Compositor> compositor;
};

class IpcHarness {
 public:
  IpcHarness(const std::filesystem::path& socket_path, ProducerKind kind);
  ~IpcHarness();

  IpcHarness(const IpcHarness&) = delete;
  IpcHarness& operator=(const IpcHarness&) = delete;

  void send_snapshot(std::uint64_t buffer_id, std::uint32_t pixel,
                     std::uint64_t commit_id);
  void send_cursor_snapshot(std::uint64_t normal_buffer_id,
                            std::uint32_t normal_pixel,
                            std::uint64_t cursor_buffer_id,
                            std::uint32_t cursor_pixel,
                            std::uint64_t commit_id);
  void send_replacement(std::uint64_t buffer_id, std::uint32_t pixel,
                        std::uint64_t commit_id);
  void send_damage_commit(std::uint64_t commit_id);
  void pump_transport(int rounds = 8);
  [[nodiscard]] glasswyrm::compositor::ContractDispatchResult
  dispatch_until_frame(gw::compositor::Compositor& compositor);
  [[nodiscard]] std::vector<ClientEvent> drain_client();
  void disconnect_client();
  [[nodiscard]] bool server_closed();

  [[nodiscard]] gw::compositor::PeerProfile profile() const noexcept {
    return profile_;
  }
  [[nodiscard]] gwipc_role role() const noexcept { return role_; }
  [[nodiscard]] gwipc_connection* server() const noexcept {
    return server_.get();
  }

 private:
  struct ListenerDeleter {
    void operator()(gwipc_listener*) const;
  };
  struct ConnectionDeleter {
    void operator()(gwipc_connection*) const;
  };
  struct MessageDeleter {
    void operator()(gwipc_message*) const;
  };

  using Listener = std::unique_ptr<gwipc_listener, ListenerDeleter>;
  using Connection = std::unique_ptr<gwipc_connection, ConnectionDeleter>;
  using Message = std::unique_ptr<gwipc_message, MessageDeleter>;

  void enqueue_snapshot_begin(std::uint64_t item_count);
  void enqueue_snapshot_end(std::uint64_t item_count);
  void enqueue_output(std::uint32_t flags);
  void enqueue_surface(std::uint32_t flags);
  void enqueue_policy(std::uint32_t flags);
  void enqueue_buffer(std::uint64_t buffer_id, std::uint32_t pixel,
                      std::uint32_t flags);
  void enqueue_cursor(std::uint32_t flags);
  void enqueue_cursor_buffer(std::uint64_t buffer_id, std::uint32_t pixel,
                             std::uint32_t flags);
  void enqueue_commit(std::uint64_t commit_id);
  [[nodiscard]] Message next_server_message();

  std::filesystem::path socket_path_;
  Listener listener_;
  Connection client_;
  Connection server_;
  ProducerKind kind_;
  gwipc_role role_{};
  gw::compositor::PeerProfile profile_{};
};

[[nodiscard]] short ready_presentation_events(
    const gw::compositor::Compositor& compositor);
[[nodiscard]] bool has_ack(const std::vector<ClientEvent>& events,
                           std::uint64_t commit_id,
                           gwipc_frame_result result = GWIPC_FRAME_ACCEPTED);
[[nodiscard]] bool has_release(const std::vector<ClientEvent>& events,
                               std::uint64_t buffer_id);

}  // namespace gw::test::drm_ipc
