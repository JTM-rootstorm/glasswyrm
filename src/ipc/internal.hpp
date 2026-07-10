#ifndef GLASSWYRM_SRC_IPC_INTERNAL_HPP
#define GLASSWYRM_SRC_IPC_INTERNAL_HPP

#include <glasswyrm/ipc.h>

#include <sys/types.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include <vector>

namespace gw::ipc {

inline constexpr std::uint32_t kKnownCapabilities =
    GWIPC_CAP_FD_PASSING | GWIPC_CAP_SNAPSHOTS | GWIPC_CAP_OUTPUT_STATE |
    GWIPC_CAP_SURFACE_STATE | GWIPC_CAP_MEMFD_BUFFERS |
    GWIPC_CAP_DAMAGE_REGIONS | GWIPC_CAP_SCALE_METADATA |
    GWIPC_CAP_SDR_COLOR_METADATA | GWIPC_CAP_FRAME_ACKNOWLEDGEMENT |
    GWIPC_CAP_TRACE_METADATA;

struct Config {
  std::string path;
  gwipc_role local_role{GWIPC_ROLE_UNKNOWN};
  std::uint64_t peer_roles{};
  std::uint64_t offered_capabilities{};
  std::uint64_t required_peer_capabilities{};
  std::uint32_t maximum_payload{GWIPC_DEFAULT_MAXIMUM_PAYLOAD};
  std::uint16_t maximum_fd_count{GWIPC_DEFAULT_MAXIMUM_FDS};
  std::uint32_t maximum_queued_bytes{GWIPC_DEFAULT_MAXIMUM_QUEUED_BYTES};
  std::uint16_t maximum_queued_messages{GWIPC_DEFAULT_MAXIMUM_QUEUED_MESSAGES};
  std::string label;
  std::array<std::uint8_t, 16> instance_id{};
  bool require_same_uid{true};
};

struct QueuedRecord {
  std::vector<std::uint8_t> bytes;
  std::vector<int> fds;
  std::uint64_t sequence{};

  QueuedRecord() = default;
  QueuedRecord(const QueuedRecord&) = delete;
  QueuedRecord& operator=(const QueuedRecord&) = delete;
  QueuedRecord(QueuedRecord&& other) noexcept;
  QueuedRecord& operator=(QueuedRecord&& other) noexcept;
  ~QueuedRecord();
};

struct SnapshotState {
  bool active{};
  std::uint64_t id{};
  std::uint64_t generation{};
  std::uint32_t expected_count{};
  std::uint32_t item_count{};
};

bool fill_instance_id(std::array<std::uint8_t, 16>& id) noexcept;
bool valid_role(gwipc_role role) noexcept;
gwipc_status validate_config(const Config& config) noexcept;
void close_fd(int& fd) noexcept;

}  // namespace gw::ipc

struct gwipc_message {
  std::uint16_t type{};
  std::uint32_t flags{};
  std::uint64_t sequence{};
  std::uint64_t reply_to{};
  std::vector<std::uint8_t> payload;
  std::vector<int> fds;

  ~gwipc_message();
};

struct gwipc_listener {
  int fd{-1};
  int system_errno{};
  std::string path;
  dev_t bound_device{};
  ino_t bound_inode{};
  gw::ipc::Config config;
  std::uint64_t next_connection_id{1};
};

struct gwipc_connection {
  int fd{-1};
  int system_errno{};
  gwipc_connection_state state{GWIPC_CONNECTION_CLOSED};
  bool server_side{};
  bool peer_credentials_read{};
  bool close_after_flush{};
  bool snapshot_aborted{};
  gw::ipc::Config config;
  gwipc_peer_info peer{};
  std::uint64_t assigned_connection_id{};
  std::uint64_t next_send_sequence{1};
  std::uint64_t next_receive_sequence{1};
  std::size_t queued_bytes{};
  std::deque<gw::ipc::QueuedRecord> outgoing;
  std::deque<gwipc_message*> incoming;
  std::unordered_set<std::uint64_t> pending_replies;
  std::unordered_map<std::uint64_t, std::uint64_t> pending_ping_nonces;
  gw::ipc::SnapshotState outgoing_snapshot;
  gw::ipc::SnapshotState incoming_snapshot;

  ~gwipc_connection();
};

#endif
