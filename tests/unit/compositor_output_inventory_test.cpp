#include "glasswyrmd/compositor_output_inventory.hpp"

#include "helpers/test_support.hpp"
#include "ipc/internal.hpp"

#include <glasswyrm/ipc.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

using glasswyrm::output::OutputId;
using glasswyrm::server::CompositorInventoryFailure;
using glasswyrm::server::CompositorInventoryQuerySink;
using glasswyrm::server::CompositorInventoryState;
using glasswyrm::server::CompositorOutputInventory;
using gw::test::require;

constexpr OutputId kLeft{UINT64_C(0x8000000000000001)};
constexpr OutputId kRight{UINT64_C(0x8000000000000002)};
constexpr std::uint64_t kLeftMode = UINT64_C(0x4000000000000011);
constexpr std::uint64_t kRightMode = UINT64_C(0x4000000000000021);
constexpr std::uint64_t kGeneration = 7;
constexpr std::uint64_t kSnapshotId = 19;
constexpr std::uint64_t kQueryId = 41;

struct MessageDelete {
  void operator()(gwipc_message *value) const { gwipc_message_destroy(value); }
};
struct ContractPayloadDelete {
  void operator()(gwipc_contract_payload *value) const {
    gwipc_contract_payload_destroy(value);
  }
};
struct ControlPayloadDelete {
  void operator()(gwipc_control_payload *value) const {
    gwipc_control_payload_destroy(value);
  }
};
using Message = std::unique_ptr<gwipc_message, MessageDelete>;

template <typename Value>
Message contract_message(
    const std::uint16_t type, const std::uint32_t flags,
    const std::uint64_t sequence, const std::uint64_t reply_to,
    const Value &value,
    gwipc_status (*encode)(const Value *, gwipc_contract_payload **)) {
  gwipc_contract_payload *raw = nullptr;
  require(encode(&value, &raw) == GWIPC_STATUS_OK,
          "test contract payload encodes");
  std::unique_ptr<gwipc_contract_payload, ContractPayloadDelete> payload(raw);
  std::size_t size = 0;
  const auto *data = gwipc_contract_payload_data(payload.get(), &size);
  auto message = Message(new gwipc_message);
  message->type = type;
  message->flags = flags;
  message->sequence = sequence;
  message->reply_to = reply_to;
  message->payload.assign(data, data + size);
  return message;
}

template <typename Value>
Message control_message(
    const std::uint16_t type, const std::uint64_t sequence,
    const Value &value,
    gwipc_status (*encode)(const Value *, gwipc_control_payload **)) {
  gwipc_control_payload *raw = nullptr;
  require(encode(&value, &raw) == GWIPC_STATUS_OK,
          "test control payload encodes");
  std::unique_ptr<gwipc_control_payload, ControlPayloadDelete> payload(raw);
  std::size_t size = 0;
  const auto *data = gwipc_control_payload_data(payload.get(), &size);
  auto message = Message(new gwipc_message);
  message->type = type;
  message->sequence = sequence;
  message->payload.assign(data, data + size);
  return message;
}

class QuerySink final : public CompositorInventoryQuerySink {
public:
  gwipc_status enqueue(const gwipc_outgoing_message &message,
                       std::uint64_t &sequence) override {
    type = message.type;
    flags = message.flags;
    payload.assign(message.payload, message.payload + message.payload_size);
    sequence = assigned_sequence;
    return status;
  }

  gwipc_status status{GWIPC_STATUS_OK};
  std::uint64_t assigned_sequence{37};
  std::uint16_t type{};
  std::uint32_t flags{};
  std::vector<std::uint8_t> payload;
};

