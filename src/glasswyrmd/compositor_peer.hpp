#pragma once

#include "glasswyrmd/compositor_output_inventory.hpp"
#include "glasswyrmd/peer_transport.hpp"
#include "glasswyrmd/vrr_state_cache.hpp"
#include "protocol/x11/screen_model.hpp"

#include <optional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace glasswyrm::server {

struct CompositorSnapshotSubmission {
  struct Buffer {
    gwipc_buffer_attach attach{};
    int fd{-1};
    int synchronization_fd{-1};
  };
  struct Damage {
    std::uint64_t surface_id{};
    std::vector<gwipc_damage_rectangle> rectangles;
  };
  struct SurfaceOutput {
    gwipc_surface_output_state state{};
    std::vector<std::uint64_t> output_ids;
  };

  CompositorSnapshotSubmission() = default;
  CompositorSnapshotSubmission(
      const std::uint64_t commit, const std::uint64_t producer_generation,
      std::vector<gwipc_surface_upsert> surface_records,
      std::vector<gwipc_surface_policy_upsert> policy_records,
      std::vector<Buffer> buffer_records, std::vector<Damage> damage_records)
      : commit_id(commit), generation(producer_generation),
        surfaces(std::move(surface_records)),
        policies(std::move(policy_records)),
        buffers(std::move(buffer_records)), damages(std::move(damage_records)) {}

  std::uint64_t commit_id{}, generation{};
  std::uint64_t primary_output_id{}, output_layout_generation{};
  std::vector<gwipc_surface_upsert> surfaces;
  std::vector<gwipc_surface_policy_upsert> policies;
  std::vector<Buffer> buffers;
  std::vector<Damage> damages;
  std::vector<gwipc_output_upsert> outputs;
  std::vector<SurfaceOutput> surface_outputs;
  std::vector<gwipc_output_vrr_policy_upsert> output_vrr_policies;
  std::vector<gwipc_surface_vrr_state> surface_vrr_states;
};

struct CompositorContentSubmission {
  std::uint64_t commit_id{}, generation{};
  std::vector<CompositorSnapshotSubmission::Buffer> buffers;
  std::vector<CompositorSnapshotSubmission::Damage> damages;
};

struct CompositorCursorSubmission {
  gwipc_surface_upsert surface{};
  std::optional<CompositorSnapshotSubmission::Buffer> buffer;
  std::optional<CompositorSnapshotSubmission::Damage> damage;
  CompositorSnapshotSubmission::SurfaceOutput surface_output;
  std::int32_t pointer_x{}, pointer_y{};
};

struct CompositorBufferRelease {
  std::uint64_t buffer_id{};
  gwipc_buffer_release_reason reason{GWIPC_BUFFER_RELEASE_INVALID};
};

struct CompositorSessionStateChange {
  gwipc_session_state_change change{};
  std::uint64_t sequence{};
};

