#include "glasswyrmd/compositor_output_inventory.hpp"

#include <algorithm>
#include <iterator>
#include <memory>

namespace glasswyrm::server {
namespace {

constexpr std::uint32_t kQueryFlags =
    GWIPC_OUTPUT_QUERY_DESCRIPTORS | GWIPC_OUTPUT_QUERY_MODES |
    GWIPC_OUTPUT_QUERY_LAYOUT;
constexpr std::uint32_t kMaximumInventoryItems =
    static_cast<std::uint32_t>(output::kMaximumOutputs *
                               (output::kMaximumModesPerOutput + 6U));

struct ContractPayloadDelete {
  void operator()(gwipc_contract_payload *value) const {
    gwipc_contract_payload_destroy(value);
  }
};

struct DecodedContractDelete {
  void operator()(gwipc_decoded_contract *value) const {
    gwipc_decoded_contract_destroy(value);
  }
};

struct DecodedControlDelete {
  void operator()(gwipc_decoded_control *value) const {
    gwipc_decoded_control_destroy(value);
  }
};

using DecodedContract =
    std::unique_ptr<gwipc_decoded_contract, DecodedContractDelete>;
using DecodedControl =
    std::unique_ptr<gwipc_decoded_control, DecodedControlDelete>;

DecodedContract decode_contract(const gwipc_message *message) {
  gwipc_decoded_contract *raw = nullptr;
  if (gwipc_contract_decode_message(message, &raw) != GWIPC_STATUS_OK)
    return {};
  return DecodedContract(raw);
}

DecodedControl decode_control(const gwipc_message *message) {
  gwipc_decoded_control *raw = nullptr;
  if (gwipc_control_decode_message(message, &raw) != GWIPC_STATUS_OK)
    return {};
  return DecodedControl(raw);
}

bool has_flag(const std::uint32_t flags, const std::uint32_t flag) noexcept {
  return (flags & flag) != 0;
}

class GwipcInventoryQuerySink final : public CompositorInventoryQuerySink {
public:
  explicit GwipcInventoryQuerySink(gwipc_connection *connection)
      : connection_(connection) {}

