#include "gwcomp/output_inventory_publisher.hpp"

#include <algorithm>
#include <memory>
#include <new>

namespace glasswyrm::compositor {
namespace {

struct ContractPayloadDeleter {
  void operator()(gwipc_contract_payload *value) const noexcept {
    gwipc_contract_payload_destroy(value);
  }
};

struct ControlPayloadDeleter {
  void operator()(gwipc_control_payload *value) const noexcept {
    gwipc_control_payload_destroy(value);
  }
};

template <typename Value>
gwipc_status append_contract(
    std::vector<OutputInventoryMessage> &messages, const std::uint16_t type,
    const std::uint32_t flags, const std::uint64_t reply_to, const Value &value,
    gwipc_status (*encode)(const Value *, gwipc_contract_payload **)) {
  gwipc_contract_payload *raw = nullptr;
  const auto status = encode(&value, &raw);
  std::unique_ptr<gwipc_contract_payload, ContractPayloadDeleter> payload(raw);
  if (status != GWIPC_STATUS_OK)
    return status;
  std::size_t size = 0;
  const auto *data = gwipc_contract_payload_data(payload.get(), &size);
  if (size == 0 || data == nullptr)
    return GWIPC_STATUS_SYSTEM_ERROR;
  messages.push_back(
      {type, flags, reply_to, std::vector<std::uint8_t>(data, data + size)});
  return GWIPC_STATUS_OK;
}

template <typename Value>
gwipc_status append_control(std::vector<OutputInventoryMessage> &messages,
                            const std::uint16_t type, const Value &value,
                            gwipc_status (*encode)(const Value *,
                                                   gwipc_control_payload **)) {
  gwipc_control_payload *raw = nullptr;
  const auto status = encode(&value, &raw);
  std::unique_ptr<gwipc_control_payload, ControlPayloadDeleter> payload(raw);
  if (status != GWIPC_STATUS_OK)
    return status;
  std::size_t size = 0;
  const auto *data = gwipc_control_payload_data(payload.get(), &size);
  if (size == 0 || data == nullptr)
    return GWIPC_STATUS_SYSTEM_ERROR;
  messages.push_back(
      {type, 0, 0, std::vector<std::uint8_t>(data, data + size)});
  return GWIPC_STATUS_OK;
}

bool valid_query(const gwipc_output_state_query &query,
                 const std::uint64_t query_sequence,
                 const std::uint64_t snapshot_id) noexcept {
  constexpr auto known_flags =
      GWIPC_OUTPUT_QUERY_DESCRIPTORS | GWIPC_OUTPUT_QUERY_MODES |
      GWIPC_OUTPUT_QUERY_LAYOUT | GWIPC_OUTPUT_QUERY_WINDOWS;
  return query.struct_size >= sizeof(query) && query.query_id != 0 &&
         query.flags != 0 && (query.flags & ~known_flags) == 0 &&
         query_sequence != 0 && snapshot_id != 0 &&
         std::ranges::all_of(query.reserved,
                             [](const auto value) { return value == 0; });
}

std::uint32_t capability_flags(const output::OutputDescriptor &descriptor) {
  std::uint32_t flags = 0;
  if (descriptor.connected)
    flags |= GWIPC_OUTPUT_CAP_CONNECTED;
  if (descriptor.arbitrary_headless_mode)
    flags |= GWIPC_OUTPUT_CAP_ARBITRARY_HEADLESS_MODE;
  if (!descriptor.mode_configurable)
    flags |= GWIPC_OUTPUT_CAP_MODE_FIXED;
  if (descriptor.scale_configurable)
    flags |= GWIPC_OUTPUT_CAP_SCALE_CONFIGURABLE;
  if (descriptor.transform_configurable)
    flags |= GWIPC_OUTPUT_CAP_TRANSFORM_CONFIGURABLE;
  if (descriptor.primary_eligible)
    flags |= GWIPC_OUTPUT_CAP_PRIMARY_ELIGIBLE;
  if (descriptor.physical_width_mm != 0 && descriptor.physical_height_mm != 0)
    flags |= GWIPC_OUTPUT_CAP_PHYSICAL_DIMENSIONS_KNOWN;
  return flags;
}

gwipc_output_descriptor_upsert
descriptor_record(const output::OutputDescriptor &descriptor) {
  gwipc_output_descriptor_upsert value{};
  value.struct_size = sizeof(value);
  value.output_id = descriptor.id.value;
  value.kind = static_cast<gwipc_output_kind>(descriptor.kind);
  value.capability_flags = capability_flags(descriptor);
  value.name = descriptor.name.data();
  value.name_length = descriptor.name.size();
  value.physical_width_millimeters = descriptor.physical_width_mm;
  value.physical_height_millimeters = descriptor.physical_height_mm;
  value.supported_transform_mask = descriptor.supported_transform_mask;
  value.minimum_scale_numerator = descriptor.minimum_scale.numerator;
  value.minimum_scale_denominator = descriptor.minimum_scale.denominator;
  value.maximum_scale_numerator = descriptor.maximum_scale.numerator;
  value.maximum_scale_denominator = descriptor.maximum_scale.denominator;
  value.maximum_scale_denominator_value = descriptor.maximum_scale_denominator;
  value.maximum_physical_width = descriptor.maximum_physical_width;
  value.maximum_physical_height = descriptor.maximum_physical_height;
  return value;
}

gwipc_output_mode_upsert mode_record(const output::OutputMode &mode) {
  gwipc_output_mode_upsert value{};
  value.struct_size = sizeof(value);
  value.output_id = mode.output_id.value;
  value.mode_id = mode.id.value;
  value.physical_width = mode.physical_width;
  value.physical_height = mode.physical_height;
  value.refresh_millihertz = mode.refresh_millihertz;
  value.preferred = mode.preferred;
  value.current = mode.current;
  // Native mode flags contribute to the stable mode identity, but GWIPC 0.8
  // reserves the published mode-flags field to zero.
  value.flags = 0;
  return value;
}

gwipc_sdr_color_metadata color_record(const output::SdrMetadata &color) {
  return {static_cast<gwipc_sdr_color_space>(color.color_space),
          static_cast<gwipc_transfer_function>(color.transfer_function),
          static_cast<gwipc_color_primaries>(color.primaries),
          static_cast<std::uint8_t>(color.luminance_available),
          color.minimum_luminance_millinit,
          color.maximum_luminance_millinit,
          color.max_frame_average_luminance_millinit};
}

gwipc_output_upsert state_record(const output::OutputState &state) {
  gwipc_output_upsert value{};
  value.struct_size = sizeof(value);
  value.output_id = state.output_id.value;
  value.enabled = state.enabled;
  value.logical_x = state.logical_x;
  value.logical_y = state.logical_y;
  value.logical_width = state.logical_width;
  value.logical_height = state.logical_height;
  value.physical_pixel_width = state.physical_width;
  value.physical_pixel_height = state.physical_height;
  value.refresh_millihertz = state.refresh_millihertz;
  value.scale_numerator = state.scale.numerator;
  value.scale_denominator = state.scale.denominator;
  value.transform = static_cast<gwipc_transform>(state.transform);
  value.color = color_record(state.color);
  return value;
}

std::uint32_t requested_item_count(const gwipc_output_state_query &query,
                                   const output::OutputLayout &layout) {
  std::size_t count = 0;
  if ((query.flags & GWIPC_OUTPUT_QUERY_DESCRIPTORS) != 0)
    count += layout.descriptors.size();
  if ((query.flags & GWIPC_OUTPUT_QUERY_MODES) != 0)
    for (const auto &[id, descriptor] : layout.descriptors) {
      (void)id;
      count += descriptor.modes.size();
    }
  if ((query.flags & GWIPC_OUTPUT_QUERY_LAYOUT) != 0)
    count += layout.states.size();
  return static_cast<std::uint32_t>(count);
}

gwipc_status append_descriptors(std::vector<OutputInventoryMessage> &messages,
                                const output::OutputLayout &layout) {
  for (const auto id : layout.output_order) {
    const auto value = descriptor_record(layout.descriptors.at(id));
    const auto status =
        append_contract(messages, GWIPC_MESSAGE_OUTPUT_DESCRIPTOR_UPSERT,
                        GWIPC_FLAG_SNAPSHOT_ITEM, 0, value,
                        gwipc_contract_encode_output_descriptor_upsert);
    if (status != GWIPC_STATUS_OK)
      return status;
  }
  return GWIPC_STATUS_OK;
}

gwipc_status append_modes(std::vector<OutputInventoryMessage> &messages,
                          const output::OutputLayout &layout) {
  for (const auto id : layout.output_order) {
    std::vector<const output::OutputMode *> modes;
    const auto &descriptor = layout.descriptors.at(id);
    modes.reserve(descriptor.modes.size());
    for (const auto &mode : descriptor.modes)
      modes.push_back(&mode);
    std::ranges::sort(modes, {}, [](const auto *mode) { return mode->id; });
    for (const auto *mode : modes) {
      const auto value = mode_record(*mode);
      const auto status = append_contract(
          messages, GWIPC_MESSAGE_OUTPUT_MODE_UPSERT, GWIPC_FLAG_SNAPSHOT_ITEM,
          0, value, gwipc_contract_encode_output_mode_upsert);
      if (status != GWIPC_STATUS_OK)
        return status;
    }
  }
  return GWIPC_STATUS_OK;
}

gwipc_status append_states(std::vector<OutputInventoryMessage> &messages,
                           const output::OutputLayout &layout) {
  for (const auto id : layout.output_order) {
    const auto value = state_record(layout.states.at(id));
    const auto status = append_contract(messages, GWIPC_MESSAGE_OUTPUT_UPSERT,
                                        GWIPC_FLAG_SNAPSHOT_ITEM, 0, value,
                                        gwipc_contract_encode_output_upsert);
    if (status != GWIPC_STATUS_OK)
      return status;
  }
  return GWIPC_STATUS_OK;
}

} // namespace

OutputInventoryPublication build_output_inventory_publication(
    const gwipc_output_state_query &query, const std::uint64_t query_sequence,
    const std::uint64_t snapshot_id, const output::OutputLayout &layout) {
  OutputInventoryPublication result;
  if (!valid_query(query, query_sequence, snapshot_id)) {
    result.status = GWIPC_STATUS_INVALID_ARGUMENT;
    return result;
  }
  result.layout_validation = output::validate_layout(layout);
  if (!result.layout_validation) {
    result.status = GWIPC_STATUS_INVALID_ARGUMENT;
    return result;
  }

  try {
    std::vector<OutputInventoryMessage> messages;
    const auto item_count = requested_item_count(query, layout);
    messages.reserve(static_cast<std::size_t>(item_count) + 3U);

    gwipc_snapshot_begin begin{};
    begin.struct_size = sizeof(begin);
    begin.snapshot_id = snapshot_id;
    begin.domain = GWIPC_SNAPSHOT_OUTPUTS;
    begin.generation = layout.generation;
    begin.expected_item_count = item_count;
    auto status = append_control(messages, GWIPC_MESSAGE_SNAPSHOT_BEGIN, begin,
                                 gwipc_control_encode_snapshot_begin);
    if (status == GWIPC_STATUS_OK &&
        (query.flags & GWIPC_OUTPUT_QUERY_DESCRIPTORS) != 0)
      status = append_descriptors(messages, layout);
    if (status == GWIPC_STATUS_OK &&
        (query.flags & GWIPC_OUTPUT_QUERY_MODES) != 0)
      status = append_modes(messages, layout);
    if (status == GWIPC_STATUS_OK &&
        (query.flags & GWIPC_OUTPUT_QUERY_LAYOUT) != 0)
      status = append_states(messages, layout);

    gwipc_snapshot_end end{};
    end.struct_size = sizeof(end);
    end.snapshot_id = snapshot_id;
    end.generation = layout.generation;
    end.actual_item_count = item_count;
    if (status == GWIPC_STATUS_OK)
      status = append_control(messages, GWIPC_MESSAGE_SNAPSHOT_END, end,
                              gwipc_control_encode_snapshot_end);

    gwipc_output_configuration_acknowledged acknowledged{};
    acknowledged.struct_size = sizeof(acknowledged);
    acknowledged.request_id = query.query_id;
    acknowledged.applied_generation = layout.generation;
    acknowledged.result = GWIPC_OUTPUT_CONFIGURATION_ACCEPTED;
    acknowledged.primary_output_id = layout.primary_output_id.value;
    acknowledged.root_logical_width = layout.root_logical_width;
    acknowledged.root_logical_height = layout.root_logical_height;
    acknowledged.enabled_output_count = layout.enabled_output_count;
    if (status == GWIPC_STATUS_OK)
      status = append_contract(
          messages, GWIPC_MESSAGE_OUTPUT_CONFIGURATION_ACKNOWLEDGED,
          GWIPC_FLAG_REPLY, query_sequence, acknowledged,
          gwipc_contract_encode_output_configuration_acknowledged);

    result.status = status;
    if (status == GWIPC_STATUS_OK)
      result.messages = std::move(messages);
  } catch (const std::bad_alloc &) {
    result.status = GWIPC_STATUS_OUT_OF_MEMORY;
  } catch (...) {
    result.status = GWIPC_STATUS_SYSTEM_ERROR;
  }
  return result;
}

} // namespace glasswyrm::compositor
