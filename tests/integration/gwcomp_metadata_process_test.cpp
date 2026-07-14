#include <glasswyrm/ipc.h>

#include <poll.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>

namespace {
[[noreturn]] void fail(const char *message) {
  std::fprintf(stderr, "gwcomp metadata process test: %s\n", message);
  std::exit(1);
}
void require(bool value, const char *message) {
  if (!value)
    fail(message);
}
struct CD {
  void operator()(gwipc_connection *p) const { gwipc_connection_destroy(p); }
};
struct MD {
  void operator()(gwipc_message *p) const { gwipc_message_destroy(p); }
};
struct PD {
  void operator()(gwipc_contract_payload *p) const {
    gwipc_contract_payload_destroy(p);
  }
};
struct XD {
  void operator()(gwipc_control_payload *p) const {
    gwipc_control_payload_destroy(p);
  }
};
struct DD {
  void operator()(gwipc_decoded_contract *p) const {
    gwipc_decoded_contract_destroy(p);
  }
};
using Connection = std::unique_ptr<gwipc_connection, CD>;
using Message = std::unique_ptr<gwipc_message, MD>;
bool pump(gwipc_connection *c) {
  pollfd p{gwipc_connection_fd(c), gwipc_connection_wanted_poll_events(c), 0};
  int n = ::poll(&p, 1, 50);
  if (n < 0)
    return errno == EINTR;
  if (n)
    (void)gwipc_connection_process_poll_events(c, p.revents);
  return gwipc_connection_get_state(c) != GWIPC_CONNECTION_CLOSED;
}
Connection connect_to(const std::string &path, bool buffered) {
  gwipc_connection_options o{};
  o.struct_size = sizeof(o);
  o.path = path.c_str();
  o.local_role = GWIPC_ROLE_PROTOCOL_SERVER;
  o.acceptable_server_roles = GWIPC_ROLE_BIT(GWIPC_ROLE_COMPOSITOR);
  o.offered_capabilities =
      GWIPC_CAP_SNAPSHOTS | GWIPC_CAP_OUTPUT_STATE | GWIPC_CAP_SURFACE_STATE |
      GWIPC_CAP_SDR_COLOR_METADATA | GWIPC_CAP_FRAME_ACKNOWLEDGEMENT |
      GWIPC_CAP_WINDOW_LIFECYCLE;
  if (buffered)
    o.offered_capabilities |= GWIPC_CAP_FD_PASSING | GWIPC_CAP_MEMFD_BUFFERS |
                              GWIPC_CAP_DAMAGE_REGIONS;
  o.required_peer_capabilities = o.offered_capabilities;
  o.instance_label = "metadata-process-test";
  gwipc_connection *raw = nullptr;
  auto s = gwipc_connection_connect(&o, &raw);
  require(s == GWIPC_STATUS_OK || s == GWIPC_STATUS_IN_PROGRESS,
          "connect ProtocolServer");
  Connection c(raw);
  for (int i = 0; i < 200 && gwipc_connection_get_state(c.get()) !=
                                 GWIPC_CONNECTION_ESTABLISHED;
       ++i)
    require(pump(c.get()), "drive handshake");
  require(gwipc_connection_get_state(c.get()) == GWIPC_CONNECTION_ESTABLISHED,
          "ProtocolServer establishes");
  return c;
}
template <class V, class E>
void contract(gwipc_connection *c, std::uint16_t type, std::uint32_t flags,
              const V &v, E encode) {
  gwipc_contract_payload *raw = nullptr;
  require(encode(&v, &raw) == GWIPC_STATUS_OK, "encode contract");
  std::unique_ptr<gwipc_contract_payload, PD> p(raw);
  std::size_t n = 0;
  auto *b = gwipc_contract_payload_data(p.get(), &n);
  gwipc_outgoing_message m{};
  m.struct_size = sizeof(m);
  m.type = type;
  m.flags = flags;
  m.payload = b;
  m.payload_size = n;
  require(gwipc_connection_enqueue(c, &m) == GWIPC_STATUS_OK,
          "enqueue contract");
}
template <class V, class E>
void contract_fd(gwipc_connection *c, std::uint16_t type, std::uint32_t flags,
                 const V &v, E encode, int fd) {
  gwipc_contract_payload *raw = nullptr;
  require(encode(&v, &raw) == GWIPC_STATUS_OK, "encode descriptor contract");
  std::unique_ptr<gwipc_contract_payload, PD> p(raw);
  std::size_t n = 0;
  auto *b = gwipc_contract_payload_data(p.get(), &n);
  gwipc_outgoing_message m{};
  m.struct_size = sizeof(m);
  m.type = type;
  m.flags = flags;
  m.payload = b;
  m.payload_size = n;
  m.fds = &fd;
  m.fd_count = 1;
  require(gwipc_connection_enqueue(c, &m) == GWIPC_STATUS_OK,
          "enqueue descriptor contract");
  (void)::close(fd);
}
template <class V, class E>
void control(gwipc_connection *c, std::uint16_t type, const V &v, E encode) {
  gwipc_control_payload *raw = nullptr;
  require(encode(&v, &raw) == GWIPC_STATUS_OK, "encode control");
  std::unique_ptr<gwipc_control_payload, XD> p(raw);
  std::size_t n = 0;
  auto *b = gwipc_control_payload_data(p.get(), &n);
  gwipc_outgoing_message m{};
  m.struct_size = sizeof(m);
  m.type = type;
  m.payload = b;
  m.payload_size = n;
  require(gwipc_connection_enqueue(c, &m) == GWIPC_STATUS_OK,
          "enqueue control");
}
gwipc_sdr_color_metadata srgb() {
  return {GWIPC_SDR_COLOR_SPACE_SRGB,
          GWIPC_TRANSFER_FUNCTION_SRGB,
          GWIPC_COLOR_PRIMARIES_SRGB,
          0,
          0,
          0,
          0};
}
void send_scene(gwipc_connection *c, bool buffered) {
  gwipc_snapshot_begin b{};
  b.struct_size = sizeof(b);
  b.snapshot_id = 1;
  b.domain = GWIPC_SNAPSHOT_COMPLETE_SESSION;
  b.generation = 1;
  b.expected_item_count = buffered ? 4 : 3;
  control(c, GWIPC_MESSAGE_SNAPSHOT_BEGIN, b,
          gwipc_control_encode_snapshot_begin);
  gwipc_output_upsert o{};
  o.struct_size = sizeof(o);
  o.output_id = 1;
  o.enabled = 1;
  o.logical_width = o.physical_pixel_width = 1024;
  o.logical_height = o.physical_pixel_height = 768;
  o.refresh_millihertz = 60000;
  o.scale_numerator = o.scale_denominator = 1;
  o.transform = GWIPC_TRANSFORM_NORMAL;
  o.color = srgb();
  contract(c, GWIPC_MESSAGE_OUTPUT_UPSERT, GWIPC_FLAG_SNAPSHOT_ITEM, o,
           gwipc_contract_encode_output_upsert);
  gwipc_surface_upsert s{};
  s.struct_size = sizeof(s);
  s.surface_id = (UINT64_C(1) << 32) | 1001U;
  s.x11_window_id = 1001;
  s.output_id = 1;
  s.logical_width = 320;
  s.logical_height = 200;
  s.visible = 1;
  s.transform = GWIPC_TRANSFORM_NORMAL;
  s.opacity = GWIPC_OPACITY_ONE;
  s.scale_numerator = s.scale_denominator = 1;
  s.color = srgb();
  s.presentation_flags = buffered ? 0 : GWIPC_SURFACE_PRESENTATION_METADATA_ONLY;
  s.fullscreen_eligible = GWIPC_TRI_STATE_FALSE;
  s.direct_scanout_eligible = GWIPC_TRI_STATE_UNKNOWN;
  contract(c, GWIPC_MESSAGE_SURFACE_UPSERT, GWIPC_FLAG_SNAPSHOT_ITEM, s,
           gwipc_contract_encode_surface_upsert);
  gwipc_surface_policy_upsert p{};
  p.struct_size = sizeof(p);
  p.surface_id = s.surface_id;
  p.x11_window_id = 1001;
  p.workspace_id = 1;
  p.window_type = GWIPC_POLICY_WINDOW_NORMAL;
  p.applied_state = GWIPC_POLICY_APPLIED_NORMAL;
  p.focused = p.managed = p.decoration_eligible = 1;
  p.fullscreen_eligible = GWIPC_TRI_STATE_FALSE;
  p.direct_scanout_eligible = GWIPC_TRI_STATE_UNKNOWN;
  contract(c, GWIPC_MESSAGE_SURFACE_POLICY_UPSERT, GWIPC_FLAG_SNAPSHOT_ITEM, p,
           gwipc_contract_encode_surface_policy_upsert);
  if (buffered) {
    gwipc_buffer_attach a{};
    a.struct_size = sizeof(a);
    a.buffer_id = 11;
    a.surface_id = s.surface_id;
    a.width = s.logical_width;
    a.height = s.logical_height;
    a.stride = s.logical_width * 4U;
    a.pixel_format = GWIPC_PIXEL_FORMAT_XRGB8888;
    a.alpha_semantics = GWIPC_ALPHA_OPAQUE;
    a.storage_size = static_cast<std::uint64_t>(a.stride) * a.height;
    a.color = srgb();
    a.synchronization = GWIPC_SYNCHRONIZATION_NONE;
    const int fd = ::memfd_create("gwcomp-m7-process",
                                  MFD_CLOEXEC | MFD_ALLOW_SEALING);
    require(fd >= 0 && ::ftruncate(fd, static_cast<off_t>(a.storage_size)) == 0,
            "create buffered scene memfd");
    const std::uint32_t green = UINT32_C(0x0000ff00);
    for (std::uint64_t offset = 0; offset < a.storage_size;
         offset += sizeof(green))
      require(::pwrite(fd, &green, sizeof(green), static_cast<off_t>(offset)) ==
                  sizeof(green),
              "populate buffered scene memfd");
    require(::fcntl(fd, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_GROW) == 0,
            "seal buffered scene memfd");
    contract_fd(c, GWIPC_MESSAGE_BUFFER_ATTACH, GWIPC_FLAG_SNAPSHOT_ITEM, a,
                gwipc_contract_encode_buffer_attach, fd);
  }
  gwipc_snapshot_end e{};
  e.struct_size = sizeof(e);
  e.snapshot_id = 1;
  e.generation = 1;
  e.actual_item_count = buffered ? 4 : 3;
  control(c, GWIPC_MESSAGE_SNAPSHOT_END, e, gwipc_control_encode_snapshot_end);
  gwipc_frame_commit f{};
  f.struct_size = sizeof(f);
  f.commit_id = 1;
  f.output_id = 1;
  f.producer_generation = 1;
  contract(c, GWIPC_MESSAGE_FRAME_COMMIT, GWIPC_FLAG_ACK_REQUIRED, f,
           gwipc_contract_encode_frame_commit);
}
void receive_ack(gwipc_connection *c) {
  for (int i = 0; i < 200; ++i) {
    require(pump(c), "drive acknowledgement");
    for (;;) {
      gwipc_message *raw = nullptr;
      if (gwipc_connection_receive(c, &raw) != GWIPC_STATUS_OK)
        break;
      Message m(raw);
      if (gwipc_message_type(m.get()) != GWIPC_MESSAGE_FRAME_ACKNOWLEDGED)
        continue;
      gwipc_decoded_contract *d = nullptr;
      require(gwipc_contract_decode_message(m.get(), &d) == GWIPC_STATUS_OK,
              "decode acknowledgement");
      std::unique_ptr<gwipc_decoded_contract, DD> x(d);
      auto *a = gwipc_decoded_frame_acknowledged(x.get());
      require(a && a->commit_id == 1 && a->result == GWIPC_FRAME_ACCEPTED,
              "metadata frame accepted");
      return;
    }
  }
  fail("acknowledgement timeout");
}
} // namespace
int main(int argc, char **argv) {
  require(argc == 2, "usage: test /path/to/gwcomp");
  char temp[] = "/tmp/gwcomp-metadata-process-XXXXXX";
  require(::mkdtemp(temp), "temporary directory");
  std::filesystem::path root = temp;
  for (const bool buffered : {false, true}) {
    const auto case_root = root / (buffered ? "m7" : "m6");
    std::filesystem::create_directories(case_root);
    auto socket = (case_root / "gwcomp.sock").string();
    auto dumps = (case_root / "dumps").string();
    auto manifest = (case_root / "scene/scenes.jsonl").string();
    pid_t child = ::fork();
    require(child >= 0, "fork gwcomp");
    if (child == 0) {
      ::execl(argv[1], argv[1], "--ipc-socket", socket.c_str(), "--dump-dir",
              dumps.c_str(), "--scene-manifest", manifest.c_str(),
              "--max-frames", "1", nullptr);
      _exit(127);
    }
    struct stat st{};
    for (int i = 0; i < 200 && ::lstat(socket.c_str(), &st) != 0; ++i)
      (void)::usleep(10000);
    require(S_ISSOCK(st.st_mode), "listener ready");
    auto c = connect_to(socket, buffered);
    send_scene(c.get(), buffered);
    receive_ack(c.get());
    c.reset();
    int status = 0;
    require(::waitpid(child, &status, 0) == child && WIFEXITED(status) &&
                WEXITSTATUS(status) == 0,
            "gwcomp exits cleanly");
    require(std::filesystem::is_regular_file(manifest),
            "ProtocolServer scene manifest exists");
    std::ifstream in(manifest);
    std::string json{std::istreambuf_iterator<char>(in), {}};
    require(std::count(json.begin(), json.end(), '\n') == 1 &&
                json.find("\"commit_id\":1") != std::string::npos &&
                json.find("\"generation\":1") != std::string::npos &&
                json.find("\"scene_hash\":\"") != std::string::npos &&
                json.find("\"surface_count\":1") != std::string::npos &&
                json.find("\"x11_window_id\":1001") != std::string::npos,
            "manifest records exactly one identified ProtocolServer scene");
    if (buffered) {
      require(std::filesystem::is_regular_file(
                  std::filesystem::path(dumps) / "frames.jsonl"),
              "buffered scene creates frame manifest");
      require(std::filesystem::is_regular_file(
                  std::filesystem::path(dumps) /
                  "frame-000001-output-0000000000000001.ppm"),
              "buffered scene creates PPM");
      require(json.find("\"metadata_only\":false") != std::string::npos,
              "buffered scene manifest records drawable surface metadata");
    } else {
      require(json.find("\"metadata_only\":true") != std::string::npos,
              "metadata scene manifest records metadata-only surface");
      require(!std::filesystem::exists(
                  std::filesystem::path(dumps) / "frames.jsonl"),
              "metadata scene creates no frame manifest");
      for (auto const &e :
           std::filesystem::recursive_directory_iterator(case_root))
        require(e.path().extension() != ".ppm",
                "metadata scene creates no PPM");
    }
  }
  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);
}
