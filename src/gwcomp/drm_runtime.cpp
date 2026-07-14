#include "gwcomp/drm_runtime.hpp"

#include "backends/drm/connector_name.hpp"
#include "backends/drm/connector_selector.hpp"
#include "backends/drm/device.hpp"
#include "backends/drm/drm_report.hpp"
#include "backends/drm/kms_api.hpp"
#include "backends/drm/mode_selector.hpp"
#include "backends/drm/pipeline_selector.hpp"
#include "backends/drm/presenter.hpp"
#include "backends/headless/frame_dump.hpp"
#include "backends/session/vt_api.hpp"

#include <algorithm>
#include <csignal>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <tuple>
#include <vector>

namespace glasswyrm::compositor {
namespace {

drm::DeviceOpenOptions device_options(const DrmApiMode policy) {
  const bool atomic = policy != DrmApiMode::Legacy;
  return {atomic, atomic};
}

std::vector<std::string> device_paths(const std::string& requested) {
  if (requested != "auto") return {requested};
  std::vector<std::string> paths;
  std::error_code error;
  for (std::filesystem::directory_iterator entries("/dev/dri", error), end;
       !error && entries != end; entries.increment(error)) {
    const auto name = entries->path().filename().string();
    if (!name.starts_with("card") || name.size() == 4 ||
        !std::all_of(name.begin() + 4, name.end(), [](const unsigned char c) {
          return c >= '0' && c <= '9';
        }))
      continue;
    paths.push_back(entries->path().string());
  }
  std::ranges::sort(paths);
  return paths;
}

bool connector_is_eligible(const drm::Connector& connector,
                           const std::span<const drm::Crtc> crtcs) {
  if (connector.status != drm::ConnectionStatus::Connected ||
      connector.modes.empty() || connector.non_desktop ||
      connector.type ==
          static_cast<std::uint32_t>(drm::ConnectorType::Writeback))
    return false;
  const auto crtc = drm::select_crtc(connector, crtcs);
  return crtc.status == drm::CrtcSelectionStatus::Success;
}

std::optional<std::size_t> select_unconstrained_connector(
    const drm::DeviceSnapshot& snapshot,
    const std::optional<std::string>& requested) {
  std::optional<std::size_t> result;
  for (std::size_t index = 0; index < snapshot.connectors.size(); ++index) {
    const auto& connector = snapshot.connectors[index];
    if (requested && drm::connector_name(connector.type, connector.type_id) !=
                         *requested)
      continue;
    if (!connector_is_eligible(connector, snapshot.crtcs)) continue;
    if (result) return std::nullopt;
    result = index;
  }
  return result;
}

std::optional<drm::Mode> select_default_mode(const drm::Connector& connector) {
  if (connector.modes.empty()) return std::nullopt;
  return *std::ranges::min_element(
      connector.modes, [](const drm::Mode& left, const drm::Mode& right) {
        const auto left_key = std::tuple{
            !left.preferred,
            std::uint64_t{std::numeric_limits<std::uint64_t>::max()} -
                std::uint64_t{left.width} * left.height,
            std::numeric_limits<std::uint32_t>::max() -
                left.refresh_millihz,
            left.name, left.clock_khz};
        const auto right_key = std::tuple{
            !right.preferred,
            std::uint64_t{std::numeric_limits<std::uint64_t>::max()} -
                std::uint64_t{right.width} * right.height,
            std::numeric_limits<std::uint32_t>::max() -
                right.refresh_millihz,
            right.name, right.clock_khz};
        return left_key < right_key;
      });
}

std::optional<drm::Mode> selected_mode(const drm::DeviceSnapshot& snapshot,
                                       const Options& options) {
  if (!options.mode) {
    const auto connector =
        select_unconstrained_connector(snapshot, options.connector);
    return connector ? select_default_mode(snapshot.connectors[*connector])
                     : std::nullopt;
  }
  const auto connector = drm::select_connector(
      snapshot.connectors, snapshot.crtcs, options.mode->width,
      options.mode->height,
      options.connector
          ? std::optional<std::string_view>(*options.connector)
          : std::nullopt);
  if (connector.status != drm::ConnectorSelectionStatus::Success)
    return std::nullopt;
  const auto& candidate = snapshot.connectors[connector.connector_index];
  const auto mode = drm::select_mode(
      candidate.modes,
      {options.mode->width, options.mode->height, 0,
       options.mode->refresh_millihz});
  return mode.status == drm::ModeSelectionStatus::Success
             ? std::optional<drm::Mode>(candidate.modes[mode.mode_index])
             : std::nullopt;
}

bool device_qualifies(const drm::DeviceSnapshot& snapshot,
                      const Options& options) {
  return selected_mode(snapshot, options).has_value();
}

drm::DrmPresentationApi presentation_api(const DrmApiMode policy) {
  switch (policy) {
    case DrmApiMode::Auto: return drm::DrmPresentationApi::Auto;
    case DrmApiMode::Atomic: return drm::DrmPresentationApi::Atomic;
    case DrmApiMode::Legacy: return drm::DrmPresentationApi::Legacy;
  }
  return drm::DrmPresentationApi::Auto;
}

}  // namespace

DrmRuntimeResources::DrmRuntimeResources() = default;
DrmRuntimeResources::~DrmRuntimeResources() = default;

bool create_drm_presenter(
    const Options& options, DrmRuntimeResources& resources,
    std::unique_ptr<output::PresentationBackend>& presenter,
    std::string& error) {
  error.clear();
  resources.drm_api = drm::make_real_drm_api();
  resources.kms_api = drm::make_real_kms_api();
  drm::DeviceDiscovery discovery;
  std::optional<drm::Device> device;
  const auto open_options = device_options(options.drm_api);
  if (options.drm_fd) {
    device = drm::Device::adopt(*resources.drm_api, *options.drm_fd,
                                open_options, discovery);
  } else {
    const auto candidates = device_paths(*options.drm_device);
    if (candidates.empty()) {
      error = "no DRM primary-node candidates were found";
      return false;
    }
    std::vector<std::string> eligible;
    for (const auto& path : candidates) {
      auto candidate = drm::Device::open(*resources.drm_api, path,
                                         open_options, discovery);
      if (candidate && device_qualifies(candidate->snapshot(), options))
        eligible.push_back(path);
    }
    if (eligible.size() != 1) {
      error = eligible.empty()
                  ? "no DRM device has one eligible connector and mode"
                  : "automatic DRM device selection is ambiguous";
      return false;
    }
    device = drm::Device::open(*resources.drm_api, eligible.front(),
                               open_options, discovery);
  }
  if (!device) {
    error = discovery.error.empty() ? "DRM device initialization failed"
                                    : discovery.error;
    return false;
  }
  const auto mode = selected_mode(device->snapshot(), options);
  if (!mode) {
    error = "DRM connector or mode selection is ambiguous or unsupported";
    return false;
  }
  if (options.drm_report)
    resources.report = std::make_unique<drm::DrmReport>(*options.drm_report);
  if (options.mirror_dump_dir)
    resources.mirror =
        std::make_unique<headless::FrameDumper>(*options.mirror_dump_dir);
  if (!options.external_session)
    resources.vt_api = std::make_unique<session::LinuxVirtualTerminalApi>();

  auto backend = std::make_unique<drm::DrmPresenter>(
      std::move(*device), *resources.kms_api, resources.report.get(),
      resources.mirror.get());
  drm::DrmPresenterConfig config;
  config.output = {0, mode->width, mode->height, mode->refresh_millihz};
  config.connector = options.connector;
  config.refresh_millihz =
      options.mode ? options.mode->refresh_millihz : std::nullopt;
  config.api = presentation_api(options.drm_api);
  config.tty_path = options.tty.value_or("");
  config.vt_signals = {SIGUSR1, SIGUSR2};
  if (!backend->initialize(config, resources.vt_api.get(), error)) return false;
  if (!backend->fallback_reason().empty())
    std::fprintf(stderr, "gwcomp: atomic DRM fallback: %s\n",
                 backend->fallback_reason().c_str());
  presenter = std::move(backend);
  return true;
}

}  // namespace glasswyrm::compositor