gwipc_output_descriptor_upsert descriptor(const OutputId id,
                                           const std::string_view name) {
  gwipc_output_descriptor_upsert value{};
  value.struct_size = sizeof(value);
  value.output_id = id.value;
  value.kind = GWIPC_OUTPUT_HEADLESS;
  value.capability_flags =
      GWIPC_OUTPUT_CAP_CONNECTED |
      GWIPC_OUTPUT_CAP_ARBITRARY_HEADLESS_MODE |
      GWIPC_OUTPUT_CAP_SCALE_CONFIGURABLE |
      GWIPC_OUTPUT_CAP_TRANSFORM_CONFIGURABLE |
      GWIPC_OUTPUT_CAP_PRIMARY_ELIGIBLE;
  value.name = name.data();
  value.name_length = name.size();
  value.supported_transform_mask = UINT32_C(0xff);
  value.minimum_scale_numerator = 1;
  value.minimum_scale_denominator = 1;
  value.maximum_scale_numerator = 4;
  value.maximum_scale_denominator = 1;
  value.maximum_scale_denominator_value = 120;
  value.maximum_physical_width = 4096;
  value.maximum_physical_height = 4096;
  return value;
}

gwipc_output_mode_upsert mode(const OutputId output_id,
                              const std::uint64_t mode_id,
                              const std::uint32_t width,
                              const std::uint32_t height,
                              const std::uint32_t refresh) {
  gwipc_output_mode_upsert value{};
  value.struct_size = sizeof(value);
  value.output_id = output_id.value;
  value.mode_id = mode_id;
  value.physical_width = width;
  value.physical_height = height;
  value.refresh_millihertz = refresh;
  value.preferred = 1;
  value.current = 1;
  return value;
}

gwipc_output_upsert state(const OutputId output_id, const std::int32_t x,
                          const std::uint32_t width,
                          const std::uint32_t height,
                          const std::uint32_t refresh) {
  gwipc_output_upsert value{};
  value.struct_size = sizeof(value);
  value.output_id = output_id.value;
  value.enabled = 1;
  value.logical_x = x;
  value.logical_width = width;
  value.logical_height = height;
  value.physical_pixel_width = width;
  value.physical_pixel_height = height;
  value.refresh_millihertz = refresh;
  value.scale_numerator = 1;
  value.scale_denominator = 1;
  value.transform = GWIPC_TRANSFORM_NORMAL;
  value.color.color_space = GWIPC_SDR_COLOR_SPACE_SRGB;
  value.color.transfer_function = GWIPC_TRANSFER_FUNCTION_SRGB;
  value.color.primaries = GWIPC_COLOR_PRIMARIES_SRGB;
  return value;
}

gwipc_output_configuration_acknowledged acknowledgement() {
  gwipc_output_configuration_acknowledged value{};
  value.struct_size = sizeof(value);
  value.request_id = kQueryId;
  value.applied_generation = kGeneration;
  value.result = GWIPC_OUTPUT_CONFIGURATION_ACCEPTED;
  value.primary_output_id = kLeft.value;
  value.root_logical_width = 1440;
  value.root_logical_height = 600;
  value.enabled_output_count = 2;
  return value;
}

void require_consume(CompositorOutputInventory &inventory, Message message,
                     std::string &error) {
  require(inventory.consume(message.get(), error),
          "valid inventory record is consumed");
}

std::uint64_t start_query(CompositorOutputInventory &inventory,
                          QuerySink &sink, std::string &error) {
  require(inventory.start(sink, kQueryId, error),
          "inventory query starts");
  const auto sequence = inventory.query_sequence();
  auto query_message = Message(new gwipc_message);
  query_message->type = sink.type;
  query_message->payload = sink.payload;
  gwipc_decoded_contract *raw = nullptr;
  require(gwipc_contract_decode_message(query_message.get(), &raw) ==
              GWIPC_STATUS_OK,
          "queued query decodes");
  const auto *query = gwipc_decoded_output_state_query(raw);
  require(sequence == sink.assigned_sequence &&
              inventory.query_id() == kQueryId &&
              sink.type == GWIPC_MESSAGE_OUTPUT_STATE_QUERY &&
              sink.flags == GWIPC_FLAG_ACK_REQUIRED && query &&
              query->query_id == kQueryId &&
              query->flags == (GWIPC_OUTPUT_QUERY_DESCRIPTORS |
                               GWIPC_OUTPUT_QUERY_MODES |
                               GWIPC_OUTPUT_QUERY_LAYOUT),
          "query records its exact sequence and query identity");
  gwipc_decoded_contract_destroy(raw);
  return sequence;
}

