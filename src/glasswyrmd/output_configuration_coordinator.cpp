#include "glasswyrmd/output_configuration_coordinator.hpp"

#include "output/model/drm_configuration.hpp"
#include "output/model/identity.hpp"

#include <algorithm>
#include <limits>
#include <string>
#include <tuple>
#include <utility>

namespace glasswyrm::server {
namespace {

using Result = gw::ipc::wire::OutputConfigurationResult;

Result validation_result(const output::LayoutValidationError error) noexcept {
  using Error = output::LayoutValidationError;
  switch (error) {
  case Error::InvalidScale:
    return Result::UnsupportedScale;
  case Error::UnsupportedTransform:
    return Result::UnsupportedTransform;
  case Error::InvalidMode:
  case Error::PhysicalLimitExceeded:
    return Result::UnsupportedMode;
  case Error::IncompleteInventory:
  case Error::InvalidIdentity:
    return Result::UnknownOutput;
  case Error::None:
  case Error::TooManyOutputs:
  case Error::NoEnabledOutput:
  case Error::InvalidPrimaryOutput:
  case Error::InvalidName:
  case Error::InvalidDescriptor:
  case Error::InvalidPosition:
  case Error::OverlappingOutputs:
  case Error::InvalidRootExtent:
  case Error::InvalidLogicalExtent:
  case Error::InvalidDisabledState:
  case Error::InvalidGeneration:
  case Error::InvalidOutputOrder:
    return Result::InvalidLayout;
  }
  return Result::InternalError;
}

std::string mode_name(const gw::ipc::wire::OutputUpsert &value) {
  return std::to_string(value.physical_pixel_width) + "x" +
         std::to_string(value.physical_pixel_height) + "@" +
         std::to_string(value.refresh_millihertz);
}

output::SdrMetadata color_from(
    const gw::ipc::wire::SdrColorMetadata &value) noexcept {
  return {static_cast<output::SdrColorSpace>(value.color_space),
          static_cast<output::SdrTransferFunction>(value.transfer_function),
          static_cast<output::SdrColorPrimaries>(value.primaries),
          value.luminance_available,
          value.minimum_luminance_millinit,
          value.maximum_luminance_millinit,
          value.max_frame_average_luminance_millinit};
}

void update_layout_metadata(output::OutputLayout &layout) {
  layout.root_logical_width = 0;
  layout.root_logical_height = 0;
  layout.enabled_output_count = 0;
  layout.output_order.clear();
  for (const auto &[id, state] : layout.states) {
    layout.output_order.push_back(id);
    if (!state.enabled)
      continue;
    ++layout.enabled_output_count;
    if (state.logical_x >= 0)
      layout.root_logical_width = std::max(
          layout.root_logical_width,
          static_cast<std::uint32_t>(state.logical_x) + state.logical_width);
    if (state.logical_y >= 0)
      layout.root_logical_height = std::max(
          layout.root_logical_height,
          static_cast<std::uint32_t>(state.logical_y) + state.logical_height);
  }
  std::ranges::sort(
      layout.output_order,
      [&layout](const output::OutputId left_id,
                const output::OutputId right_id) {
        const auto &left = layout.states.at(left_id);
        const auto &right = layout.states.at(right_id);
        if (left.enabled != right.enabled)
          return left.enabled > right.enabled;
        if (!left.enabled)
          return left_id < right_id;
        return std::tie(left.logical_y, left.logical_x, left_id) <
               std::tie(right.logical_y, right.logical_x, right_id);
      });
}

} // namespace

OutputConfigurationCoordinator::OutputConfigurationCoordinator(
    output::OutputLayout inventory,
    std::map<std::uint64_t, gwipc_vrr_policy_mode> vrr_policies)
    : inventory_(std::move(inventory)), committed_layout_(inventory_),
      committed_vrr_policies_(std::move(vrr_policies)) {
  valid_ = static_cast<bool>(output::validate_layout(inventory_));
  if (!committed_vrr_policies_.empty()) {
    if (committed_vrr_policies_.size() != inventory_.descriptors.size())
      valid_ = false;
    for (const auto& [id, mode] : committed_vrr_policies_)
      if (!inventory_.descriptors.contains(output::OutputId{id}) ||
          mode < GWIPC_VRR_POLICY_OFF ||
          mode > GWIPC_VRR_POLICY_ALWAYS_ELIGIBLE)
        valid_ = false;
  }
}

OutputConfigurationSnapshotStatus
OutputConfigurationCoordinator::begin_snapshot(
    const std::uint64_t snapshot_id,
    const std::uint32_t expected_item_count) {
  if (transaction_)
    return OutputConfigurationSnapshotStatus::Busy;
  if (stage_ != OutputConfigurationStage::Idle || snapshot_id == 0 ||
      expected_item_count == 0 ||
      expected_item_count > output::kMaximumOutputs * 2U)
    return OutputConfigurationSnapshotStatus::InvalidState;
  clear_staging();
  snapshot_id_ = snapshot_id;
  expected_item_count_ = expected_item_count;
  stage_ = OutputConfigurationStage::Collecting;
  return OutputConfigurationSnapshotStatus::Accepted;
}

OutputConfigurationSnapshotStatus
OutputConfigurationCoordinator::stage_vrr_policy(
    const gwipc_output_vrr_policy_upsert& value) {
  if (stage_ != OutputConfigurationStage::Collecting)
    return transaction_ ? OutputConfigurationSnapshotStatus::Busy
                        : OutputConfigurationSnapshotStatus::InvalidState;
  if (!vrr_profile() || value.struct_size < sizeof(value) ||
      !inventory_.descriptors.contains(output::OutputId{value.output_id}) ||
      value.mode < GWIPC_VRR_POLICY_OFF ||
      value.mode > GWIPC_VRR_POLICY_ALWAYS_ELIGIBLE || value.flags != 0 ||
      !staged_vrr_policies_.emplace(value.output_id, value.mode).second) {
    snapshot_failure_ = OutputConfigurationSnapshotStatus::InvalidRecord;
    staged_result_ = Result::UnsupportedVrr;
    return snapshot_failure_;
  }
  if (item_count_ == std::numeric_limits<std::uint32_t>::max()) {
    snapshot_failure_ = OutputConfigurationSnapshotStatus::CountMismatch;
    staged_result_ = Result::InvalidLayout;
    return snapshot_failure_;
  }
  ++item_count_;
  return OutputConfigurationSnapshotStatus::Accepted;
}

OutputConfigurationSnapshotStatus OutputConfigurationCoordinator::stage_output(
    const gw::ipc::wire::OutputUpsert &value) {
  if (stage_ != OutputConfigurationStage::Collecting)
    return transaction_ ? OutputConfigurationSnapshotStatus::Busy
                        : OutputConfigurationSnapshotStatus::InvalidState;
  if (item_count_ == std::numeric_limits<std::uint32_t>::max()) {
    snapshot_failure_ = OutputConfigurationSnapshotStatus::CountMismatch;
    staged_result_ = Result::InvalidLayout;
    return snapshot_failure_;
  }
  ++item_count_;
  const output::OutputId id{value.output_id};
  if (!id || !inventory_.descriptors.contains(id)) {
    if (snapshot_failure_ == OutputConfigurationSnapshotStatus::Accepted) {
      snapshot_failure_ = OutputConfigurationSnapshotStatus::InvalidRecord;
      staged_result_ = Result::UnknownOutput;
    }
    return snapshot_failure_;
  }
  if (!staged_outputs_.emplace(id, value).second) {
    if (snapshot_failure_ == OutputConfigurationSnapshotStatus::Accepted) {
      snapshot_failure_ = OutputConfigurationSnapshotStatus::InvalidRecord;
      staged_result_ = Result::InvalidLayout;
    }
    return snapshot_failure_;
  }
  return OutputConfigurationSnapshotStatus::Accepted;
}

OutputConfigurationSnapshotStatus OutputConfigurationCoordinator::end_snapshot(
    const std::uint64_t snapshot_id,
    const std::uint32_t actual_item_count) {
  if (stage_ != OutputConfigurationStage::Collecting ||
      snapshot_id != snapshot_id_)
    return OutputConfigurationSnapshotStatus::InvalidState;
  if (actual_item_count != item_count_ ||
      item_count_ != expected_item_count_ ||
      staged_outputs_.size() != inventory_.descriptors.size() ||
      (vrr_profile() &&
       staged_vrr_policies_.size() != inventory_.descriptors.size())) {
    snapshot_failure_ = OutputConfigurationSnapshotStatus::CountMismatch;
    if (staged_result_ != Result::UnknownOutput)
      staged_result_ = Result::InvalidLayout;
  }
  stage_ = OutputConfigurationStage::SnapshotReady;
  return snapshot_failure_;
}

void OutputConfigurationCoordinator::abort_snapshot() noexcept {
  if (stage_ == OutputConfigurationStage::Collecting ||
      stage_ == OutputConfigurationStage::SnapshotReady)
    clear_staging();
}

std::optional<gw::ipc::wire::OutputConfigurationAcknowledged>
OutputConfigurationCoordinator::submit(
    const gw::ipc::wire::OutputConfigurationCommit &request) {
  if (transaction_)
    return acknowledgement(request.configuration_id, Result::Busy);
  if (!valid_)
    return acknowledgement(request.configuration_id, Result::InternalError);
  if (request.configuration_id == 0 || request.flags != 0)
    return acknowledgement(request.configuration_id, Result::InvalidLayout);
  if (request.base_generation != committed_layout_.generation) {
    const auto result =
        acknowledgement(request.configuration_id, Result::StaleGeneration);
    clear_staging();
    return result;
  }
  if (stage_ != OutputConfigurationStage::SnapshotReady ||
      request.configuration_id != snapshot_id_) {
    const auto result = acknowledgement(
        request.configuration_id,
        staged_result_ == Result::UnknownOutput ? Result::UnknownOutput
                                                : Result::InvalidLayout);
    clear_staging();
    return result;
  }
  if (snapshot_failure_ != OutputConfigurationSnapshotStatus::Accepted) {
    const auto result = acknowledgement(request.configuration_id,
                                        staged_result_);
    clear_staging();
    return result;
  }

  output::OutputLayout candidate;
  const auto result = build_candidate(request, candidate);
  if (result != Result::Accepted) {
    const auto rejected = acknowledgement(request.configuration_id, result);
    clear_staging();
    return rejected;
  }
  transaction_ = OutputConfigurationTransaction{
      request.configuration_id, committed_layout_, std::move(candidate),
      committed_vrr_policies_, staged_vrr_policies_};
  clear_staging();
  stage_ = OutputConfigurationStage::PolicyPending;
  return std::nullopt;
}

bool OutputConfigurationCoordinator::accept_policy() noexcept {
  if (stage_ != OutputConfigurationStage::PolicyPending || !transaction_)
    return false;
  stage_ = OutputConfigurationStage::CompositorPending;
  return true;
}

std::optional<gw::ipc::wire::OutputConfigurationAcknowledged>
OutputConfigurationCoordinator::reject_policy(const Result rejection) noexcept {
  if (stage_ != OutputConfigurationStage::PolicyPending || !transaction_ ||
      (rejection != Result::PolicyRejected &&
       rejection != Result::VrrPolicyRejected))
    return std::nullopt;
  const auto result = acknowledgement(transaction_->configuration_id,
                                      rejection);
  clear_transaction();
  return result;
}

bool OutputConfigurationCoordinator::accept_compositor() noexcept {
  if (!can_accept_compositor())
    return false;
  stage_ = OutputConfigurationStage::CommitReady;
  return true;
}

bool OutputConfigurationCoordinator::begin_rollback(
    const Result rejection) noexcept {
  if (stage_ != OutputConfigurationStage::CompositorPending || !transaction_ ||
      (rejection != Result::CompositorRejected &&
       rejection != Result::PresenterRejected &&
       rejection != Result::VrrPresenterRejected))
    return false;
  rollback_result_ = rejection;
  stage_ = OutputConfigurationStage::RollbackPending;
  return true;
}

std::optional<gw::ipc::wire::OutputConfigurationAcknowledged>
OutputConfigurationCoordinator::finish_rollback(const bool succeeded) noexcept {
  if (stage_ != OutputConfigurationStage::RollbackPending || !transaction_)
    return std::nullopt;
  const auto result = acknowledgement(
      transaction_->configuration_id,
      succeeded ? rollback_result_ : Result::InternalError);
  clear_transaction();
  return result;
}

std::optional<gw::ipc::wire::OutputConfigurationAcknowledged>
OutputConfigurationCoordinator::commit() noexcept {
  if (stage_ != OutputConfigurationStage::CommitReady || !transaction_)
    return std::nullopt;
  committed_layout_ = std::move(transaction_->proposed_layout);
  committed_vrr_policies_ = std::move(transaction_->proposed_vrr_policies);
  const auto request_id = transaction_->configuration_id;
  clear_transaction();
  return acknowledgement(request_id, Result::Accepted);
}

std::optional<gw::ipc::wire::OutputConfigurationAcknowledged>
OutputConfigurationCoordinator::fail_internal() noexcept {
  if (!transaction_)
    return std::nullopt;
  const auto result = acknowledgement(transaction_->configuration_id,
                                      Result::InternalError);
  clear_transaction();
  return result;
}

OutputConfigurationCoordinator::Acknowledged
OutputConfigurationCoordinator::acknowledgement(
    const std::uint64_t request_id, const Result result) const noexcept {
  return {request_id,
          committed_layout_.generation,
          result,
          0,
          committed_layout_.primary_output_id.value,
          committed_layout_.root_logical_width,
          committed_layout_.root_logical_height,
          static_cast<std::uint32_t>(committed_layout_.enabled_output_count)};
}

OutputConfigurationCoordinator::Result
OutputConfigurationCoordinator::build_candidate(
    const gw::ipc::wire::OutputConfigurationCommit &request,
    output::OutputLayout &candidate) const {
  if (committed_layout_.generation == std::numeric_limits<std::uint64_t>::max())
    return Result::InternalError;
  candidate = inventory_;
  candidate.generation = committed_layout_.generation + 1;
  candidate.primary_output_id = output::OutputId{request.primary_output_id};
  if (!candidate.descriptors.contains(candidate.primary_output_id))
    return Result::UnknownOutput;
  for (auto &[id, descriptor] : candidate.descriptors) {
    const auto staged = staged_outputs_.find(id);
    if (staged == staged_outputs_.end())
      return Result::InvalidLayout;
    const auto &value = staged->second;
    const auto &old = committed_layout_.states.at(id);
    auto &state = candidate.states.at(id);
    state = {};
    state.output_id = id;
    state.enabled = value.enabled;
    state.logical_x = value.logical_x;
    state.logical_y = value.logical_y;
    state.logical_width = value.logical_width;
    state.logical_height = value.logical_height;
    state.physical_width = value.physical_pixel_width;
    state.physical_height = value.physical_pixel_height;
    state.refresh_millihertz = value.refresh_millihertz;
    state.scale = {value.scale_numerator, value.scale_denominator};
    state.transform = static_cast<output::OutputTransform>(value.transform);
    state.color = color_from(value.color);
    state.primary = id == candidate.primary_output_id;
    state.generation = candidate.generation;
    if (!value.enabled)
      continue;
    if (!descriptor.scale_configurable && state.scale != old.scale)
      return Result::UnsupportedScale;
    if (!descriptor.transform_configurable &&
        state.transform != old.transform)
      return Result::UnsupportedTransform;
    const bool mode_changed =
        state.physical_width != old.physical_width ||
        state.physical_height != old.physical_height ||
        state.refresh_millihertz != old.refresh_millihertz;
    if (!descriptor.mode_configurable && mode_changed)
      return Result::UnsupportedMode;

    auto selected = std::ranges::find_if(
        descriptor.modes, [&state](const output::OutputMode &mode) {
          return mode.physical_width == state.physical_width &&
                 mode.physical_height == state.physical_height &&
                 mode.refresh_millihertz == state.refresh_millihertz;
        });
    for (auto &mode : descriptor.modes)
      mode.current = false;
    if (selected != descriptor.modes.end()) {
      selected->current = true;
      state.mode_id = selected->id;
    } else if (descriptor.kind == output::OutputKind::Headless &&
               descriptor.arbitrary_headless_mode &&
               descriptor.mode_configurable) {
      const auto name = mode_name(value);
      const auto id_value = output::derive_output_mode_id(
          id, state.physical_width, state.physical_height,
          state.refresh_millihertz, 0, name);
      if (!id_value)
        return Result::UnsupportedMode;
      state.mode_id = *id_value;
      descriptor.modes.push_back({*id_value,
                                  id,
                                  state.physical_width,
                                  state.physical_height,
                                  state.refresh_millihertz,
                                  0,
                                  name,
                                  false,
                                  true});
    } else {
      return Result::UnsupportedMode;
    }
  }
  update_layout_metadata(candidate);
  if (output::is_single_fixed_drm_output_profile(committed_layout_)) {
    const auto drm_validation =
        output::validate_single_fixed_drm_configuration(committed_layout_,
                                                        candidate);
    if (drm_validation == output::DrmConfigurationError::PhysicalModeChanged)
      return Result::UnsupportedMode;
    if (drm_validation != output::DrmConfigurationError::None)
      return Result::InvalidLayout;
  }
  const auto validation = output::validate_layout(candidate);
  return validation ? Result::Accepted : validation_result(validation.error);
}

void OutputConfigurationCoordinator::clear_staging() noexcept {
  snapshot_id_ = 0;
  expected_item_count_ = 0;
  item_count_ = 0;
  snapshot_failure_ = OutputConfigurationSnapshotStatus::Accepted;
  staged_result_ = Result::InvalidLayout;
  staged_outputs_.clear();
  staged_vrr_policies_.clear();
  if (!transaction_)
    stage_ = OutputConfigurationStage::Idle;
}

void OutputConfigurationCoordinator::clear_transaction() noexcept {
  transaction_.reset();
  rollback_result_ = Result::CompositorRejected;
  stage_ = OutputConfigurationStage::Idle;
}

} // namespace glasswyrm::server
