#include "gwcomp_m4_edge_scenarios.hpp"

#include <glasswyrm/ipc.h>

#include <poll.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <fcntl.h>
#include <memory>
#include <string>
#include <vector>

namespace {

constexpr std::uint64_t kCapabilities =
    GWIPC_CAP_FD_PASSING | GWIPC_CAP_SNAPSHOTS | GWIPC_CAP_OUTPUT_STATE |
    GWIPC_CAP_SURFACE_STATE | GWIPC_CAP_MEMFD_BUFFERS |
    GWIPC_CAP_DAMAGE_REGIONS | GWIPC_CAP_SDR_COLOR_METADATA |
    GWIPC_CAP_FRAME_ACKNOWLEDGEMENT;

struct ConnectionDeleter { void operator()(gwipc_connection* p) const { gwipc_connection_destroy(p); } };
struct MessageDeleter { void operator()(gwipc_message* p) const { gwipc_message_destroy(p); } };
struct PayloadDeleter { void operator()(gwipc_contract_payload* p) const { gwipc_contract_payload_destroy(p); } };
struct DecodedDeleter { void operator()(gwipc_decoded_contract* p) const { gwipc_decoded_contract_destroy(p); } };
using Connection = std::unique_ptr<gwipc_connection, ConnectionDeleter>;
using Message = std::unique_ptr<gwipc_message, MessageDeleter>;
using Payload = std::unique_ptr<gwipc_contract_payload, PayloadDeleter>;
using Decoded = std::unique_ptr<gwipc_decoded_contract, DecodedDeleter>;

void u16(std::vector<std::uint8_t>& b, std::uint16_t v) { b.push_back(v); b.push_back(v >> 8U); }
void u32(std::vector<std::uint8_t>& b, std::uint32_t v) { for (unsigned s = 0; s < 32; s += 8) b.push_back(v >> s); }
void u64(std::vector<std::uint8_t>& b, std::uint64_t v) { for (unsigned s = 0; s < 64; s += 8) b.push_back(v >> s); }

bool enqueue(gwipc_connection* c, std::uint16_t type, std::uint32_t flags,
             const std::uint8_t* bytes, std::size_t size, const int* fds = nullptr,
             std::size_t fd_count = 0) {
  gwipc_outgoing_message m{};
  m.struct_size = sizeof(m); m.type = type; m.flags = flags;
  m.payload = bytes; m.payload_size = size; m.fds = fds; m.fd_count = fd_count;
  return gwipc_connection_enqueue(c, &m) == GWIPC_STATUS_OK;
}

template <class T, class Encoder>
bool send(gwipc_connection* c, std::uint16_t type, std::uint32_t flags,
          const T& value, Encoder encoder, const int* fds = nullptr,
          std::size_t fd_count = 0) {
  gwipc_contract_payload* raw = nullptr;
  if (encoder(&value, &raw) != GWIPC_STATUS_OK) return false;
  const Payload payload(raw);
  std::size_t size = 0;
  const auto* bytes = gwipc_contract_payload_data(payload.get(), &size);
  return enqueue(c, type, flags, bytes, size, fds, fd_count);
}

bool pump(gwipc_connection* c, int timeout = 50) {
  pollfd p{gwipc_connection_fd(c), gwipc_connection_wanted_poll_events(c), 0};
  const int ready = ::poll(&p, 1, timeout);
  if (ready < 0) return errno == EINTR;
  if (ready && gwipc_connection_process_poll_events(c, p.revents) == GWIPC_STATUS_SYSTEM_ERROR) return false;
  return gwipc_connection_get_state(c) != GWIPC_CONNECTION_CLOSED;
}

Connection connect_to(const char* socket) {
  gwipc_connection_options options{};
  options.struct_size = sizeof(options); options.path = socket;
  options.local_role = GWIPC_ROLE_TEST_PRODUCER;
  options.acceptable_server_roles = GWIPC_ROLE_BIT(GWIPC_ROLE_COMPOSITOR);
  options.offered_capabilities = kCapabilities;
  options.required_peer_capabilities = kCapabilities;
  options.instance_label = "gwcomp-m4-edge-producer";
  gwipc_connection* raw = nullptr;
  const auto status = gwipc_connection_connect(&options, &raw);
  if (status != GWIPC_STATUS_OK && status != GWIPC_STATUS_IN_PROGRESS) return {};
  Connection result(raw);
  for (int i = 0; i < 200 && gwipc_connection_get_state(result.get()) != GWIPC_CONNECTION_ESTABLISHED; ++i)
    if (!pump(result.get())) return {};
  if (gwipc_connection_get_state(result.get()) != GWIPC_CONNECTION_ESTABLISHED) return {};
  return result;
}

gwipc_sdr_color_metadata srgb() {
  return {GWIPC_SDR_COLOR_SPACE_SRGB, GWIPC_TRANSFER_FUNCTION_SRGB,
          GWIPC_COLOR_PRIMARIES_SRGB, 0, 0, 0, 0};
}

int make_buffer(std::uint32_t width, std::uint32_t height, std::uint32_t pixel,
                bool sealed = true) {
  const std::size_t size = static_cast<std::size_t>(width) * height * 4U;
  const int fd = ::memfd_create("gwcomp-m4-edge", MFD_CLOEXEC | MFD_ALLOW_SEALING);
  if (fd < 0 || ::ftruncate(fd, static_cast<off_t>(size)) != 0) { if (fd >= 0) ::close(fd); return -1; }
  void* map = ::mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (map == MAP_FAILED) { ::close(fd); return -1; }
  auto* pixels = static_cast<std::uint32_t*>(map);
  for (std::size_t i = 0; i < size / 4U; ++i) pixels[i] = pixel;
  const bool ok = ::msync(map, size, MS_SYNC) == 0 && ::munmap(map, size) == 0 &&
                  (!sealed || ::fcntl(fd, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_GROW) == 0);
  if (!ok) { ::close(fd); return -1; }
  return fd;
}

gwipc_output_upsert output() {
  gwipc_output_upsert v{}; v.struct_size = sizeof(v); v.output_id = 1; v.enabled = 1;
  v.logical_width = v.physical_pixel_width = 16; v.logical_height = v.physical_pixel_height = 16;
  v.refresh_millihertz = 60'000; v.scale_numerator = v.scale_denominator = 1;
  v.transform = GWIPC_TRANSFORM_NORMAL; v.color = srgb(); return v;
}
gwipc_surface_upsert surface(std::uint64_t id = 1) {
  gwipc_surface_upsert v{}; v.struct_size = sizeof(v); v.surface_id = id; v.output_id = 1;
  v.logical_width = v.logical_height = 16; v.visible = 1; v.transform = GWIPC_TRANSFORM_NORMAL;
  v.opacity = GWIPC_OPACITY_ONE; v.scale_numerator = v.scale_denominator = 1; v.color = srgb(); return v;
}
gwipc_buffer_attach attachment(std::uint64_t buffer, std::uint64_t target = 1) {
  gwipc_buffer_attach v{}; v.struct_size = sizeof(v); v.buffer_id = buffer; v.surface_id = target;
  v.width = v.height = 16; v.stride = 64; v.storage_size = 1024;
  v.pixel_format = GWIPC_PIXEL_FORMAT_XRGB8888; v.alpha_semantics = GWIPC_ALPHA_OPAQUE;
  v.color = srgb(); return v;
}

bool begin(gwipc_connection* c, std::uint32_t items) {
  std::vector<std::uint8_t> b; u64(b, 1); u16(b, 4); u16(b, 0); u64(b, 1); u32(b, items); u32(b, 0);
  return enqueue(c, GWIPC_MESSAGE_SNAPSHOT_BEGIN, 0, b.data(), b.size());
}
bool end(gwipc_connection* c, std::uint32_t items) {
  std::vector<std::uint8_t> b; u64(b, 1); u64(b, 1); u32(b, items); u32(b, 0);
  return enqueue(c, GWIPC_MESSAGE_SNAPSHOT_END, 0, b.data(), b.size());
}
bool commit(gwipc_connection* c, std::uint64_t id) {
  gwipc_frame_commit v{}; v.struct_size = sizeof(v); v.commit_id = id; v.output_id = 1; v.producer_generation = 1;
  return send(c, GWIPC_MESSAGE_FRAME_COMMIT, GWIPC_FLAG_ACK_REQUIRED, v, gwipc_contract_encode_frame_commit);
}

bool initial(gwipc_connection* c, std::uint64_t buffer_id = 1) {
  if (!begin(c, 3)) return false;
  const auto o = output(); const auto s = surface(); auto a = attachment(buffer_id);
  const int fd = make_buffer(16, 16, UINT32_C(0xff203040));
  if (fd < 0) return false;
  const bool ok = send(c, GWIPC_MESSAGE_OUTPUT_UPSERT, GWIPC_FLAG_SNAPSHOT_ITEM, o, gwipc_contract_encode_output_upsert) &&
      send(c, GWIPC_MESSAGE_SURFACE_UPSERT, GWIPC_FLAG_SNAPSHOT_ITEM, s, gwipc_contract_encode_surface_upsert) &&
      send(c, GWIPC_MESSAGE_BUFFER_ATTACH, GWIPC_FLAG_SNAPSHOT_ITEM, a, gwipc_contract_encode_buffer_attach, &fd, 1) && end(c, 3) && commit(c, 100);
  ::close(fd); return ok;
}

bool await(gwipc_connection* c, std::uint64_t commit_id, gwipc_frame_result result,
           std::uint64_t released = 0, gwipc_buffer_release_reason reason = GWIPC_BUFFER_RELEASE_INVALID) {
  bool ack = false, release = released == 0;
  for (int i = 0; i < 300 && (!ack || !release); ++i) {
    if (!pump(c)) return false;
    for (;;) {
      gwipc_message* raw = nullptr;
      if (gwipc_connection_receive(c, &raw) != GWIPC_STATUS_OK) break;
      const Message message(raw); gwipc_decoded_contract* decoded_raw = nullptr;
      if (gwipc_contract_decode_message(message.get(), &decoded_raw) != GWIPC_STATUS_OK) continue;
      const Decoded decoded(decoded_raw);
      if (gwipc_message_type(message.get()) == GWIPC_MESSAGE_FRAME_ACKNOWLEDGED) {
        const auto* v = gwipc_decoded_frame_acknowledged(decoded.get());
        ack |= v && v->commit_id == commit_id && v->result == result;
      } else if (gwipc_message_type(message.get()) == GWIPC_MESSAGE_BUFFER_RELEASE) {
        const auto* v = gwipc_decoded_buffer_release(decoded.get());
        release |= v && v->buffer_id == released && v->reason == reason;
      }
    }
  }
  return ack && release;
}

bool buffer_replace(gwipc_connection* c) {
  if (!initial(c) || !await(c, 100, GWIPC_FRAME_ACCEPTED)) return false;
  auto a = attachment(2); const int fd = make_buffer(16, 16, UINT32_C(0xff506070));
  if (fd < 0) return false;
  const bool sent = send(c, GWIPC_MESSAGE_BUFFER_ATTACH, 0, a, gwipc_contract_encode_buffer_attach, &fd, 1) && commit(c, 101);
  ::close(fd);
  return sent && await(c, 101, GWIPC_FRAME_ACCEPTED, 1, GWIPC_BUFFER_RELEASE_REPLACED);
}

bool detach_remove(gwipc_connection* c) {
  if (!initial(c) || !await(c, 100, GWIPC_FRAME_ACCEPTED)) return false;
  gwipc_buffer_detach d{}; d.struct_size = sizeof(d); d.surface_id = d.buffer_id = 1;
  if (!send(c, GWIPC_MESSAGE_BUFFER_DETACH, 0, d, gwipc_contract_encode_buffer_detach) || !commit(c, 101) ||
      !await(c, 101, GWIPC_FRAME_REJECTED_INCOMPLETE_METADATA)) return false;
  gwipc_surface_remove r{}; r.struct_size = sizeof(r); r.surface_id = 1;
  return send(c, GWIPC_MESSAGE_SURFACE_REMOVE, 0, r, gwipc_contract_encode_surface_remove) &&
         commit(c, 102) && await(c, 102, GWIPC_FRAME_ACCEPTED, 1, GWIPC_BUFFER_RELEASE_SURFACE_REMOVED);
}

bool invalid_metadata(gwipc_connection* c) {
  if (!begin(c, 2)) return false;
  auto o = output(); o.scale_numerator = 2;
  const auto s = surface();
  return send(c, GWIPC_MESSAGE_OUTPUT_UPSERT, GWIPC_FLAG_SNAPSHOT_ITEM, o, gwipc_contract_encode_output_upsert) &&
         send(c, GWIPC_MESSAGE_SURFACE_UPSERT, GWIPC_FLAG_SNAPSHOT_ITEM, s, gwipc_contract_encode_surface_upsert) &&
         end(c, 2) && commit(c, 100) && await(c, 100, GWIPC_FRAME_REJECTED_INCOMPLETE_METADATA);
}

bool invalid_buffer(gwipc_connection* c) {
  if (!begin(c, 3)) return false;
  const auto o = output(); const auto s = surface(); auto a = attachment(1); a.width = 8; a.stride = 32; a.storage_size = 512;
  const int fd = make_buffer(8, 16, UINT32_C(0xff203040)); if (fd < 0) return false;
  const bool sent = send(c, GWIPC_MESSAGE_OUTPUT_UPSERT, GWIPC_FLAG_SNAPSHOT_ITEM, o, gwipc_contract_encode_output_upsert) &&
      send(c, GWIPC_MESSAGE_SURFACE_UPSERT, GWIPC_FLAG_SNAPSHOT_ITEM, s, gwipc_contract_encode_surface_upsert) &&
      send(c, GWIPC_MESSAGE_BUFFER_ATTACH, GWIPC_FLAG_SNAPSHOT_ITEM, a, gwipc_contract_encode_buffer_attach, &fd, 1) && end(c, 3) && commit(c, 100);
  ::close(fd); return sent && await(c, 100, GWIPC_FRAME_REJECTED_INVALID_BUFFER);
}

bool unknown_reference(gwipc_connection* c) {
  if (!begin(c, 2)) return false;
  const auto o = output(); auto a = attachment(1, 9); const int fd = make_buffer(16, 16, UINT32_C(0xff203040));
  if (fd < 0) return false;
  const bool sent = send(c, GWIPC_MESSAGE_OUTPUT_UPSERT, GWIPC_FLAG_SNAPSHOT_ITEM, o, gwipc_contract_encode_output_upsert) &&
      send(c, GWIPC_MESSAGE_BUFFER_ATTACH, GWIPC_FLAG_SNAPSHOT_ITEM, a, gwipc_contract_encode_buffer_attach, &fd, 1) && end(c, 2) && commit(c, 100);
  ::close(fd);
  auto s = surface(9);
  return sent && await(c, 100, GWIPC_FRAME_REJECTED_UNKNOWN_SURFACE) &&
         send(c, GWIPC_MESSAGE_SURFACE_UPSERT, 0, s, gwipc_contract_encode_surface_upsert) &&
         commit(c, 101) && await(c, 101, GWIPC_FRAME_ACCEPTED);
}

} // namespace

int run_gwcomp_m4_edge_scenario(const char* socket, std::string_view scenario) {
  const bool handled = scenario == "buffer-replace" || scenario == "detach-remove" ||
      scenario == "invalid-metadata" || scenario == "invalid-buffer" ||
      scenario == "unknown-reference" || scenario == "snapshot-reconnect";
  if (!handled) return -1;
  auto connection = connect_to(socket); if (!connection) return 1;
  bool ok = false;
  if (scenario == "buffer-replace") ok = buffer_replace(connection.get());
  else if (scenario == "detach-remove") ok = detach_remove(connection.get());
  else if (scenario == "invalid-metadata") ok = invalid_metadata(connection.get());
  else if (scenario == "invalid-buffer") ok = invalid_buffer(connection.get());
  else if (scenario == "unknown-reference") ok = unknown_reference(connection.get());
  else {
    ok = initial(connection.get()) && await(connection.get(), 100, GWIPC_FRAME_ACCEPTED);
    connection.reset();
    if (ok) {
      connection = connect_to(socket);
      ok = connection && initial(connection.get(), 2) && await(connection.get(), 100, GWIPC_FRAME_ACCEPTED);
    }
  }
  return ok ? 0 : 1;
}
