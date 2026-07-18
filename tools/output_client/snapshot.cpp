#include "output_client/internal.hpp"

#include <algorithm>
#include <memory>

namespace glasswyrm::tools::output_client {
namespace {

template <typename Type, void (*Destroy)(Type *)>
using Owned = std::unique_ptr<Type, decltype(Destroy)>;

bool fail(std::string &error, const char *detail) {
  error = detail;
  return false;
}

} // namespace

bool SnapshotDecoder::consume_control(const gwipc_message *message,
                                      std::string &error) {
  gwipc_decoded_control *raw = nullptr;
  if (gwipc_control_decode_message(message, &raw) != GWIPC_STATUS_OK)
    return fail(error, "control server sent malformed snapshot framing");
  Owned<gwipc_decoded_control, gwipc_decoded_control_destroy> decoded(
      raw, gwipc_decoded_control_destroy);
  if (gwipc_message_type(message) == GWIPC_MESSAGE_SNAPSHOT_BEGIN) {
    const auto *begin = gwipc_decoded_snapshot_begin(decoded.get());
    if (!begin || reading_ || ended_ || begin->domain != GWIPC_SNAPSHOT_OUTPUTS ||
        begin->snapshot_id == 0 || begin->generation == 0 || begin->flags != 0 ||
        begin->expected_item_count > UINT16_MAX)
      return fail(error, "control server sent an invalid output snapshot begin");
    snapshot_id_ = begin->snapshot_id;
    snapshot_.generation = begin->generation;
    expected_items_ = begin->expected_item_count;
    reading_ = true;
    return true;
  }
  const auto *end = gwipc_decoded_snapshot_end(decoded.get());
  if (!end || !reading_ || ended_ || end->snapshot_id != snapshot_id_ ||
      end->generation != snapshot_.generation ||
      end->actual_item_count != actual_items_ ||
      end->actual_item_count != expected_items_)
    return fail(error, "control server sent an incomplete output snapshot");
  reading_ = false;
  ended_ = true;
  complete_ = acknowledged_;
  return true;
}

bool SnapshotDecoder::consume_contract(const gwipc_message *message,
                                       std::string &error) {
  gwipc_decoded_contract *raw = nullptr;
  if (gwipc_contract_decode_message(message, &raw) != GWIPC_STATUS_OK)
    return fail(error, "control server sent a malformed output record");
  Owned<gwipc_decoded_contract, gwipc_decoded_contract_destroy> decoded(
      raw, gwipc_decoded_contract_destroy);
  const auto type = gwipc_message_type(message);
  if (type == GWIPC_MESSAGE_OUTPUT_CONFIGURATION_ACKNOWLEDGED) {
    const auto *ack =
        gwipc_decoded_output_configuration_acknowledged(decoded.get());
    if (!ack || ack->request_id != request_id_ ||
        ack->result != GWIPC_OUTPUT_CONFIGURATION_ACCEPTED ||
        ack->applied_generation != snapshot_.generation)
      return fail(error, "control server rejected the output query");
    snapshot_.primary_output_id = ack->primary_output_id;
    snapshot_.root_width = ack->root_logical_width;
    snapshot_.root_height = ack->root_logical_height;
    snapshot_.enabled_output_count = ack->enabled_output_count;
    acknowledged_ = true;
    complete_ = ended_;
    return true;
  }
  if (!reading_ || (gwipc_message_flags(message) & GWIPC_FLAG_SNAPSHOT_ITEM) == 0)
    return fail(error, "control server sent an output item outside a snapshot");
  ++actual_items_;
  if (actual_items_ > expected_items_)
    return fail(error, "control server exceeded its output snapshot count");
  if (type == GWIPC_MESSAGE_OUTPUT_DESCRIPTOR_UPSERT) {
    const auto *value = gwipc_decoded_output_descriptor_upsert(decoded.get());
    if (!value || snapshot_.descriptors.contains(value->output_id))
      return fail(error, "output snapshot contains a duplicate descriptor");
    OutputDescriptor descriptor;
    descriptor.id = value->output_id;
    descriptor.kind = value->kind;
    descriptor.capabilities = value->capability_flags;
    descriptor.name.assign(value->name, value->name_length);
    descriptor.physical_width_mm = value->physical_width_millimeters;
    descriptor.physical_height_mm = value->physical_height_millimeters;
    descriptor.transforms = value->supported_transform_mask;
    descriptor.minimum_scale_numerator = value->minimum_scale_numerator;
    descriptor.minimum_scale_denominator = value->minimum_scale_denominator;
    descriptor.maximum_scale_numerator = value->maximum_scale_numerator;
    descriptor.maximum_scale_denominator = value->maximum_scale_denominator;
    descriptor.maximum_scale_denominator_value =
        value->maximum_scale_denominator_value;
    descriptor.maximum_physical_width = value->maximum_physical_width;
    descriptor.maximum_physical_height = value->maximum_physical_height;
    snapshot_.descriptors.emplace(descriptor.id, std::move(descriptor));
  } else if (type == GWIPC_MESSAGE_OUTPUT_MODE_UPSERT) {
    const auto *value = gwipc_decoded_output_mode_upsert(decoded.get());
    if (!value)
      return fail(error, "output snapshot contains an invalid mode");
    snapshot_.modes.push_back({value->mode_id, value->output_id,
                               value->physical_width, value->physical_height,
                               value->refresh_millihertz,
                               value->preferred != 0, value->current != 0});
  } else if (type == GWIPC_MESSAGE_OUTPUT_UPSERT) {
    const auto *value = gwipc_decoded_output_upsert(decoded.get());
    if (!value || snapshot_.outputs.contains(value->output_id))
      return fail(error, "output snapshot contains duplicate layout state");
    snapshot_.outputs.emplace(
        value->output_id,
        OutputState{value->output_id,
                    value->enabled != 0,
                    value->logical_x,
                    value->logical_y,
                    value->logical_width,
                    value->logical_height,
                    value->physical_pixel_width,
                    value->physical_pixel_height,
                    value->refresh_millihertz,
                    value->scale_numerator,
                    value->scale_denominator,
                    value->transform});
  } else if (type == GWIPC_MESSAGE_SURFACE_UPSERT) {
    const auto *value = gwipc_decoded_surface_upsert(decoded.get());
    if (!value || value->x11_window_id == 0)
      return fail(error, "window snapshot contains an invalid surface");
    auto &window = snapshot_.windows[value->x11_window_id];
    window.surface_id = value->surface_id;
    window.window_id = value->x11_window_id;
    window.x = value->logical_x;
    window.y = value->logical_y;
    window.width = value->logical_width;
    window.height = value->logical_height;
    window.primary_output_id = value->output_id;
    window.visible = value->visible != 0;
  } else if (type == GWIPC_MESSAGE_SURFACE_OUTPUT_STATE) {
    const auto *value = gwipc_decoded_surface_output_state(decoded.get());
    if (!value)
      return fail(error, "window snapshot contains invalid membership");
    auto found = std::find_if(snapshot_.windows.begin(), snapshot_.windows.end(),
                              [value](const auto &entry) {
                                return entry.second.surface_id == value->surface_id;
                              });
    if (found == snapshot_.windows.end())
      return fail(error, "window membership precedes its surface record");
    auto &window = found->second;
    window.primary_output_id = value->primary_output_id;
    window.output_ids.assign(value->output_ids,
                             value->output_ids + value->output_count);
    window.preferred_scale_numerator = value->preferred_scale_numerator;
    window.preferred_scale_denominator = value->preferred_scale_denominator;
    window.client_buffer_scale = value->client_buffer_scale;
    window.scale_mode = value->scale_mode;
  } else if (type == GWIPC_MESSAGE_SURFACE_POLICY_UPSERT) {
    const auto *value = gwipc_decoded_surface_policy_upsert(decoded.get());
    if (!value || value->x11_window_id == 0)
      return fail(error, "window snapshot contains invalid policy state");
    auto &window = snapshot_.windows[value->x11_window_id];
    window.surface_id = value->surface_id;
    window.window_id = value->x11_window_id;
    window.focused = value->focused != 0;
    window.fullscreen = value->applied_state == GWIPC_POLICY_APPLIED_FULLSCREEN;
  } else {
    return fail(error, "control server sent an unexpected snapshot item");
  }
  return true;
}

bool SnapshotDecoder::consume(const gwipc_message *message,
                              std::string &error) {
  const auto type = gwipc_message_type(message);
  if (type == GWIPC_MESSAGE_SNAPSHOT_BEGIN ||
      type == GWIPC_MESSAGE_SNAPSHOT_END)
    return consume_control(message, error);
  if (type == GWIPC_MESSAGE_SNAPSHOT_ABORT)
    return fail(error, "control server aborted the output snapshot");
  return consume_contract(message, error);
}

} // namespace glasswyrm::tools::output_client
