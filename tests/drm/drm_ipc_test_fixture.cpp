#include "tests/drm/drm_ipc_test_fixture.hpp"

#include "tests/helpers/test_support.hpp"

#include <fcntl.h>
#include <poll.h>
#include <sys/mman.h>
#include <unistd.h>

#include <array>
#include <algorithm>
#include <csignal>
#include <fstream>
#include <iterator>

namespace gw::test::drm_ipc {
namespace {

constexpr std::uint64_t kCommonCapabilities =
    GWIPC_CAP_SNAPSHOTS | GWIPC_CAP_OUTPUT_STATE | GWIPC_CAP_SURFACE_STATE |
    GWIPC_CAP_SDR_COLOR_METADATA | GWIPC_CAP_FRAME_ACKNOWLEDGEMENT;
constexpr std::uint64_t kBufferedCapabilities =
    GWIPC_CAP_FD_PASSING | GWIPC_CAP_MEMFD_BUFFERS |
    GWIPC_CAP_DAMAGE_REGIONS;
constexpr std::uint64_t kServerCapabilities =
    kCommonCapabilities | kBufferedCapabilities | GWIPC_CAP_WINDOW_LIFECYCLE;

struct ContractPayloadDeleter {
  void operator()(gwipc_contract_payload* value) const {
    gwipc_contract_payload_destroy(value);
  }
};
struct ControlPayloadDeleter {
  void operator()(gwipc_control_payload* value) const {
    gwipc_control_payload_destroy(value);
  }
};
struct DecodedContractDeleter {
  void operator()(gwipc_decoded_contract* value) const {
    gwipc_decoded_contract_destroy(value);
  }
};

std::filesystem::path temporary_directory() {
  std::string pattern = "/tmp/glasswyrm-drm-ipc-XXXXXX";
  const auto path = ::mkdtemp(pattern.data());
  require(path != nullptr, "create DRM IPC integration directory");
  return path;
}

glasswyrm::drm::Mode mode() {
  glasswyrm::drm::Mode value{"2x2", 2, 2, 60'000, 25'000, true};
  value.hsync_start = 3;
  value.hsync_end = 4;
  value.htotal = 5;
  value.vsync_start = 3;
  value.vsync_end = 4;
  value.vtotal = 5;
  value.vrefresh_hz = 60;
  return value;
}

glasswyrm::drm::DeviceSnapshot snapshot() {
  using namespace glasswyrm::drm;
  DeviceSnapshot value;
  value.canonical_path = "/dev/dri/card0";
  value.device_major = 226;
  value.primary_node = value.dumb_buffer = value.universal_planes =
      value.atomic = true;
  value.driver.name = "virtio_gpu";
  value.crtcs.push_back({40, 0, {10}, 60, 0, 0, true, mode()});
  Connector connector;
  connector.id = 10;
  connector.type = static_cast<std::uint32_t>(ConnectorType::Virtual);
  connector.type_id = 1;
  connector.status = ConnectionStatus::Connected;
  connector.modes.push_back(mode());
  connector.possible_crtc_mask = 1;
  connector.current_crtc_id = 40;
  value.connectors.push_back(std::move(connector));
  value.planes.push_back({50, PlaneType::Primary, 1, {kFormatXrgb8888},
                          40, 60, 0, 0, 2, 2, 0, 0, 2U << 16U,
                          2U << 16U});
  return value;
}

std::vector<glasswyrm::drm::ObjectProperty> properties(
    const std::initializer_list<const char*> names, std::uint32_t first) {
  std::vector<glasswyrm::drm::ObjectProperty> value;
  for (const auto name : names) value.push_back({first++, name, 0, 64});
  return value;
}

void configure_kms(glasswyrm::drm::FakeKmsApi& api) {
  using namespace glasswyrm::drm;
  api.dumb_allocation = {7, 8, 16};
  api.connector_crtcs[10] = 40;
  KmsMode native{};
  native.hdisplay = native.vdisplay = 2;
  native.name = "2x2";
  api.crtcs[40] = {40, 60, 0, 0, true, native};
  api.planes[50] = {50, 60, 40, 0, 0, 2, 2, 0, 0, 2U << 16U,
                    2U << 16U};
  api.properties[{KmsObjectType::Connector, 10}] =
      properties({"CRTC_ID"}, 10);
  api.properties[{KmsObjectType::Crtc, 40}] =
      properties({"MODE_ID", "ACTIVE"}, 20);
  api.properties[{KmsObjectType::Plane, 50}] = properties(
      {"FB_ID", "CRTC_ID", "SRC_X", "SRC_Y", "SRC_W", "SRC_H",
       "CRTC_X", "CRTC_Y", "CRTC_W", "CRTC_H"},
      30);
}

gwipc_sdr_color_metadata srgb() {
  return {GWIPC_SDR_COLOR_SPACE_SRGB, GWIPC_TRANSFER_FUNCTION_SRGB,
          GWIPC_COLOR_PRIMARIES_SRGB, 0, 0, 0, 0};
}

template <class Value, class Encoder>
void enqueue_contract(gwipc_connection* connection, std::uint16_t type,
                      std::uint32_t flags, const Value& value, Encoder encoder,
                      const int* fds = nullptr, std::size_t fd_count = 0) {
  gwipc_contract_payload* raw = nullptr;
  require(encoder(&value, &raw) == GWIPC_STATUS_OK, "encode IPC contract");
  std::unique_ptr<gwipc_contract_payload, ContractPayloadDeleter> payload(raw);
  std::size_t size = 0;
  const auto* bytes = gwipc_contract_payload_data(payload.get(), &size);
  gwipc_outgoing_message message{};
  message.struct_size = sizeof(message);
  message.type = type;
  message.flags = flags;
  message.payload = bytes;
  message.payload_size = size;
  message.fds = fds;
  message.fd_count = fd_count;
  require(gwipc_connection_enqueue(connection, &message) == GWIPC_STATUS_OK,
          "enqueue IPC contract");
}

template <class Value, class Encoder>
void enqueue_control(gwipc_connection* connection, std::uint16_t type,
                     const Value& value, Encoder encoder) {
  gwipc_control_payload* raw = nullptr;
  require(encoder(&value, &raw) == GWIPC_STATUS_OK, "encode IPC control");
  std::unique_ptr<gwipc_control_payload, ControlPayloadDeleter> payload(raw);
  std::size_t size = 0;
  const auto* bytes = gwipc_control_payload_data(payload.get(), &size);
  gwipc_outgoing_message message{};
  message.struct_size = sizeof(message);
  message.type = type;
  message.payload = bytes;
  message.payload_size = size;
  require(gwipc_connection_enqueue(connection, &message) == GWIPC_STATUS_OK,
          "enqueue IPC control");
}

int buffer_fd(const std::uint32_t pixel) {
  constexpr std::size_t size = 4U * sizeof(std::uint32_t);
  const int fd = ::memfd_create("gwcomp-drm-ipc", MFD_CLOEXEC | MFD_ALLOW_SEALING);
  require(fd >= 0 && ::ftruncate(fd, size) == 0, "create producer memfd");
  const std::array pixels{pixel, pixel, pixel, pixel};
  require(::pwrite(fd, pixels.data(), size, 0) == static_cast<ssize_t>(size) &&
              ::fcntl(fd, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_GROW) == 0,
          "populate producer memfd");
  return fd;
}

}  // namespace

int FakeVirtualTerminal::open_terminal(std::string_view) {
  calls.push_back("open");
  return 9;
}
bool FakeVirtualTerminal::identify(
    int, glasswyrm::session::DeviceIdentity& value) {
  value = {4, 2, true};
  return true;
}
bool FakeVirtualTerminal::get_state(
    int, glasswyrm::session::VirtualTerminalState& value) {
  value.active = 1;
  return true;
}
bool FakeVirtualTerminal::get_mode(
    int, glasswyrm::session::VirtualTerminalMode& value) {
  value = {};
  return true;
}
bool FakeVirtualTerminal::get_kd_mode(int, int& value) {
  value = 0;
  return true;
}
bool FakeVirtualTerminal::activate(int, unsigned value) {
  calls.push_back("activate:" + std::to_string(value));
  return true;
}
bool FakeVirtualTerminal::wait_until_active(int, unsigned) { return true; }
bool FakeVirtualTerminal::set_process_mode(int, int, int) {
  calls.push_back("process");
  return true;
}
bool FakeVirtualTerminal::set_mode(
    int, const glasswyrm::session::VirtualTerminalMode&) {
  calls.push_back("restore-process");
  return true;
}
bool FakeVirtualTerminal::set_graphics_mode(int) {
  calls.push_back("graphics");
  return true;
}
bool FakeVirtualTerminal::set_kd_mode(int, int) {
  calls.push_back("restore-kd");
  return true;
}
bool FakeVirtualTerminal::acknowledge_release(int) {
  calls.push_back("release");
  return true;
}
bool FakeVirtualTerminal::acknowledge_acquire(int) {
  calls.push_back("acquire");
  return true;
}
void FakeVirtualTerminal::close_terminal(int) noexcept { calls.push_back("close"); }
std::string FakeVirtualTerminal::last_error() const { return "fake VT failure"; }

PresenterHarness::PresenterHarness(gw::compositor::PresentationTiming timing,
                                   const bool enable_scene_manifest)
    : root(temporary_directory()),
      drm({"/dev/dri/card0", glasswyrm::drm::DeviceOpenStatus::Success,
           snapshot(), {}}),
      report(root / "drm-report.jsonl"),
      mirror(root / "mirror") {
  using namespace glasswyrm::drm;
  configure_kms(kms);
  DeviceDiscovery discovery;
  auto device = Device::open(drm, "/dev/dri/card0", {}, discovery);
  require(device.has_value(), "open fake DRM integration device");
  auto backend =
      std::make_unique<DrmPresenter>(std::move(*device), kms, &report, &mirror);
  presenter = backend.get();
  DrmPresenterConfig config;
  config.output = {1, 2, 2, 60'000};
  config.connector = "Virtual-1";
  config.api = DrmPresentationApi::Atomic;
  config.tty_path = "/dev/tty2";
  config.vt_signals = {SIGUSR1, SIGUSR2};
  std::string error;
  require(presenter->initialize(config, &vt, error),
          "initialize fake DRM integration presenter");
  const auto scene_manifest = enable_scene_manifest
                                  ? std::optional<std::filesystem::path>(
                                        scene_manifest_path())
                                  : std::nullopt;
  compositor = std::make_unique<gw::compositor::Compositor>(
      std::move(backend), scene_manifest, std::move(timing));
}

PresenterHarness::~PresenterHarness() {
  compositor.reset();
  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);
}

std::filesystem::path PresenterHarness::mirror_frame(
    const std::uint64_t ordinal) const {
  char number[7]{};
  std::snprintf(number, sizeof(number), "%06llu",
                static_cast<unsigned long long>(ordinal));
  return root / "mirror" /
         (std::string("frame-") + number + "-output-0000000000000001.ppm");
}

std::string PresenterHarness::report_contents() const {
  std::ifstream input(report.path());
  return {std::istreambuf_iterator<char>(input), {}};
}

std::filesystem::path PresenterHarness::scene_manifest_path() const {
  return root / "scene.jsonl";
}

std::string PresenterHarness::scene_manifest_contents() const {
  std::ifstream input(scene_manifest_path());
  return {std::istreambuf_iterator<char>(input), {}};
}

bool PresenterHarness::shutdown(std::string& error) {
  return compositor->shutdown_presentation(error);
}

void IpcHarness::ListenerDeleter::operator()(gwipc_listener* value) const {
  gwipc_listener_destroy(value);
}
void IpcHarness::ConnectionDeleter::operator()(gwipc_connection* value) const {
  gwipc_connection_destroy(value);
}
void IpcHarness::MessageDeleter::operator()(gwipc_message* value) const {
  gwipc_message_destroy(value);
}

IpcHarness::IpcHarness(const std::filesystem::path& socket_path,
                       const ProducerKind kind)
    : socket_path_(socket_path), kind_(kind) {
  role_ = kind == ProducerKind::M4 ? GWIPC_ROLE_TEST_PRODUCER
                                   : GWIPC_ROLE_PROTOCOL_SERVER;
  profile_ = kind == ProducerKind::M4
                 ? gw::compositor::PeerProfile::M4TestProducer
                 : gw::compositor::PeerProfile::M7BufferedProtocolServer;
  const auto accepted = GWIPC_ROLE_BIT(GWIPC_ROLE_TEST_PRODUCER) |
                        GWIPC_ROLE_BIT(GWIPC_ROLE_PROTOCOL_SERVER);
  gwipc_listener_options listener_options{};
  listener_options.struct_size = sizeof(listener_options);
  listener_options.path = socket_path_.c_str();
  listener_options.local_role = GWIPC_ROLE_COMPOSITOR;
  listener_options.accepted_peer_roles = accepted;
  listener_options.offered_capabilities = kServerCapabilities;
  listener_options.required_peer_capabilities = kCommonCapabilities;
  gwipc_listener* raw_listener = nullptr;
  require(gwipc_listener_create(&listener_options, &raw_listener) ==
              GWIPC_STATUS_OK,
          "create integration compositor listener");
  listener_.reset(raw_listener);

  const auto client_capabilities =
      kCommonCapabilities | kBufferedCapabilities |
      (kind == ProducerKind::ProtocolServer ? GWIPC_CAP_WINDOW_LIFECYCLE : 0);
  gwipc_connection_options connection_options{};
  connection_options.struct_size = sizeof(connection_options);
  connection_options.path = socket_path_.c_str();
  connection_options.local_role = role_;
  connection_options.acceptable_server_roles =
      GWIPC_ROLE_BIT(GWIPC_ROLE_COMPOSITOR);
  connection_options.offered_capabilities = client_capabilities;
  connection_options.required_peer_capabilities = client_capabilities;
  gwipc_connection* raw_client = nullptr;
  const auto connect_status =
      gwipc_connection_connect(&connection_options, &raw_client);
  require(connect_status == GWIPC_STATUS_OK ||
              connect_status == GWIPC_STATUS_IN_PROGRESS,
          "connect integration producer");
  client_.reset(raw_client);

  for (int attempt = 0; attempt < 100 && !server_; ++attempt) {
    pollfd descriptor{gwipc_listener_fd(listener_.get()), POLLIN, 0};
    if (::poll(&descriptor, 1, 20) > 0) {
      gwipc_connection* raw_server = nullptr;
      if (gwipc_listener_accept(listener_.get(), &raw_server) == GWIPC_STATUS_OK)
        server_.reset(raw_server);
    }
  }
  require(server_ != nullptr, "accept integration producer");
  for (int attempt = 0; attempt < 100 &&
       (gwipc_connection_get_state(client_.get()) !=
            GWIPC_CONNECTION_ESTABLISHED ||
        gwipc_connection_get_state(server_.get()) !=
            GWIPC_CONNECTION_ESTABLISHED);
       ++attempt)
    pump_transport(1);
  require(gwipc_connection_get_state(client_.get()) ==
                  GWIPC_CONNECTION_ESTABLISHED &&
              gwipc_connection_get_state(server_.get()) ==
                  GWIPC_CONNECTION_ESTABLISHED,
          "complete integration producer handshake");
}

IpcHarness::~IpcHarness() = default;

void IpcHarness::pump_transport(const int rounds) {
  for (int round = 0; round < rounds; ++round) {
    std::array<pollfd, 2> descriptors{{
        {client_ ? gwipc_connection_fd(client_.get()) : -1,
         static_cast<short>(
             client_ ? gwipc_connection_wanted_poll_events(client_.get()) : 0),
         0},
        {server_ ? gwipc_connection_fd(server_.get()) : -1,
         static_cast<short>(
             server_ ? gwipc_connection_wanted_poll_events(server_.get()) : 0),
         0}}};
    const int ready = ::poll(descriptors.data(), descriptors.size(), 20);
    require(ready >= 0, "poll integration producer transport");
    if (client_ && descriptors[0].revents != 0)
      (void)gwipc_connection_process_poll_events(client_.get(),
                                                 descriptors[0].revents);
    if (server_ && descriptors[1].revents != 0)
      (void)gwipc_connection_process_poll_events(server_.get(),
                                                 descriptors[1].revents);
  }
}

void IpcHarness::enqueue_snapshot_begin(const std::uint64_t item_count) {
  gwipc_snapshot_begin value{};
  value.struct_size = sizeof(value);
  value.snapshot_id = 1;
  value.domain = GWIPC_SNAPSHOT_COMPLETE_SESSION;
  value.generation = 1;
  value.expected_item_count = item_count;
  enqueue_control(client_.get(), GWIPC_MESSAGE_SNAPSHOT_BEGIN, value,
                  gwipc_control_encode_snapshot_begin);
}

void IpcHarness::enqueue_snapshot_end(const std::uint64_t item_count) {
  gwipc_snapshot_end value{};
  value.struct_size = sizeof(value);
  value.snapshot_id = 1;
  value.generation = 1;
  value.actual_item_count = item_count;
  enqueue_control(client_.get(), GWIPC_MESSAGE_SNAPSHOT_END, value,
                  gwipc_control_encode_snapshot_end);
}

void IpcHarness::enqueue_output(const std::uint32_t flags) {
  gwipc_output_upsert value{};
  value.struct_size = sizeof(value);
  value.output_id = 1;
  value.enabled = 1;
  value.logical_width = value.physical_pixel_width = 2;
  value.logical_height = value.physical_pixel_height = 2;
  value.refresh_millihertz = 60'000;
  value.scale_numerator = value.scale_denominator = 1;
  value.transform = GWIPC_TRANSFORM_NORMAL;
  value.color = srgb();
  enqueue_contract(client_.get(), GWIPC_MESSAGE_OUTPUT_UPSERT, flags, value,
                   gwipc_contract_encode_output_upsert);
}

void IpcHarness::enqueue_surface(const std::uint32_t flags) {
  gwipc_surface_upsert value{};
  value.struct_size = sizeof(value);
  value.surface_id = 1;
  value.x11_window_id = 1001;
  value.output_id = 1;
  value.logical_width = value.logical_height = 2;
  value.visible = 1;
  value.transform = GWIPC_TRANSFORM_NORMAL;
  value.opacity = GWIPC_OPACITY_ONE;
  value.scale_numerator = value.scale_denominator = 1;
  value.color = srgb();
  value.fullscreen_eligible = GWIPC_TRI_STATE_FALSE;
  value.direct_scanout_eligible = GWIPC_TRI_STATE_UNKNOWN;
  enqueue_contract(client_.get(), GWIPC_MESSAGE_SURFACE_UPSERT, flags, value,
                   gwipc_contract_encode_surface_upsert);
}

void IpcHarness::enqueue_policy(const std::uint32_t flags) {
  gwipc_surface_policy_upsert value{};
  value.struct_size = sizeof(value);
  value.surface_id = 1;
  value.x11_window_id = 1001;
  value.workspace_id = 1;
  value.window_type = GWIPC_POLICY_WINDOW_NORMAL;
  value.applied_state = GWIPC_POLICY_APPLIED_NORMAL;
  value.focused = value.managed = value.decoration_eligible = 1;
  value.fullscreen_eligible = GWIPC_TRI_STATE_FALSE;
  value.direct_scanout_eligible = GWIPC_TRI_STATE_UNKNOWN;
  enqueue_contract(client_.get(), GWIPC_MESSAGE_SURFACE_POLICY_UPSERT, flags,
                   value, gwipc_contract_encode_surface_policy_upsert);
}

void IpcHarness::enqueue_buffer(const std::uint64_t buffer_id,
                                const std::uint32_t pixel,
                                const std::uint32_t flags) {
  gwipc_buffer_attach value{};
  value.struct_size = sizeof(value);
  value.buffer_id = buffer_id;
  value.surface_id = 1;
  value.width = value.height = 2;
  value.stride = 8;
  value.storage_size = 16;
  value.pixel_format = GWIPC_PIXEL_FORMAT_XRGB8888;
  value.alpha_semantics = GWIPC_ALPHA_OPAQUE;
  value.color = srgb();
  const int fd = buffer_fd(pixel);
  enqueue_contract(client_.get(), GWIPC_MESSAGE_BUFFER_ATTACH, flags, value,
                   gwipc_contract_encode_buffer_attach, &fd, 1);
  (void)::close(fd);
}

void IpcHarness::enqueue_commit(const std::uint64_t commit_id) {
  gwipc_frame_commit value{};
  value.struct_size = sizeof(value);
  value.commit_id = commit_id;
  value.output_id = 1;
  value.producer_generation = commit_id;
  enqueue_contract(client_.get(), GWIPC_MESSAGE_FRAME_COMMIT,
                   GWIPC_FLAG_ACK_REQUIRED, value,
                   gwipc_contract_encode_frame_commit);
}

void IpcHarness::send_snapshot(const std::uint64_t buffer_id,
                               const std::uint32_t pixel,
                               const std::uint64_t commit_id) {
  const std::uint64_t items = kind_ == ProducerKind::M4 ? 3 : 4;
  enqueue_snapshot_begin(items);
  enqueue_output(GWIPC_FLAG_SNAPSHOT_ITEM);
  enqueue_surface(GWIPC_FLAG_SNAPSHOT_ITEM);
  if (kind_ == ProducerKind::ProtocolServer)
    enqueue_policy(GWIPC_FLAG_SNAPSHOT_ITEM);
  enqueue_buffer(buffer_id, pixel, GWIPC_FLAG_SNAPSHOT_ITEM);
  enqueue_snapshot_end(items);
  enqueue_commit(commit_id);
}

void IpcHarness::send_replacement(const std::uint64_t buffer_id,
                                  const std::uint32_t pixel,
                                  const std::uint64_t commit_id) {
  enqueue_buffer(buffer_id, pixel, 0);
  enqueue_commit(commit_id);
}

void IpcHarness::send_damage_commit(const std::uint64_t commit_id) {
  const gwipc_damage_rectangle rectangle{0, 0, 1, 1};
  gwipc_surface_damage damage{};
  damage.struct_size = sizeof(damage);
  damage.surface_id = 1;
  damage.rectangles = &rectangle;
  damage.rectangle_count = 1;
  enqueue_contract(client_.get(), GWIPC_MESSAGE_SURFACE_DAMAGE, 0, damage,
                   gwipc_contract_encode_surface_damage);
  enqueue_commit(commit_id);
}

IpcHarness::Message IpcHarness::next_server_message() {
  for (int attempt = 0; attempt < 100; ++attempt) {
    pump_transport(1);
    gwipc_message* raw = nullptr;
    if (gwipc_connection_receive(server_.get(), &raw) == GWIPC_STATUS_OK)
      return Message(raw);
  }
  require(false, "receive integration producer message");
  return {};
}

glasswyrm::compositor::ContractDispatchResult IpcHarness::dispatch_until_frame(
    gw::compositor::Compositor& compositor) {
  compositor.set_peer_profile(profile_);
  for (int message_count = 0; message_count < 16; ++message_count) {
    auto message = next_server_message();
    const auto type = gwipc_message_type(message.get());
    const auto result = glasswyrm::compositor::dispatch_contract_message(
        server_.get(), message.get(), role_, profile_, std::nullopt, compositor);
    if (type == GWIPC_MESSAGE_FRAME_COMMIT) return result;
  }
  require(false, "dispatch producer frame within bounded message count");
  return {};
}

std::vector<ClientEvent> IpcHarness::drain_client() {
  pump_transport(8);
  std::vector<ClientEvent> events;
  if (!client_) return events;
  for (;;) {
    gwipc_message* raw = nullptr;
    if (gwipc_connection_receive(client_.get(), &raw) != GWIPC_STATUS_OK) break;
    Message message(raw);
    ClientEvent observed;
    observed.type = gwipc_message_type(message.get());
    gwipc_decoded_contract* decoded_raw = nullptr;
    require(gwipc_contract_decode_message(message.get(), &decoded_raw) ==
                GWIPC_STATUS_OK,
            "decode compositor response");
    std::unique_ptr<gwipc_decoded_contract, DecodedContractDeleter> decoded(
        decoded_raw);
    if (observed.type == GWIPC_MESSAGE_FRAME_ACKNOWLEDGED) {
      const auto* value = gwipc_decoded_frame_acknowledged(decoded.get());
      require(value != nullptr, "read frame acknowledgement");
      observed.commit_id = value->commit_id;
      observed.frame_result = value->result;
    } else if (observed.type == GWIPC_MESSAGE_BUFFER_RELEASE) {
      const auto* value = gwipc_decoded_buffer_release(decoded.get());
      require(value != nullptr, "read buffer release");
      observed.buffer_id = value->buffer_id;
      observed.release_reason = value->reason;
    }
    events.push_back(observed);
  }
  return events;
}

void IpcHarness::disconnect_client() {
  client_.reset();
  pump_transport(8);
}

bool IpcHarness::server_closed() {
  pump_transport(2);
  return gwipc_connection_get_state(server_.get()) == GWIPC_CONNECTION_CLOSED;
}

short ready_presentation_events(const gw::compositor::Compositor& compositor) {
  pollfd descriptor{compositor.presentation_poll_fd(), POLLIN, 0};
  require(::poll(&descriptor, 1, 100) == 1,
          "fake DRM completion FD becomes readable");
  return descriptor.revents;
}

bool has_ack(const std::vector<ClientEvent>& events,
             const std::uint64_t commit_id, const gwipc_frame_result result) {
  return std::any_of(events.begin(), events.end(), [&](const ClientEvent& event) {
    return event.type == GWIPC_MESSAGE_FRAME_ACKNOWLEDGED &&
           event.commit_id == commit_id && event.frame_result == result;
  });
}

bool has_release(const std::vector<ClientEvent>& events,
                 const std::uint64_t buffer_id) {
  return std::any_of(events.begin(), events.end(), [&](const ClientEvent& event) {
    return event.type == GWIPC_MESSAGE_BUFFER_RELEASE &&
           event.buffer_id == buffer_id &&
           event.release_reason == GWIPC_BUFFER_RELEASE_REPLACED;
  });
}

}  // namespace gw::test::drm_ipc