  gwipc_status enqueue(const gwipc_outgoing_message &message,
                       std::uint64_t &sequence) override {
    return gwipc_connection_enqueue_with_sequence(connection_, &message,
                                                  &sequence);
  }

private:
  gwipc_connection *connection_{};
};

std::string mode_name(const gwipc_output_mode_upsert &mode) {
  return "MODE-" + std::to_string(mode.mode_id);
}

output::SdrMetadata color(const gwipc_sdr_color_metadata &value) {
  return {static_cast<output::SdrColorSpace>(value.color_space),
          static_cast<output::SdrTransferFunction>(value.transfer_function),
          static_cast<output::SdrColorPrimaries>(value.primaries),
          value.luminance_available != 0,
          value.minimum_luminance_millinit,
          value.maximum_luminance_millinit,
          value.max_frame_average_luminance_millinit};
}

} // namespace

bool CompositorOutputInventory::fail(const CompositorInventoryFailure failure,
                                     const char *message,
                                     std::string &error) {
  state_ = CompositorInventoryState::Failed;
  failure_ = failure;
  failure_message_ = message;
  completed_layout_.reset();
  error = failure_message_;
  return false;
}

bool CompositorOutputInventory::start(gwipc_connection *connection,
                                      const std::uint64_t query_id,
                                      std::string &error) {
  if (connection == nullptr)
    return fail(CompositorInventoryFailure::InvalidQuery,
                "output inventory query requires a connection and identity",
                error);
  GwipcInventoryQuerySink sink(connection);
  return start(sink, query_id, error);
}

bool CompositorOutputInventory::start(CompositorInventoryQuerySink &sink,
                                      const std::uint64_t query_id,
                                      std::string &error) {
  if (state_ != CompositorInventoryState::Idle)
    return fail(CompositorInventoryFailure::InvalidState,
                "output inventory query was already started", error);
  if (query_id == 0)
    return fail(CompositorInventoryFailure::InvalidQuery,
                "output inventory query requires a connection and identity",
                error);

  gwipc_output_state_query query{};
  query.struct_size = sizeof(query);
  query.query_id = query_id;
  query.flags =
      kQueryFlags |
      (vrr_profile_ ? static_cast<std::uint32_t>(GWIPC_OUTPUT_QUERY_VRR) : 0U);
  gwipc_contract_payload *raw_payload = nullptr;
  const auto encode_status =
      gwipc_contract_encode_output_state_query(&query, &raw_payload);
  std::unique_ptr<gwipc_contract_payload, ContractPayloadDelete> payload(
      raw_payload);
  if (encode_status != GWIPC_STATUS_OK)
    return fail(CompositorInventoryFailure::EncodeFailed,
                "could not encode output inventory query", error);

  std::size_t payload_size = 0;
  const auto *payload_data =
      gwipc_contract_payload_data(payload.get(), &payload_size);
  gwipc_outgoing_message outgoing{};
  outgoing.struct_size = sizeof(outgoing);
  outgoing.type = GWIPC_MESSAGE_OUTPUT_STATE_QUERY;
  outgoing.flags = GWIPC_FLAG_ACK_REQUIRED;
  outgoing.payload = payload_data;
  outgoing.payload_size = payload_size;
  std::uint64_t sequence = 0;
  if (sink.enqueue(outgoing, sequence) != GWIPC_STATUS_OK || sequence == 0)
    return fail(CompositorInventoryFailure::EnqueueFailed,
                "could not enqueue output inventory query", error);

  query_id_ = query_id;
  query_sequence_ = sequence;
  state_ = CompositorInventoryState::AwaitingSnapshot;
  error.clear();
  return true;
}

bool CompositorOutputInventory::consume(const gwipc_message *message,
                                        std::string &error) {
  if (state_ == CompositorInventoryState::Failed) {
    error = failure_message_;
    return false;
  }
  if (message == nullptr || state_ == CompositorInventoryState::Idle ||
      state_ == CompositorInventoryState::Complete)
    return fail(CompositorInventoryFailure::InvalidState,
                "output inventory received a message in an invalid state",
                error);
  if (gwipc_message_fd_count(message) != 0)
    return fail(CompositorInventoryFailure::InvalidEnvelope,
                "output inventory record carried file descriptors", error);

  const auto type = gwipc_message_type(message);
  if (state_ == CompositorInventoryState::AwaitingSnapshot)
    return type == GWIPC_MESSAGE_SNAPSHOT_BEGIN
               ? consume_begin(message, error)
               : fail(CompositorInventoryFailure::UnexpectedMessage,
                      "output inventory did not begin with an Outputs snapshot",
                      error);
  if (state_ == CompositorInventoryState::AwaitingAcknowledgement)
    return type == GWIPC_MESSAGE_OUTPUT_CONFIGURATION_ACKNOWLEDGED
               ? consume_acknowledgement(message, error)
               : fail(CompositorInventoryFailure::UnexpectedMessage,
                      "output inventory snapshot was not followed by its reply",
                      error);

  switch (type) {
  case GWIPC_MESSAGE_OUTPUT_DESCRIPTOR_UPSERT:
    return consume_descriptor(message, error);
  case GWIPC_MESSAGE_OUTPUT_MODE_UPSERT:
    return consume_mode(message, error);
  case GWIPC_MESSAGE_OUTPUT_UPSERT:
    return consume_state(message, error);
  case GWIPC_MESSAGE_OUTPUT_VRR_CAPABILITY_UPSERT:
    return consume_vrr_capability(message, error);
  case GWIPC_MESSAGE_OUTPUT_VRR_POLICY_UPSERT:
    return consume_vrr_policy(message, error);
  case GWIPC_MESSAGE_OUTPUT_VRR_STATE_UPSERT:
    return consume_vrr_state(message, error);
  case GWIPC_MESSAGE_PRESENTATION_TIMING:
    return consume_timing(message, error);
  case GWIPC_MESSAGE_SNAPSHOT_END:
    return consume_end(message, error);
  default:
    return fail(CompositorInventoryFailure::UnexpectedMessage,
                "output inventory contained an unexpected record", error);
  }
}

bool CompositorOutputInventory::consume_vrr_capability(
    const gwipc_message* message, std::string& error) {
  if (!vrr_profile_ ||
      (state_ != CompositorInventoryState::ReadingLayout &&
       state_ != CompositorInventoryState::ReadingVrrCapabilities))
    return fail(CompositorInventoryFailure::InvalidOrder,
                "output VRR capability arrived out of order", error);
  if (!consume_item_envelope(message, error)) return false;
  const auto decoded = decode_contract(message);
  const auto* record = decoded
                           ? gwipc_decoded_output_vrr_capability_upsert(
                                 decoded.get())
                           : nullptr;
  const auto index = vrr_capabilities_.size();
  if (!record || index >= descriptor_order_.size() ||
      record->output_id != descriptor_order_[index].value)
    return fail(CompositorInventoryFailure::InvalidRecord,
                "output VRR capability did not match inventory order", error);
  vrr_capabilities_.push_back(*record);
  state_ = CompositorInventoryState::ReadingVrrCapabilities;
  error.clear();
  return true;
}

bool CompositorOutputInventory::consume_vrr_policy(
    const gwipc_message* message, std::string& error) {
  if (!vrr_profile_ ||
      (state_ != CompositorInventoryState::ReadingVrrCapabilities &&
       state_ != CompositorInventoryState::ReadingVrrPolicies))
    return fail(CompositorInventoryFailure::InvalidOrder,
                "output VRR policy arrived out of order", error);
  if (!consume_item_envelope(message, error)) return false;
  const auto decoded = decode_contract(message);
  const auto* record = decoded
                           ? gwipc_decoded_output_vrr_policy_upsert(
                                 decoded.get())
                           : nullptr;
  const auto index = vrr_policies_.size();
  if (!record || vrr_capabilities_.size() != descriptor_order_.size() ||
      index >= descriptor_order_.size() ||
      record->output_id != descriptor_order_[index].value)
    return fail(CompositorInventoryFailure::InvalidRecord,
                "output VRR policy did not match inventory order", error);
  vrr_policies_.push_back(*record);
  state_ = CompositorInventoryState::ReadingVrrPolicies;
  error.clear();
  return true;
}

bool CompositorOutputInventory::consume_vrr_state(
    const gwipc_message* message, std::string& error) {
  if (!vrr_profile_ ||
      (state_ != CompositorInventoryState::ReadingVrrPolicies &&
       state_ != CompositorInventoryState::ReadingVrrStates))
    return fail(CompositorInventoryFailure::InvalidOrder,
                "output VRR state arrived out of order", error);
  if (!consume_item_envelope(message, error)) return false;
  const auto decoded = decode_contract(message);
  const auto* record = decoded
                           ? gwipc_decoded_output_vrr_state_upsert(decoded.get())
                           : nullptr;
  const auto index = vrr_states_.size();
  if (!record || vrr_policies_.size() != descriptor_order_.size() ||
      index >= descriptor_order_.size() ||
      record->output_id != descriptor_order_[index].value)
    return fail(CompositorInventoryFailure::InvalidRecord,
                "output VRR state did not match inventory order", error);
  vrr_states_.push_back(*record);
  state_ = CompositorInventoryState::ReadingVrrStates;
  error.clear();
  return true;
}

bool CompositorOutputInventory::consume_timing(
    const gwipc_message* message, std::string& error) {
  if (!vrr_profile_ ||
      (state_ != CompositorInventoryState::ReadingVrrStates &&
       state_ != CompositorInventoryState::ReadingPresentationTiming))
    return fail(CompositorInventoryFailure::InvalidOrder,
                "presentation timing arrived out of order", error);
  if (!consume_item_envelope(message, error)) return false;
  const auto decoded = decode_contract(message);
  const auto* record =
      decoded ? gwipc_decoded_presentation_timing(decoded.get()) : nullptr;
  if (!record || vrr_states_.size() != descriptor_order_.size() ||
      std::ranges::find(descriptor_order_,
                        output::OutputId{record->output_id}) ==
          descriptor_order_.end() ||
      std::ranges::any_of(timings_, [&](const auto& value) {
        return value.output_id == record->output_id;
      }))
    return fail(CompositorInventoryFailure::InvalidRecord,
                "presentation timing did not match inventory order", error);
  timings_.push_back(*record);
  state_ = CompositorInventoryState::ReadingPresentationTiming;
  error.clear();
  return true;
}

bool CompositorOutputInventory::consume_item_envelope(
    const gwipc_message *message, std::string &error) {
  if (gwipc_message_flags(message) != GWIPC_FLAG_SNAPSHOT_ITEM ||
      gwipc_message_reply_to(message) != 0 ||
      gwipc_message_sequence(message) == 0)
    return fail(CompositorInventoryFailure::InvalidEnvelope,
                "output inventory item had invalid envelope metadata", error);
  if (item_count_ == expected_item_count_ ||
      item_count_ == kMaximumInventoryItems)
    return fail(CompositorInventoryFailure::CountMismatch,
                "output inventory exceeded its declared item count", error);
  ++item_count_;
  return true;
}

bool CompositorOutputInventory::consume_begin(const gwipc_message *message,
                                              std::string &error) {
  if (gwipc_message_flags(message) != 0 ||
      gwipc_message_reply_to(message) != 0 ||
      gwipc_message_sequence(message) == 0)
    return fail(CompositorInventoryFailure::InvalidEnvelope,
                "output inventory begin had invalid envelope metadata", error);
  const auto decoded = decode_control(message);
  const auto *begin = decoded ? gwipc_decoded_snapshot_begin(decoded.get())
                              : nullptr;
  if (begin == nullptr || begin->domain != GWIPC_SNAPSHOT_OUTPUTS ||
      begin->flags != 0 || begin->snapshot_id == 0 || begin->generation == 0 ||
      begin->expected_item_count == UINT32_MAX ||
      begin->expected_item_count > kMaximumInventoryItems)
    return fail(CompositorInventoryFailure::InvalidSnapshot,
                "output inventory begin was invalid", error);
  snapshot_id_ = begin->snapshot_id;
  generation_ = begin->generation;
  expected_item_count_ = begin->expected_item_count;
  pending_layout_.generation = generation_;
  state_ = CompositorInventoryState::ReadingDescriptors;
  error.clear();
  return true;
}

bool CompositorOutputInventory::consume_descriptor(
    const gwipc_message *message, std::string &error) {
  if (state_ != CompositorInventoryState::ReadingDescriptors)
    return fail(CompositorInventoryFailure::InvalidOrder,
                "output descriptor arrived after another inventory class",
                error);
  if (!consume_item_envelope(message, error))
    return false;
  const auto decoded = decode_contract(message);
  const auto *record =
      decoded ? gwipc_decoded_output_descriptor_upsert(decoded.get()) : nullptr;
  if (record == nullptr || record->name == nullptr ||
      descriptor_order_.size() == output::kMaximumOutputs)
    return fail(CompositorInventoryFailure::InvalidRecord,
                "output inventory descriptor was invalid", error);

  output::OutputDescriptor descriptor;
  descriptor.id = {record->output_id};
  descriptor.name.assign(record->name, record->name_length);
  descriptor.kind = static_cast<output::OutputKind>(record->kind);
  descriptor.connected =
      has_flag(record->capability_flags, GWIPC_OUTPUT_CAP_CONNECTED);
  descriptor.arbitrary_headless_mode = has_flag(
      record->capability_flags, GWIPC_OUTPUT_CAP_ARBITRARY_HEADLESS_MODE);
  descriptor.mode_configurable =
      !has_flag(record->capability_flags, GWIPC_OUTPUT_CAP_MODE_FIXED);
  descriptor.scale_configurable = has_flag(
      record->capability_flags, GWIPC_OUTPUT_CAP_SCALE_CONFIGURABLE);
  descriptor.transform_configurable = has_flag(
      record->capability_flags, GWIPC_OUTPUT_CAP_TRANSFORM_CONFIGURABLE);
  descriptor.primary_eligible = has_flag(
      record->capability_flags, GWIPC_OUTPUT_CAP_PRIMARY_ELIGIBLE);
  descriptor.physical_width_mm = record->physical_width_millimeters;
  descriptor.physical_height_mm = record->physical_height_millimeters;
  descriptor.supported_transform_mask = record->supported_transform_mask;
  descriptor.minimum_scale = {record->minimum_scale_numerator,
                              record->minimum_scale_denominator};
  descriptor.maximum_scale = {record->maximum_scale_numerator,
                              record->maximum_scale_denominator};
  descriptor.maximum_scale_denominator =
      record->maximum_scale_denominator_value;
  descriptor.maximum_physical_width = record->maximum_physical_width;
  descriptor.maximum_physical_height = record->maximum_physical_height;
  descriptor.maximum_physical_pixels = output::kMaximumPhysicalPixels;
  const auto id = descriptor.id;
  if (!id || pending_layout_.descriptors.contains(id))
    return fail(CompositorInventoryFailure::InvalidRecord,
                "output inventory descriptor identity was duplicated", error);
  descriptor_order_.push_back(id);
  pending_layout_.descriptors.emplace(id, std::move(descriptor));
  error.clear();
  return true;
}

bool CompositorOutputInventory::consume_mode(const gwipc_message *message,
                                             std::string &error) {
  if (state_ != CompositorInventoryState::ReadingDescriptors &&
      state_ != CompositorInventoryState::ReadingModes)
    return fail(CompositorInventoryFailure::InvalidOrder,
                "output mode arrived after layout state", error);
  if (!consume_item_envelope(message, error))
    return false;
  const auto decoded = decode_contract(message);
  const auto *record =
      decoded ? gwipc_decoded_output_mode_upsert(decoded.get()) : nullptr;
  if (record == nullptr)
    return fail(CompositorInventoryFailure::InvalidRecord,
                "output inventory mode was invalid", error);
  const output::OutputId output_id{record->output_id};
  const auto found = std::ranges::find(descriptor_order_, output_id);
  if (found == descriptor_order_.end())
    return fail(CompositorInventoryFailure::InvalidOrder,
                "output modes did not follow descriptor order", error);
  const auto position = static_cast<std::size_t>(
      std::distance(descriptor_order_.begin(), found));
  if (position < mode_output_index_)
    return fail(CompositorInventoryFailure::InvalidOrder,
                "output modes did not follow descriptor order", error);
  if (position != mode_output_index_) {
    mode_output_index_ = position;
    last_mode_id_ = {};
  }
  const output::OutputModeId mode_id{record->mode_id};
  if (!mode_id || (last_mode_id_ && mode_id <= last_mode_id_))
    return fail(CompositorInventoryFailure::InvalidOrder,
                "output modes were not ordered by stable identity", error);
  auto &descriptor = pending_layout_.descriptors.at(output_id);
  if (descriptor.modes.size() == output::kMaximumModesPerOutput)
    return fail(CompositorInventoryFailure::InvalidRecord,
                "output inventory exceeded the per-output mode limit", error);
  descriptor.modes.push_back(
      {mode_id, output_id, record->physical_width, record->physical_height,
       record->refresh_millihertz, record->flags, mode_name(*record),
       record->preferred != 0, record->current != 0});
  last_mode_id_ = mode_id;
  state_ = CompositorInventoryState::ReadingModes;
  error.clear();
  return true;
}

bool CompositorOutputInventory::consume_state(const gwipc_message *message,
                                              std::string &error) {
  if (state_ != CompositorInventoryState::ReadingDescriptors &&
      state_ != CompositorInventoryState::ReadingModes &&
      state_ != CompositorInventoryState::ReadingLayout)
    return fail(CompositorInventoryFailure::InvalidOrder,
                "output layout state arrived out of order", error);
  if (!consume_item_envelope(message, error))
    return false;
  const auto decoded = decode_contract(message);
  const auto *record = decoded ? gwipc_decoded_output_upsert(decoded.get())
                               : nullptr;
  if (record == nullptr || state_index_ >= descriptor_order_.size() ||
      record->output_id != descriptor_order_[state_index_].value)
    return fail(CompositorInventoryFailure::InvalidOrder,
                "output layout did not follow descriptor order", error);

  output::OutputState state;
  state.output_id = {record->output_id};
  state.enabled = record->enabled != 0;
  state.logical_x = record->logical_x;
  state.logical_y = record->logical_y;
  state.logical_width = record->logical_width;
  state.logical_height = record->logical_height;
  state.physical_width = record->physical_pixel_width;
  state.physical_height = record->physical_pixel_height;
  state.refresh_millihertz = record->refresh_millihertz;
  state.scale = {record->scale_numerator, record->scale_denominator};
  state.transform = static_cast<output::OutputTransform>(record->transform);
  state.color = color(record->color);
  state.generation = generation_;
  if (state.enabled) {
    const auto &modes = pending_layout_.descriptors.at(state.output_id).modes;
    const auto current = std::ranges::find_if(modes, [&](const auto &mode) {
      return mode.current && mode.physical_width == state.physical_width &&
             mode.physical_height == state.physical_height &&
             mode.refresh_millihertz == state.refresh_millihertz;
    });
    if (current == modes.end())
      return fail(CompositorInventoryFailure::InvalidRecord,
                  "enabled output state did not match its current mode", error);
    state.mode_id = current->id;
  }
  if (!pending_layout_.states.emplace(state.output_id, state).second)
    return fail(CompositorInventoryFailure::InvalidRecord,
                "output layout state identity was duplicated", error);
  ++state_index_;
  state_ = CompositorInventoryState::ReadingLayout;
  error.clear();
  return true;
}

bool CompositorOutputInventory::consume_end(const gwipc_message *message,
                                            std::string &error) {
  if (gwipc_message_flags(message) != 0 ||
      gwipc_message_reply_to(message) != 0 ||
      gwipc_message_sequence(message) == 0)
    return fail(CompositorInventoryFailure::InvalidEnvelope,
                "output inventory end had invalid envelope metadata", error);
  const auto decoded = decode_control(message);
  const auto *end = decoded ? gwipc_decoded_snapshot_end(decoded.get()) : nullptr;
  if (end == nullptr || end->snapshot_id != snapshot_id_ ||
      end->generation != generation_)
    return fail(CompositorInventoryFailure::InvalidSnapshot,
                "output inventory end did not match its begin", error);
  if (item_count_ != expected_item_count_ ||
      end->actual_item_count != item_count_ || descriptor_order_.empty() ||
      state_index_ != descriptor_order_.size() ||
      pending_layout_.states.size() != pending_layout_.descriptors.size() ||
      (vrr_profile_ &&
       (vrr_capabilities_.size() != descriptor_order_.size() ||
        vrr_policies_.size() != descriptor_order_.size() ||
        vrr_states_.size() != descriptor_order_.size())))
    return fail(CompositorInventoryFailure::CountMismatch,
                "output inventory item counts were incomplete", error);
  state_ = CompositorInventoryState::AwaitingAcknowledgement;
  error.clear();
  return true;
}

bool CompositorOutputInventory::consume_acknowledgement(
    const gwipc_message *message, std::string &error) {
  if (gwipc_message_flags(message) != GWIPC_FLAG_REPLY ||
      gwipc_message_reply_to(message) != query_sequence_ ||
      gwipc_message_sequence(message) == 0)
    return fail(CompositorInventoryFailure::CorrelationMismatch,
                "output inventory acknowledgement was not correlated", error);
  const auto decoded = decode_contract(message);
  const auto *ack =
      decoded
          ? gwipc_decoded_output_configuration_acknowledged(decoded.get())
          : nullptr;
  if (ack == nullptr || ack->request_id != query_id_ ||
      ack->applied_generation != generation_)
    return fail(CompositorInventoryFailure::CorrelationMismatch,
                "output inventory acknowledgement identity did not match",
                error);
  if (ack->result != GWIPC_OUTPUT_CONFIGURATION_ACCEPTED)
    return fail(CompositorInventoryFailure::Rejected,
                "compositor rejected the output inventory query", error);

  pending_layout_.primary_output_id = {ack->primary_output_id};
  pending_layout_.root_logical_width = ack->root_logical_width;
  pending_layout_.root_logical_height = ack->root_logical_height;
  pending_layout_.enabled_output_count = ack->enabled_output_count;
  pending_layout_.output_order = descriptor_order_;
  const auto primary =
      pending_layout_.states.find(pending_layout_.primary_output_id);
  if (primary != pending_layout_.states.end())
    primary->second.primary = true;
  const auto validation = output::validate_layout(pending_layout_);
  if (!validation)
    return fail(CompositorInventoryFailure::InvalidLayout,
                "compositor output inventory failed shared-model validation",
                error);
  completed_layout_ = std::move(pending_layout_);
  state_ = CompositorInventoryState::Complete;
  error.clear();
  return true;
}

const char *compositor_inventory_failure_name(
    const CompositorInventoryFailure failure) noexcept {
  switch (failure) {
  case CompositorInventoryFailure::None:
    return "none";
  case CompositorInventoryFailure::InvalidState:
    return "invalid-state";
  case CompositorInventoryFailure::InvalidQuery:
    return "invalid-query";
  case CompositorInventoryFailure::EncodeFailed:
    return "encode-failed";
  case CompositorInventoryFailure::EnqueueFailed:
    return "enqueue-failed";
  case CompositorInventoryFailure::UnexpectedMessage:
    return "unexpected-message";
  case CompositorInventoryFailure::InvalidEnvelope:
    return "invalid-envelope";
  case CompositorInventoryFailure::InvalidSnapshot:
    return "invalid-snapshot";
  case CompositorInventoryFailure::InvalidOrder:
    return "invalid-order";
  case CompositorInventoryFailure::InvalidRecord:
    return "invalid-record";
  case CompositorInventoryFailure::CountMismatch:
    return "count-mismatch";
  case CompositorInventoryFailure::CorrelationMismatch:
    return "correlation-mismatch";
  case CompositorInventoryFailure::Rejected:
    return "rejected";
  case CompositorInventoryFailure::InvalidLayout:
    return "invalid-layout";
  }
  return "unknown";
}

} // namespace glasswyrm::server
