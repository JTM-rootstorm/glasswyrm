#include "glasswyrmd/randr_state.hpp"

#include <algorithm>
#include <limits>
#include <new>

namespace glasswyrm::server {

namespace {

bool same_inventory_identity(const output::OutputLayout& left,
                             const output::OutputLayout& right) noexcept {
  if (left.descriptors.size() != right.descriptors.size()) return false;
  for (const auto& [id, descriptor] : left.descriptors) {
    const auto found = right.descriptors.find(id);
    if (found == right.descriptors.end() ||
        found->second.name != descriptor.name ||
        found->second.kind != descriptor.kind ||
        found->second.modes.size() != descriptor.modes.size())
      return false;
    for (std::size_t index = 0; index < descriptor.modes.size(); ++index) {
      const auto& first = descriptor.modes[index];
      const auto& second = found->second.modes[index];
      if (first.id != second.id || first.output_id != second.output_id ||
          first.physical_width != second.physical_width ||
          first.physical_height != second.physical_height ||
          first.refresh_millihertz != second.refresh_millihertz ||
          first.flags != second.flags || first.name != second.name)
        return false;
    }
  }
  return true;
}

}  // namespace

bool RandRState::configure_output_layout(const output::OutputLayout& layout) {
  if (!output::validate_layout(layout) ||
      (layout_ && !same_inventory_identity(*layout_, layout)))
    return false;

  try {
    auto output_xids = output_xids_;
    auto mode_xids = mode_xids_;
    auto next_xid = next_xid_;
    const auto allocate = [&next_xid]() -> std::optional<std::uint32_t> {
      if (next_xid == 0 ||
          next_xid == std::numeric_limits<std::uint32_t>::max())
        return std::nullopt;
      return next_xid++;
    };

    for (const auto& [id, descriptor] : layout.descriptors) {
      if (!output_xids.contains(id.value)) {
        const auto output_xid = allocate();
        const auto crtc_xid = allocate();
        if (!output_xid || !crtc_xid) return false;
        output_xids.emplace(id.value, ObjectIds{*output_xid, *crtc_xid});
      }
      for (const auto& mode : descriptor.modes) {
        if (mode_xids.contains(mode.id.value)) continue;
        const auto mode_xid = allocate();
        if (!mode_xid) return false;
        mode_xids.emplace(mode.id.value, *mode_xid);
      }
      const auto& state = layout.states.at(id);
      if (state.enabled && !mode_xids.contains(state.mode_id.value)) {
        const auto mode_xid = allocate();
        if (!mode_xid) return false;
        mode_xids.emplace(state.mode_id.value, *mode_xid);
      }
    }

    std::vector<RandROutputObject> objects;
    objects.reserve(layout.output_order.size());
    for (const auto id : layout.output_order) {
      const auto& descriptor = layout.descriptors.at(id);
      const auto& state = layout.states.at(id);
      const auto ids = output_xids.at(id.value);
      RandROutputObject object;
      object.internal_id = id.value;
      object.xid = ids.output;
      object.crtc_xid = ids.crtc;
      object.name = descriptor.name;
      object.connected = descriptor.connected;
      object.enabled = state.enabled;
      object.primary = state.primary;
      object.physical_width_mm = descriptor.physical_width_mm;
      object.physical_height_mm = descriptor.physical_height_mm;
      object.logical_x = state.logical_x;
      object.logical_y = state.logical_y;
      object.logical_width = state.logical_width;
      object.logical_height = state.logical_height;
      object.physical_width = state.physical_width;
      object.physical_height = state.physical_height;
      object.scale = state.scale;
      object.transform = state.transform;
      object.modes.reserve(descriptor.modes.size());
      for (const auto& mode : descriptor.modes) {
        object.modes.push_back(
            {mode.id.value, mode_xids.at(mode.id.value), mode.physical_width,
             mode.physical_height, mode.refresh_millihertz, mode.flags,
             mode.name, mode.preferred, state.mode_id == mode.id});
      }
      const auto current = std::ranges::find(
          descriptor.modes, state.mode_id, &output::OutputMode::id);
      if (state.enabled && current == descriptor.modes.end()) {
        object.modes.push_back(
            {state.mode_id.value, mode_xids.at(state.mode_id.value),
             state.physical_width, state.physical_height,
             state.refresh_millihertz, 0,
             std::to_string(state.physical_width) + "x" +
                 std::to_string(state.physical_height),
             false, true});
      }
      objects.push_back(std::move(object));
    }

    output_xids_ = std::move(output_xids);
    mode_xids_ = std::move(mode_xids);
    next_xid_ = next_xid;
    outputs_ = std::move(objects);
    layout_ = layout;
    return true;
  } catch (const std::bad_alloc&) {
    return false;
  }
}

const RandROutputObject* RandRState::find_output(
    const std::uint32_t xid) const noexcept {
  const auto found = std::ranges::find(outputs_, xid, &RandROutputObject::xid);
  return found == outputs_.end() ? nullptr : &*found;
}

const RandROutputObject* RandRState::find_crtc(
    const std::uint32_t xid) const noexcept {
  const auto found =
      std::ranges::find(outputs_, xid, &RandROutputObject::crtc_xid);
  return found == outputs_.end() ? nullptr : &*found;
}

const RandRModeObject* RandRState::find_mode(
    const std::uint32_t xid) const noexcept {
  for (const auto& output : outputs_) {
    const auto found = std::ranges::find(output.modes, xid,
                                         &RandRModeObject::xid);
    if (found != output.modes.end()) return &*found;
  }
  return nullptr;
}

std::uint32_t RandRState::primary_output_xid() const noexcept {
  const auto found =
      std::ranges::find(outputs_, true, &RandROutputObject::primary);
  return found == outputs_.end() ? 0 : found->xid;
}

std::uint32_t RandRState::configuration_timestamp() const noexcept {
  if (!layout_) return kRandRConfigurationTimestamp;
  const auto timestamp = static_cast<std::uint32_t>(layout_->generation);
  return timestamp == 0 ? kRandRConfigurationTimestamp : timestamp;
}

bool RandRState::select(const ClientId client, const std::uint32_t window,
                        const std::uint16_t mask) {
  const auto matches = [client, window](const RandRSelection& selection) {
    return selection.client == client && selection.window == window;
  };
  const auto found = std::find_if(selections_.begin(), selections_.end(),
                                  matches);
  if (mask == 0) {
    if (found != selections_.end()) selections_.erase(found);
    return true;
  }
  if (found != selections_.end()) {
    found->mask = mask;
    return true;
  }
  try {
    selections_.push_back({client, window, mask});
    return true;
  } catch (...) {
    return false;
  }
}

std::uint16_t RandRState::selection(const ClientId client,
                                    const std::uint32_t window) const noexcept {
  const auto found = std::find_if(
      selections_.begin(), selections_.end(),
      [client, window](const RandRSelection& selection) {
        return selection.client == client && selection.window == window;
      });
  return found == selections_.end() ? 0 : found->mask;
}

std::size_t RandRState::clear_client(const ClientId client) noexcept {
  const auto before = selections_.size();
  std::erase_if(selections_, [client](const RandRSelection& selection) {
    return selection.client == client;
  });
  return before - selections_.size();
}

std::size_t RandRState::clear_window(const std::uint32_t window) noexcept {
  const auto before = selections_.size();
  std::erase_if(selections_, [window](const RandRSelection& selection) {
    return selection.window == window;
  });
  return before - selections_.size();
}

std::size_t RandRState::prune_windows(
    const ResourceTable& resources) noexcept {
  const auto before = selections_.size();
  std::erase_if(selections_, [&resources](const RandRSelection& selection) {
    return resources.find_window(selection.window) == nullptr;
  });
  return before - selections_.size();
}

}  // namespace glasswyrm::server
