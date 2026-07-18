#include "gwcomp/renderer_runtime.hpp"
#include "gwcomp/runtime.hpp"
#include "gwcomp/runtime_reactor.hpp"
#include "gwcomp/signal_runtime.hpp"

#include "backends/output/presentation_backend.hpp"
#include "config.hpp"
#if GW_HAS_HEADLESS_BACKEND
#include "backends/headless/inventory.hpp"
#include "backends/headless/presenter.hpp"
#endif
#if GW_HAS_DRM_BACKEND
#include "gwcomp/drm_runtime.hpp"
#endif

#include <glasswyrm/ipc.h>

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>

namespace glasswyrm::compositor {
namespace {

constexpr std::uint64_t kM4Capabilities =
    GWIPC_CAP_FD_PASSING | GWIPC_CAP_SNAPSHOTS | GWIPC_CAP_OUTPUT_STATE |
    GWIPC_CAP_SURFACE_STATE | GWIPC_CAP_MEMFD_BUFFERS |
    GWIPC_CAP_DAMAGE_REGIONS | GWIPC_CAP_SDR_COLOR_METADATA |
    GWIPC_CAP_FRAME_ACKNOWLEDGEMENT;
constexpr std::uint64_t kCommonCapabilities =
    GWIPC_CAP_SNAPSHOTS | GWIPC_CAP_OUTPUT_STATE | GWIPC_CAP_SURFACE_STATE |
    GWIPC_CAP_SDR_COLOR_METADATA | GWIPC_CAP_FRAME_ACKNOWLEDGEMENT;
constexpr std::uint64_t kOfferedCapabilities =
    kM4Capabilities | GWIPC_CAP_WINDOW_LIFECYCLE | GWIPC_CAP_SESSION_STATE |
    GWIPC_CAP_CURSOR_SURFACE | GWIPC_CAP_CPU_BUFFER_SYNCHRONIZATION |
    GWIPC_CAP_OUTPUT_MANAGEMENT | GWIPC_CAP_SURFACE_OUTPUT_MEMBERSHIP |
    GWIPC_CAP_SCALE_METADATA;
struct ListenerDeleter {
  void operator()(gwipc_listener* value) const { gwipc_listener_destroy(value); }
};
bool prepare_dump_directory(const std::string& path, std::string& error) {
  std::error_code status_error;
  const auto status = std::filesystem::symlink_status(path, status_error);
  if (!status_error && std::filesystem::is_symlink(status)) {
    error = "dump path must not be a symbolic link";
    return false;
  }
  if (!status_error && std::filesystem::exists(status)) {
    if (!std::filesystem::is_directory(status)) {
      error = "dump path exists but is not a directory";
      return false;
    }
    return true;
  }
  status_error.clear();
  if (!std::filesystem::create_directories(path, status_error) && status_error) {
    error = status_error.message();
    return false;
  }
  const auto created = std::filesystem::symlink_status(path, status_error);
  if (status_error || !std::filesystem::is_directory(created) ||
      std::filesystem::is_symlink(created)) {
    error = "dump path did not resolve to a directory";
    return false;
  }
  return true;
}

}  // namespace

int run(const Options& options) {
  std::string directory_error;
  if (options.backend == Backend::Headless &&
      !prepare_dump_directory(options.dump_dir, directory_error)) {
    std::fprintf(stderr, "gwcomp: cannot prepare dump directory: %s\n",
                 directory_error.c_str());
    return 1;
  }
  if (options.mirror_dump_dir &&
      !prepare_dump_directory(*options.mirror_dump_dir, directory_error)) {
    std::fprintf(stderr, "gwcomp: cannot prepare mirror directory: %s\n",
                 directory_error.c_str());
    return 1;
  }

  std::string renderer_error;
  auto renderer = create_runtime_renderer(options, renderer_error);
  if (!renderer) {
    std::fprintf(stderr, "gwcomp: renderer initialization failed: %s\n",
                 renderer_error.c_str());
    return 1;
  }
  SignalRuntime signals;
  std::string signal_error;
  if (!signals.install(options.backend == Backend::Drm &&
                           !options.external_session,
                       signal_error)) {
    std::fprintf(stderr, "gwcomp: %s\n", signal_error.c_str());
    return 1;
  }

  std::optional<std::filesystem::path> manifest_path;
  if (options.scene_manifest) manifest_path = *options.scene_manifest;
#if GW_HAS_DRM_BACKEND
  DrmRuntimeResources drm_resources;
#endif
  output::OutputLayout output_layout;
  std::unique_ptr<glasswyrm::output::PresentationBackend> presenter;
  if (options.backend == Backend::Headless) {
#if GW_HAS_HEADLESS_BACKEND
    std::string inventory_error;
    auto inventory =
        headless::OutputInventory::build(options.headless_outputs,
                                         inventory_error);
    if (!inventory) {
      std::fprintf(stderr, "gwcomp: headless inventory failed: %s\n",
                   inventory_error.c_str());
      return 1;
    }
    output_layout = inventory->layout();
    presenter = std::make_unique<glasswyrm::headless::Presenter>(
        options.dump_dir);
#else
    std::fprintf(stderr,
                 "gwcomp: headless backend was not enabled at build time\n");
    return 1;
#endif
  } else {
#if GW_HAS_DRM_BACKEND
    std::string drm_error;
    drm::DrmOutputInventory inventory;
    if (!create_drm_presenter(options, drm_resources, inventory, presenter,
                              drm_error)) {
      std::fprintf(stderr, "gwcomp: DRM initialization failed: %s\n",
                   drm_error.c_str());
      return 1;
    }
    output_layout = std::move(inventory.layout);
#else
    std::fprintf(stderr,
                 "gwcomp: DRM backend was not enabled at build time\n");
    return 1;
#endif
  }

  gwipc_listener_options listener_options{};
  listener_options.struct_size = sizeof(listener_options);
  listener_options.path = options.ipc_socket.c_str();
  listener_options.local_role = GWIPC_ROLE_COMPOSITOR;
  listener_options.accepted_peer_roles =
      GWIPC_ROLE_BIT(GWIPC_ROLE_TEST_PRODUCER) |
      GWIPC_ROLE_BIT(GWIPC_ROLE_PROTOCOL_SERVER);
  listener_options.offered_capabilities = kOfferedCapabilities;
  listener_options.required_peer_capabilities = kCommonCapabilities;
  listener_options.maximum_payload = GWIPC_DEFAULT_MAXIMUM_PAYLOAD;
  listener_options.maximum_fd_count = GWIPC_DEFAULT_MAXIMUM_FDS;
  listener_options.maximum_queued_bytes = GWIPC_DEFAULT_MAXIMUM_QUEUED_BYTES;
  listener_options.maximum_queued_messages = GWIPC_DEFAULT_MAXIMUM_QUEUED_MESSAGES;
  listener_options.instance_label = "gwcomp-m4";
  gwipc_listener* raw_listener = nullptr;
  const auto status = gwipc_listener_create(&listener_options, &raw_listener);
  std::unique_ptr<gwipc_listener, ListenerDeleter> listener(raw_listener);
  if (status != GWIPC_STATUS_OK) {
    std::fprintf(stderr, "gwcomp: listener creation failed: %s errno=%d\n",
                 gwipc_status_string(status),
                 raw_listener ? gwipc_listener_system_errno(raw_listener) : 0);
    return 1;
  }
  std::fprintf(stderr, "gwcomp: listening socket=%s\n", options.ipc_socket.c_str());

  gw::compositor::Compositor compositor(std::move(presenter), manifest_path,
                                        {}, std::move(renderer));
  RuntimeReactor reactor(options, listener.get(), signals, compositor,
                         output_layout);
  int exit_status = reactor.run();

  std::string shutdown_error;
  if (!compositor.shutdown_presentation(shutdown_error)) {
    std::fprintf(stderr, "gwcomp: presentation restore failed: %s\n",
                 shutdown_error.c_str());
    exit_status = 1;
  }
  listener.reset();
  std::fprintf(stderr, "gwcomp: stopped\n");
  return exit_status;
}

}  // namespace glasswyrm::compositor