void consume_snapshot(CompositorOutputInventory &inventory,
                      std::string &error) {
  gwipc_snapshot_begin begin{};
  begin.struct_size = sizeof(begin);
  begin.snapshot_id = kSnapshotId;
  begin.domain = GWIPC_SNAPSHOT_OUTPUTS;
  begin.generation = kGeneration;
  begin.expected_item_count = 6;
  require_consume(inventory,
                  control_message(GWIPC_MESSAGE_SNAPSHOT_BEGIN, 10, begin,
                                  gwipc_control_encode_snapshot_begin),
                  error);

  const std::string_view left_name = "LEFT";
  const std::string_view right_name = "RIGHT";
  const auto left_descriptor = descriptor(kLeft, left_name);
  const auto right_descriptor = descriptor(kRight, right_name);
  require_consume(
      inventory,
      contract_message(GWIPC_MESSAGE_OUTPUT_DESCRIPTOR_UPSERT,
                       GWIPC_FLAG_SNAPSHOT_ITEM, 11, 0, left_descriptor,
                       gwipc_contract_encode_output_descriptor_upsert),
      error);
  require_consume(
      inventory,
      contract_message(GWIPC_MESSAGE_OUTPUT_DESCRIPTOR_UPSERT,
                       GWIPC_FLAG_SNAPSHOT_ITEM, 12, 0, right_descriptor,
                       gwipc_contract_encode_output_descriptor_upsert),
      error);

  const auto left_mode = mode(kLeft, kLeftMode, 800, 600, 60'000);
  const auto right_mode = mode(kRight, kRightMode, 640, 480, 75'000);
  require_consume(inventory,
                  contract_message(GWIPC_MESSAGE_OUTPUT_MODE_UPSERT,
                                   GWIPC_FLAG_SNAPSHOT_ITEM, 13, 0, left_mode,
                                   gwipc_contract_encode_output_mode_upsert),
                  error);
  require_consume(inventory,
                  contract_message(GWIPC_MESSAGE_OUTPUT_MODE_UPSERT,
                                   GWIPC_FLAG_SNAPSHOT_ITEM, 14, 0, right_mode,
                                   gwipc_contract_encode_output_mode_upsert),
                  error);

  const auto left_state = state(kLeft, 0, 800, 600, 60'000);
  const auto right_state = state(kRight, 800, 640, 480, 75'000);
  require_consume(inventory,
                  contract_message(GWIPC_MESSAGE_OUTPUT_UPSERT,
                                   GWIPC_FLAG_SNAPSHOT_ITEM, 15, 0, left_state,
                                   gwipc_contract_encode_output_upsert),
                  error);
  require_consume(inventory,
                  contract_message(GWIPC_MESSAGE_OUTPUT_UPSERT,
                                   GWIPC_FLAG_SNAPSHOT_ITEM, 16, 0, right_state,
                                   gwipc_contract_encode_output_upsert),
                  error);

  gwipc_snapshot_end end{};
  end.struct_size = sizeof(end);
  end.snapshot_id = kSnapshotId;
  end.generation = kGeneration;
  end.actual_item_count = 6;
  require_consume(inventory,
                  control_message(GWIPC_MESSAGE_SNAPSHOT_END, 17, end,
                                  gwipc_control_encode_snapshot_end),
                  error);
}

void test_complete_inventory() {
  QuerySink sink;
  CompositorOutputInventory inventory;
  std::string error;
  const auto sequence = start_query(inventory, sink, error);
  consume_snapshot(inventory, error);
  const auto ack = acknowledgement();
  require_consume(
      inventory,
      contract_message(GWIPC_MESSAGE_OUTPUT_CONFIGURATION_ACKNOWLEDGED,
                       GWIPC_FLAG_REPLY, 18, sequence, ack,
                       gwipc_contract_encode_output_configuration_acknowledged),
      error);

  const auto *layout = inventory.layout();
  require(inventory.state() == CompositorInventoryState::Complete && layout &&
              layout->generation == kGeneration &&
              layout->primary_output_id == kLeft &&
              layout->root_logical_width == 1440 &&
              layout->root_logical_height == 600 &&
              layout->enabled_output_count == 2 &&
              layout->output_order == std::vector<OutputId>{kLeft, kRight} &&
              layout->descriptors.at(kLeft).name == "LEFT" &&
              layout->descriptors.at(kRight).modes.front().id.value ==
                  kRightMode &&
              layout->states.at(kLeft).mode_id.value == kLeftMode &&
              layout->states.at(kLeft).primary,
          "completed inventory exposes a validated shared-model layout");
}

void test_rejects_out_of_order_mode() {
  QuerySink sink;
  CompositorOutputInventory inventory;
  std::string error;
  (void)start_query(inventory, sink, error);
  gwipc_snapshot_begin begin{};
  begin.struct_size = sizeof(begin);
  begin.snapshot_id = kSnapshotId;
  begin.domain = GWIPC_SNAPSHOT_OUTPUTS;
  begin.generation = kGeneration;
  begin.expected_item_count = 1;
  require_consume(inventory,
                  control_message(GWIPC_MESSAGE_SNAPSHOT_BEGIN, 1, begin,
                                  gwipc_control_encode_snapshot_begin),
                  error);
  const auto value = mode(kLeft, kLeftMode, 800, 600, 60'000);
  auto message = contract_message(
      GWIPC_MESSAGE_OUTPUT_MODE_UPSERT, GWIPC_FLAG_SNAPSHOT_ITEM, 2, 0, value,
      gwipc_contract_encode_output_mode_upsert);
  require(!inventory.consume(message.get(), error) &&
              inventory.failure() == CompositorInventoryFailure::InvalidOrder &&
              inventory.layout() == nullptr,
          "mode before its descriptor fails deterministically");
}

void test_rejects_wrong_correlation() {
  QuerySink sink;
  CompositorOutputInventory inventory;
  std::string error;
  const auto sequence = start_query(inventory, sink, error);
  consume_snapshot(inventory, error);
  const auto ack = acknowledgement();
  auto message = contract_message(
      GWIPC_MESSAGE_OUTPUT_CONFIGURATION_ACKNOWLEDGED, GWIPC_FLAG_REPLY, 18,
      sequence + 1, ack,
      gwipc_contract_encode_output_configuration_acknowledged);
  require(!inventory.consume(message.get(), error) &&
              inventory.failure() ==
                  CompositorInventoryFailure::CorrelationMismatch &&
              error == "output inventory acknowledgement was not correlated",
          "reply_to must match the exact query sequence");
}

void test_rejects_invalid_shared_layout() {
  QuerySink sink;
  CompositorOutputInventory inventory;
  std::string error;
  const auto sequence = start_query(inventory, sink, error);
  consume_snapshot(inventory, error);
  auto ack = acknowledgement();
  ack.root_logical_width = 1439;
  auto message = contract_message(
      GWIPC_MESSAGE_OUTPUT_CONFIGURATION_ACKNOWLEDGED, GWIPC_FLAG_REPLY, 18,
      sequence, ack,
      gwipc_contract_encode_output_configuration_acknowledged);
  require(!inventory.consume(message.get(), error) &&
              inventory.failure() == CompositorInventoryFailure::InvalidLayout,
          "acknowledged metadata must pass shared output-model validation");
}

} // namespace

int main() {
  test_complete_inventory();
  test_rejects_out_of_order_mode();
  test_rejects_wrong_correlation();
  test_rejects_invalid_shared_layout();
  return 0;
}