class CompositorPeer {
public:
  CompositorPeer(std::string path, gw::protocol::x11::ScreenModel screen,
                 bool software_content = false,
                 bool session_state = false,
                 bool cpu_buffer_synchronization = false,
                 bool output_model = false, bool vrr_profile = false);
  [[nodiscard]] bool connect(std::string &error);
  [[nodiscard]] PeerProcessOutcome process(short revents, std::string &error);
  [[nodiscard]] int fd() const noexcept { return transport_.fd(); }
  [[nodiscard]] short wanted_events() const noexcept {
    return transport_.wanted_events();
  }
  [[nodiscard]] PeerBootstrapState state() const noexcept { return state_; }
  [[nodiscard]] bool submit(const CompositorSnapshotSubmission &submission,
                            std::string &error);
  [[nodiscard]] bool submit_content(
      const CompositorContentSubmission& submission, std::string& error);
  [[nodiscard]] bool submit_cursor(
      const CompositorCursorSubmission& submission, std::uint64_t commit_id,
      std::uint64_t generation, std::string& error);
  [[nodiscard]] std::vector<CompositorBufferRelease> take_releases();
  [[nodiscard]] std::vector<CompositorSessionStateChange>
  take_session_state_changes();
  [[nodiscard]] const VrrResponseBatch& vrr_response() const noexcept {
    return vrr_response_;
  }
  [[nodiscard]] VrrStateCache* vrr_cache() noexcept {
    return vrr_profile_ ? &vrr_cache_ : nullptr;
  }
  [[nodiscard]] const VrrStateCache* vrr_cache() const noexcept {
    return vrr_profile_ ? &vrr_cache_ : nullptr;
  }
  [[nodiscard]] bool acknowledge_session_state(
      const CompositorSessionStateChange& request,
      gwipc_session_state_result result, std::string& error);
  [[nodiscard]] const CompositorSnapshotSubmission &
  replay_input() const noexcept {
    return replay_input_;
  }
  [[nodiscard]] const output::OutputLayout *output_layout() const noexcept {
    return output_layout_ ? &*output_layout_ : nullptr;
  }
  [[nodiscard]] bool
  can_adopt_output_layout(const output::OutputLayout& layout) const noexcept;
  [[nodiscard]] bool adopt_output_layout(output::OutputLayout layout);
  void forget_cursor_replay() noexcept;
  void disconnect() noexcept;

private:
  [[nodiscard]] bool send_bootstrap(std::string &error);
  [[nodiscard]] bool begin_output_inventory(std::string &error);
  [[nodiscard]] bool accept_output_inventory(std::string &error);
  void retain_cursor_records(CompositorSnapshotSubmission& submission) const;
  [[nodiscard]] bool validate_surface_membership_records(
      const CompositorSnapshotSubmission& submission,
      std::string& error) const;
  [[nodiscard]] bool validate_buffer_damage_records(
      const CompositorSnapshotSubmission& submission,
      std::string& error) const;
  [[nodiscard]] bool validate_surface_policy_links(
      const CompositorSnapshotSubmission& submission,
      std::string& error) const;
  [[nodiscard]] bool enqueue_output_records(
      const CompositorSnapshotSubmission& submission,
      std::uint32_t item_count, std::string& error);
  [[nodiscard]] bool enqueue_surface_membership_records(
      const CompositorSnapshotSubmission& submission, std::string& error);
  [[nodiscard]] bool enqueue_buffer_damage_records(
      const CompositorSnapshotSubmission& submission, std::string& error);
  [[nodiscard]] bool enqueue_snapshot_completion_records(
      const CompositorSnapshotSubmission& submission,
      std::uint32_t item_count, std::string& error);
  [[nodiscard]] bool validate_vrr_submission(
      const CompositorSnapshotSubmission& submission,
      std::string& error) const;
  [[nodiscard]] PeerProcessOutcome finish_vrr_response(std::string& error);
  [[nodiscard]] PeerProcessOutcome drain(std::string &error);

  PeerTransport transport_;
  gw::protocol::x11::ScreenModel screen_;
  PeerBootstrapState state_{PeerBootstrapState::Disconnected};
  std::uint64_t commit_sequence_{};
  CompositorSnapshotSubmission pending_;
  CompositorSnapshotSubmission replay_input_;
  CompositorContentSubmission pending_content_;
  std::vector<CompositorBufferRelease> releases_;
  bool software_content_{};
  bool session_state_{};
  bool output_model_{};
  bool vrr_profile_{};
  bool frame_acknowledged_{};
  VrrResponseBatch vrr_response_;
  VrrStateCache vrr_cache_;
  bool content_submission_{};
  std::vector<CompositorSessionStateChange> session_state_changes_;
  std::unique_ptr<CompositorOutputInventory> pending_output_inventory_;
  std::optional<output::OutputLayout> reference_output_inventory_;
  std::optional<output::OutputLayout> output_layout_;
  std::uint64_t next_output_query_id_{1};
};

} // namespace glasswyrm::server
