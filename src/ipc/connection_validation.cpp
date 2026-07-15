#include "ipc/connection_internal.hpp"

#include "ipc/wire/compositor_contract.hpp"
#include "ipc/wire/control.hpp"
#include "ipc/wire/input_contract.hpp"
#include "ipc/wire/lifecycle_contract.hpp"
#include "ipc/wire/policy_contract.hpp"

#include <fcntl.h>
#include <sys/stat.h>

namespace gw::ipc {
namespace {

std::uint64_t required_capability(std::uint16_t type) noexcept {
  switch (type) {
    case GWIPC_MESSAGE_SNAPSHOT_BEGIN:
    case GWIPC_MESSAGE_SNAPSHOT_END:
    case GWIPC_MESSAGE_SNAPSHOT_ABORT:
      return GWIPC_CAP_SNAPSHOTS;
    case GWIPC_MESSAGE_OUTPUT_UPSERT:
    case GWIPC_MESSAGE_OUTPUT_REMOVE:
      return GWIPC_CAP_OUTPUT_STATE;
    case GWIPC_MESSAGE_SURFACE_UPSERT:
    case GWIPC_MESSAGE_SURFACE_REMOVE:
      return GWIPC_CAP_SURFACE_STATE;
    case GWIPC_MESSAGE_SURFACE_POLICY_UPSERT:
      return GWIPC_CAP_SURFACE_STATE | GWIPC_CAP_WINDOW_LIFECYCLE;
    case GWIPC_MESSAGE_BUFFER_ATTACH:
      return GWIPC_CAP_FD_PASSING | GWIPC_CAP_MEMFD_BUFFERS;
    case GWIPC_MESSAGE_SURFACE_DAMAGE:
      return GWIPC_CAP_DAMAGE_REGIONS;
    case GWIPC_MESSAGE_FRAME_ACKNOWLEDGED:
      return GWIPC_CAP_FRAME_ACKNOWLEDGEMENT;
    case GWIPC_MESSAGE_POLICY_CONTEXT_UPSERT:
    case GWIPC_MESSAGE_POLICY_WINDOW_UPSERT:
    case GWIPC_MESSAGE_POLICY_WINDOW_REMOVE:
    case GWIPC_MESSAGE_POLICY_COMMIT:
    case GWIPC_MESSAGE_POLICY_WINDOW_STATE:
    case GWIPC_MESSAGE_POLICY_ACKNOWLEDGED:
      return GWIPC_CAP_WINDOW_POLICY;
    case GWIPC_MESSAGE_POLICY_LIFECYCLE_WINDOW_UPSERT:
      return GWIPC_CAP_WINDOW_POLICY | GWIPC_CAP_WINDOW_LIFECYCLE;
    case GWIPC_MESSAGE_SYNTHETIC_MOTION:
    case GWIPC_MESSAGE_SYNTHETIC_BUTTON:
    case GWIPC_MESSAGE_SYNTHETIC_KEY:
    case GWIPC_MESSAGE_SYNTHETIC_BARRIER:
    case GWIPC_MESSAGE_SYNTHETIC_INPUT_ACKNOWLEDGED:
      return GWIPC_CAP_SYNTHETIC_INPUT;
    default:
      return 0;
  }
}

gwipc_status validate_snapshot(SnapshotState& state, std::uint16_t type,
                               std::uint32_t flags,
                               std::span<const std::uint8_t> payload) {
  const bool item = (flags & GWIPC_FLAG_SNAPSHOT_ITEM) != 0;
  if (snapshot_control(type) && item) return GWIPC_STATUS_PROTOCOL_ERROR;
  if (type == GWIPC_MESSAGE_SNAPSHOT_BEGIN) {
    wire::SnapshotBegin begin;
    if (state.active || wire::decode(payload, begin) != wire::CodecStatus::Ok)
      return GWIPC_STATUS_PROTOCOL_ERROR;
    state = {true, begin.snapshot_id, begin.generation,
             begin.expected_item_count, 0};
    return GWIPC_STATUS_OK;
  }
  if (type == GWIPC_MESSAGE_SNAPSHOT_END) {
    wire::SnapshotEnd end;
    if (!state.active || wire::decode(payload, end) != wire::CodecStatus::Ok ||
        end.snapshot_id != state.id || end.generation != state.generation ||
        end.actual_item_count != state.item_count ||
        (state.expected_count != UINT32_MAX &&
         state.expected_count != state.item_count))
      return GWIPC_STATUS_PROTOCOL_ERROR;
    state = {};
    return GWIPC_STATUS_OK;
  }
  if (type == GWIPC_MESSAGE_SNAPSHOT_ABORT) {
    wire::SnapshotAbort abort;
    if (!state.active || wire::decode(payload, abort) != wire::CodecStatus::Ok ||
        abort.snapshot_id != state.id)
      return GWIPC_STATUS_PROTOCOL_ERROR;
    state = {};
    return GWIPC_STATUS_OK;
  }
  if (item) {
    if (!state.active || state.item_count == UINT32_MAX)
      return GWIPC_STATUS_PROTOCOL_ERROR;
    ++state.item_count;
  }
  return GWIPC_STATUS_OK;
}

gwipc_status validate_buffer_attach(std::span<const std::uint8_t> payload,
                                    std::span<const int> fds,
                                    wire::CodecStatus& codec) {
  wire::BufferAttach value;
  codec = wire::decode(payload, value);
  if (codec != wire::CodecStatus::Ok || fds.size() != 1) return GWIPC_STATUS_OK;
  struct stat status {};
  const int descriptor_flags = ::fcntl(fds[0], F_GETFL);
  if (descriptor_flags < 0 || (descriptor_flags & O_PATH) != 0 ||
      (descriptor_flags & O_ACCMODE) == O_WRONLY ||
      ::fstat(fds[0], &status) < 0 || !S_ISREG(status.st_mode) ||
      status.st_size < 0 ||
      static_cast<std::uint64_t>(status.st_size) < value.storage_size)
    return GWIPC_STATUS_PROTOCOL_ERROR;
  return GWIPC_STATUS_OK;
}

gwipc_status validate_control(std::uint16_t type, std::uint32_t flags,
                              std::span<const std::uint8_t> payload,
                              wire::CodecStatus& codec) {
  switch (type) {
    case GWIPC_MESSAGE_PING: {
      wire::Ping value;
      codec = wire::decode(payload, value);
      return flags == GWIPC_FLAG_ACK_REQUIRED ? GWIPC_STATUS_OK
                                               : GWIPC_STATUS_PROTOCOL_ERROR;
    }
    case GWIPC_MESSAGE_PONG: {
      wire::Pong value;
      codec = wire::decode(payload, value);
      return flags == GWIPC_FLAG_REPLY ? GWIPC_STATUS_OK
                                       : GWIPC_STATUS_PROTOCOL_ERROR;
    }
    case GWIPC_MESSAGE_PROTOCOL_ERROR: {
      wire::ProtocolError value;
      codec = wire::decode(payload, value);
      return flags == (GWIPC_FLAG_REPLY | GWIPC_FLAG_ERROR)
                 ? GWIPC_STATUS_OK
                 : GWIPC_STATUS_PROTOCOL_ERROR;
    }
    case GWIPC_MESSAGE_SNAPSHOT_BEGIN: {
      wire::SnapshotBegin value;
      codec = wire::decode(payload, value);
      return GWIPC_STATUS_OK;
    }
    case GWIPC_MESSAGE_SNAPSHOT_END: {
      wire::SnapshotEnd value;
      codec = wire::decode(payload, value);
      return GWIPC_STATUS_OK;
    }
    default: {
      wire::SnapshotAbort value;
      codec = wire::decode(payload, value);
      return GWIPC_STATUS_OK;
    }
  }
}

gwipc_status validate_compositor(gwipc_connection& connection,
                                 std::uint16_t type, std::uint32_t flags,
                                 std::span<const std::uint8_t> payload,
                                 std::span<const int> fds,
                                 wire::CodecStatus& codec) {
  switch (type) {
    case GWIPC_MESSAGE_OUTPUT_UPSERT: { wire::OutputUpsert v; codec=wire::decode(payload,v); break; }
    case GWIPC_MESSAGE_OUTPUT_REMOVE: { wire::OutputRemove v; codec=wire::decode(payload,v); break; }
    case GWIPC_MESSAGE_SURFACE_UPSERT: {
      wire::SurfaceUpsert v; codec=wire::decode(payload,v);
      if (codec == wire::CodecStatus::Ok && v.presentation_flags != 0 &&
          (connection.peer.capabilities & GWIPC_CAP_WINDOW_LIFECYCLE) == 0)
        return GWIPC_STATUS_CAPABILITY_MISMATCH;
      break;
    }
    case GWIPC_MESSAGE_SURFACE_REMOVE: { wire::SurfaceRemove v; codec=wire::decode(payload,v); break; }
    case GWIPC_MESSAGE_BUFFER_ATTACH:
      return validate_buffer_attach(payload, fds, codec);
    case GWIPC_MESSAGE_BUFFER_DETACH: { wire::BufferDetach v; codec=wire::decode(payload,v); break; }
    case GWIPC_MESSAGE_BUFFER_RELEASE: { wire::BufferRelease v; codec=wire::decode(payload,v); break; }
    case GWIPC_MESSAGE_SURFACE_DAMAGE: { wire::SurfaceDamage v; codec=wire::decode(payload,v); break; }
    case GWIPC_MESSAGE_FRAME_COMMIT: {
      wire::FrameCommit v; codec=wire::decode(payload,v);
      if (flags != GWIPC_FLAG_ACK_REQUIRED) return GWIPC_STATUS_PROTOCOL_ERROR;
      break;
    }
    default: {
      wire::FrameAcknowledged v; codec=wire::decode(payload,v);
      if (flags != GWIPC_FLAG_REPLY) return GWIPC_STATUS_PROTOCOL_ERROR;
    }
  }
  return GWIPC_STATUS_OK;
}

gwipc_status validate_policy(std::uint16_t type, std::uint32_t flags,
                             std::span<const std::uint8_t> payload,
                             const SnapshotState& snapshot,
                             wire::CodecStatus& codec) {
  switch (type) {
    case GWIPC_MESSAGE_POLICY_CONTEXT_UPSERT: { wire::PolicyContextUpsert v; codec=wire::decode(payload,v); if(flags!=0&&flags!=GWIPC_FLAG_SNAPSHOT_ITEM)return GWIPC_STATUS_PROTOCOL_ERROR; break; }
    case GWIPC_MESSAGE_POLICY_WINDOW_UPSERT: { wire::PolicyWindowUpsert v; codec=wire::decode(payload,v); if(flags!=0&&flags!=GWIPC_FLAG_SNAPSHOT_ITEM)return GWIPC_STATUS_PROTOCOL_ERROR; break; }
    case GWIPC_MESSAGE_POLICY_WINDOW_REMOVE: { wire::PolicyWindowRemove v; codec=wire::decode(payload,v); if(flags!=0)return GWIPC_STATUS_PROTOCOL_ERROR; break; }
    case GWIPC_MESSAGE_POLICY_COMMIT: { wire::PolicyCommit v; codec=wire::decode(payload,v); if(flags!=GWIPC_FLAG_ACK_REQUIRED||snapshot.active)return GWIPC_STATUS_PROTOCOL_ERROR; break; }
    case GWIPC_MESSAGE_POLICY_WINDOW_STATE: { wire::PolicyWindowState v; codec=wire::decode(payload,v); if(flags!=GWIPC_FLAG_SNAPSHOT_ITEM)return GWIPC_STATUS_PROTOCOL_ERROR; break; }
    case GWIPC_MESSAGE_POLICY_ACKNOWLEDGED: { wire::PolicyAcknowledged v; codec=wire::decode(payload,v); if(flags!=GWIPC_FLAG_REPLY)return GWIPC_STATUS_PROTOCOL_ERROR; break; }
    case GWIPC_MESSAGE_POLICY_LIFECYCLE_WINDOW_UPSERT: { wire::PolicyLifecycleWindowUpsert v; codec=wire::decode(payload,v); if(flags!=0&&flags!=GWIPC_FLAG_SNAPSHOT_ITEM)return GWIPC_STATUS_PROTOCOL_ERROR; break; }
    default: { wire::SurfacePolicyUpsert v; codec=wire::decode(payload,v); if(flags!=GWIPC_FLAG_SNAPSHOT_ITEM)return GWIPC_STATUS_PROTOCOL_ERROR; }
  }
  return GWIPC_STATUS_OK;
}

gwipc_status validate_input(std::uint16_t type, std::uint32_t flags,
                            std::span<const std::uint8_t> payload,
                            wire::CodecStatus& codec) {
  switch (type) {
    case GWIPC_MESSAGE_SYNTHETIC_MOTION: { wire::SyntheticMotion v; codec=wire::decode(payload,v); break; }
    case GWIPC_MESSAGE_SYNTHETIC_BUTTON: { wire::SyntheticButton v; codec=wire::decode(payload,v); break; }
    case GWIPC_MESSAGE_SYNTHETIC_KEY: { wire::SyntheticKey v; codec=wire::decode(payload,v); break; }
    case GWIPC_MESSAGE_SYNTHETIC_BARRIER: { wire::SyntheticBarrier v; codec=wire::decode(payload,v); break; }
    default: { wire::SyntheticInputAcknowledged v; codec=wire::decode(payload,v); return flags==GWIPC_FLAG_REPLY?GWIPC_STATUS_OK:GWIPC_STATUS_PROTOCOL_ERROR; }
  }
  return flags == GWIPC_FLAG_ACK_REQUIRED ? GWIPC_STATUS_OK
                                           : GWIPC_STATUS_PROTOCOL_ERROR;
}

}  // namespace

bool snapshot_control(std::uint16_t type) noexcept {
  return type >= GWIPC_MESSAGE_SNAPSHOT_BEGIN &&
         type <= GWIPC_MESSAGE_SNAPSHOT_ABORT;
}

gwipc_status validate_application(gwipc_connection& connection,
                                  std::uint16_t type, std::uint32_t flags,
                                  std::span<const std::uint8_t> payload,
                                  std::span<const int> fds,
                                  SnapshotState& snapshot) {
  const auto required = required_capability(type);
  if ((required & connection.peer.capabilities) != required)
    return GWIPC_STATUS_CAPABILITY_MISMATCH;
  wire::CodecStatus codec = wire::CodecStatus::InvalidValue;
  gwipc_status status = GWIPC_STATUS_PROTOCOL_ERROR;
  if ((type >= GWIPC_MESSAGE_PING && type <= GWIPC_MESSAGE_PROTOCOL_ERROR) ||
      snapshot_control(type))
    status = validate_control(type, flags, payload, codec);
  else if (type == GWIPC_MESSAGE_SURFACE_POLICY_UPSERT ||
           (type >= GWIPC_MESSAGE_POLICY_CONTEXT_UPSERT &&
            type <= GWIPC_MESSAGE_POLICY_ACKNOWLEDGED))
    status = validate_policy(type, flags, payload, snapshot, codec);
  else if (type >= GWIPC_MESSAGE_OUTPUT_UPSERT &&
           type <= GWIPC_MESSAGE_FRAME_ACKNOWLEDGED)
    status = validate_compositor(connection, type, flags, payload, fds, codec);
  else if (type >= GWIPC_MESSAGE_SYNTHETIC_MOTION &&
           type <= GWIPC_MESSAGE_SYNTHETIC_INPUT_ACKNOWLEDGED)
    status = validate_input(type, flags, payload, codec);
  if (status != GWIPC_STATUS_OK) return status;
  if (codec != wire::CodecStatus::Ok) return GWIPC_STATUS_PROTOCOL_ERROR;
  if (type == GWIPC_MESSAGE_BUFFER_ATTACH) {
    if (fds.size() != 1) return GWIPC_STATUS_PROTOCOL_ERROR;
  } else if (!fds.empty()) {
    return GWIPC_STATUS_PROTOCOL_ERROR;
  }
  return validate_snapshot(snapshot, type, flags, payload);
}

}  // namespace gw::ipc
