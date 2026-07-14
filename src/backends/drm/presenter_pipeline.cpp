#include "backends/drm/presenter.hpp"

#include "backends/drm/connector_name.hpp"
#include "backends/drm/connector_selector.hpp"
#include "backends/drm/mode_selector.hpp"
#include "backends/drm/pipeline_selector.hpp"

#include <algorithm>
#include <array>

namespace glasswyrm::drm {
bool DrmPresenter::select_pipeline(std::string& error) {
  const auto& snapshot = device_.snapshot();
  const auto connector = select_connector(
      snapshot.connectors, snapshot.crtcs, config_.output.width,
      config_.output.height,
      config_.connector ? std::optional<std::string_view>(*config_.connector)
                        : std::nullopt);
  if (connector.status != ConnectorSelectionStatus::Success) {
    error = "DRM connector selection did not produce one eligible output";
    return false;
  }
  const auto& selected = snapshot.connectors[connector.connector_index];
  const auto mode = select_mode(selected.modes,
      {config_.output.width, config_.output.height,
       config_.output.refresh_millihz, config_.refresh_millihz});
  if (mode.status != ModeSelectionStatus::Success) {
    error = "DRM mode selection did not produce an exact output mode";
    return false;
  }
  const auto crtc = select_crtc(selected, snapshot.crtcs);
  if (crtc.status != CrtcSelectionStatus::Success) {
    error = "DRM CRTC selection failed";
    return false;
  }
  const auto plane =
      select_primary_plane(snapshot.crtcs[crtc.crtc_index], snapshot.planes);
  if (plane.status != PlaneSelectionStatus::Success &&
      config_.api == DrmPresentationApi::Atomic) {
    error = "DRM primary-plane selection failed";
    return false;
  }
  pipeline_ = {selected.id, snapshot.crtcs[crtc.crtc_index].id,
               plane.status == PlaneSelectionStatus::Success
                   ? snapshot.planes[plane.plane_index].id : 0};
  selected_mode_ = selected.modes[mode.mode_index];
  kms_mode_ = kms_mode_from_discovered(selected_mode_);
  return true;
}

bool DrmPresenter::configure_api(std::string& error) {
  if (config_.api != DrmPresentationApi::Legacy) {
    std::string atomic_error;
    if (try_atomic(atomic_error)) return true;
    if (config_.api == DrmPresentationApi::Atomic) {
      error = "forced atomic DRM initialization failed: " + atomic_error;
      return false;
    }
    fallback_reason_ = atomic_error.substr(0, kMaximumDiagnosticBytes);
  }
  mode_blob_.reset();
  return capture_legacy(error);
}

bool DrmPresenter::live_connector_ids(std::vector<std::uint32_t>& ids,
                                      std::string& error) {
  ids.clear();
  for (const auto& connector : device_.snapshot().connectors) {
    std::uint32_t routed{};
    if (!kms_.read_connector_crtc(device_.borrowed_kms_fd(), connector.id,
                                  routed, error)) return false;
    if (routed == pipeline_.crtc) ids.push_back(connector.id);
  }
  return true;
}

bool DrmPresenter::try_atomic(std::string& error) {
  const auto& snapshot = device_.snapshot();
  if (!snapshot.atomic || !snapshot.universal_planes ||
      pipeline_.primary_plane == 0) {
    error = "atomic and universal-plane capabilities are incomplete";
    return false;
  }
  if (std::ranges::any_of(snapshot.planes, [&](const Plane& plane) {
        return plane.current_crtc_id == pipeline_.crtc &&
               plane.type != PlaneType::Primary && plane.framebuffer_id != 0;
      })) {
    error = "active non-primary planes make atomic state capture incomplete";
    return false;
  }
  std::vector<std::uint32_t> connectors;
  if (!live_connector_ids(connectors, error) ||
      !capture_saved_state(kms_, device_.borrowed_kms_fd(), pipeline_,
                           connectors, true, saved_, error) ||
      !mode_blob_.create(kms_, device_.borrowed_kms_fd(), kms_mode_, error))
    return false;
  const auto request = atomic_initial_request(
      pipeline_, saved_.properties, mode_blob_.id(),
      buffers_.front().framebuffer_id(), config_.output.width,
      config_.output.height);
  if (!kms_.atomic_commit(device_.borrowed_kms_fd(), request,
                          AtomicTestOnly | AtomicAllowModeset, nullptr, error)) {
    mode_blob_.reset();
    return false;
  }
  selected_api_ = ReportApiPath::Atomic;
  return true;
}

bool DrmPresenter::capture_legacy(std::string& error) {
  std::vector<std::uint32_t> connectors;
  if (!live_connector_ids(connectors, error) ||
      !capture_saved_state(kms_, device_.borrowed_kms_fd(), pipeline_,
                           connectors, false, saved_, error)) return false;
  selected_api_ = ReportApiPath::Legacy;
  return true;
}

bool DrmPresenter::initialize_report(std::string& error) {
  if (!report_) return true;
  const auto& snapshot = device_.snapshot();
  const auto connector = std::ranges::find_if(snapshot.connectors,
      [&](const Connector& value) { return value.id == pipeline_.connector; });
  if (connector == snapshot.connectors.end()) {
    error = "selected DRM connector disappeared";
    return false;
  }
  const DiscoveryReport discovery{snapshot.canonical_path, snapshot.driver.name,
      snapshot.primary_node, snapshot.dumb_buffer, snapshot.atomic};
  SelectionReport selection;
  selection.connector_name = connector_name(connector->type, connector->type_id);
  selection.connector_id = pipeline_.connector;
  selection.crtc_id = pipeline_.crtc;
  selection.primary_plane_id = pipeline_.primary_plane;
  selection.mode_name = selected_mode_.name;
  selection.width = selected_mode_.width;
  selection.height = selected_mode_.height;
  selection.refresh_millihz = selected_mode_.refresh_millihz;
  selection.api = selected_api_;
  selection.framebuffer_format = "XRGB8888";
  selection.pitches = {buffers_.front().pitch(), buffers_.back().pitch()};
  selection.sizes = {buffers_.front().size(), buffers_.back().size()};
  selection.vt_path = config_.tty_path.empty() ? "external" : config_.tty_path;
  selection.vt_owned = device_.session() == DeviceSession::Standalone;
  const std::array<DrmReportRecord, 2> records{discovery, selection};
  StagedDrmReport staged;
  return report_->stage(records, staged, error) && report_->commit(staged, error);
}
}  // namespace glasswyrm::drm
