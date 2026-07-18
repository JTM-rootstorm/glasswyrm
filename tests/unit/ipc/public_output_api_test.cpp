#include <glasswyrm/ipc.h>

#include "ipc/internal.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <string_view>

namespace {

bool check(const bool condition, const char *message) {
  if (!condition)
    std::fprintf(stderr, "public output API: %s\n", message);
  return condition;
}

gwipc_decoded_contract *decode(const std::uint16_t type,
                               const gwipc_contract_payload *payload) {
  if (!payload)
    return nullptr;
  std::size_t size = 0;
  const auto *data = gwipc_contract_payload_data(payload, &size);
  if (size != 0 && !data)
    return nullptr;
  auto *message = new gwipc_message;
  message->type = type;
  if (size != 0)
    message->payload.assign(data, data + size);
  gwipc_decoded_contract *decoded = nullptr;
  const auto status = gwipc_contract_decode_message(message, &decoded);
  gwipc_message_destroy(message);
  return status == GWIPC_STATUS_OK ? decoded : nullptr;
}

bool descriptor_round_trip() {
  constexpr std::string_view name{"HEADLESS-1"};
  gwipc_output_descriptor_upsert value{};
  value.struct_size = sizeof(value);
  value.output_id = UINT64_C(0x8000000000000001);
  value.kind = GWIPC_OUTPUT_HEADLESS;
  value.capability_flags = GWIPC_OUTPUT_CAP_CONNECTED |
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

  gwipc_contract_payload *payload = nullptr;
  if (!check(gwipc_contract_encode_output_descriptor_upsert(&value, &payload) ==
                 GWIPC_STATUS_OK,
             "descriptor encoding failed"))
    return false;
  auto *decoded = decode(GWIPC_MESSAGE_OUTPUT_DESCRIPTOR_UPSERT, payload);
  gwipc_contract_payload_destroy(payload);
  const auto *output = gwipc_decoded_output_descriptor_upsert(decoded);
  const bool ok =
      check(output && output->output_id == value.output_id &&
                output->name_length == name.size() &&
                std::string_view(output->name, output->name_length) == name,
            "descriptor decoded view does not own the expected name") &&
      check(gwipc_decoded_output_mode_upsert(decoded) == nullptr,
            "wrong decoded accessor accepted a descriptor");
  gwipc_decoded_contract_destroy(decoded);

  value.reserved[0] = 1;
  payload = reinterpret_cast<gwipc_contract_payload *>(1);
  return ok &&
         check(gwipc_contract_encode_output_descriptor_upsert(
                   &value, &payload) == GWIPC_STATUS_INVALID_ARGUMENT &&
                   payload == reinterpret_cast<gwipc_contract_payload *>(1),
               "reserved descriptor input was accepted");
}

bool remaining_contracts() {
  bool ok = true;
  gwipc_contract_payload *payload = nullptr;

  gwipc_output_mode_upsert mode{};
  mode.struct_size = sizeof(mode);
  mode.output_id = 1;
  mode.mode_id = 2;
  mode.physical_width = 800;
  mode.physical_height = 600;
  mode.refresh_millihertz = 60'000;
  mode.preferred = 1;
  mode.current = 1;
  if (!check(gwipc_contract_encode_output_mode_upsert(&mode, &payload) ==
                 GWIPC_STATUS_OK,
             "mode encoding failed"))
    return false;
  auto *decoded = decode(GWIPC_MESSAGE_OUTPUT_MODE_UPSERT, payload);
  gwipc_contract_payload_destroy(payload);
  payload = nullptr;
  const auto *decoded_mode = gwipc_decoded_output_mode_upsert(decoded);
  ok &= check(decoded_mode && decoded_mode->mode_id == mode.mode_id &&
                  decoded_mode->current == 1,
              "mode round trip failed");
  gwipc_decoded_contract_destroy(decoded);
  mode.preferred = 2;
  ok &= check(gwipc_contract_encode_output_mode_upsert(&mode, &payload) ==
                  GWIPC_STATUS_INVALID_ARGUMENT,
              "noncanonical mode boolean was accepted");

  const std::array<std::uint64_t, 2> output_ids{1, 2};
  gwipc_surface_output_state surface{};
  surface.struct_size = sizeof(surface);
  surface.surface_id = 10;
  surface.primary_output_id = 1;
  surface.output_ids = output_ids.data();
  surface.output_count = output_ids.size();
  surface.preferred_scale_numerator = 5;
  surface.preferred_scale_denominator = 4;
  surface.client_buffer_scale = 2;
  surface.scale_mode = GWIPC_SURFACE_SCALE_SCALED_PIXMAP;
  surface.layout_generation = 7;
  if (!check(gwipc_contract_encode_surface_output_state(&surface, &payload) ==
                 GWIPC_STATUS_OK,
             "surface output-state encoding failed"))
    return false;
  decoded = decode(GWIPC_MESSAGE_SURFACE_OUTPUT_STATE, payload);
  gwipc_contract_payload_destroy(payload);
  payload = nullptr;
  const auto *decoded_surface = gwipc_decoded_surface_output_state(decoded);
  ok &= check(decoded_surface && decoded_surface->output_count == 2 &&
                  decoded_surface->output_ids[0] == 1 &&
                  decoded_surface->output_ids[1] == 2,
              "surface membership decoded view is invalid");
  gwipc_decoded_contract_destroy(decoded);
  surface.output_count = GWIPC_MAXIMUM_OUTPUTS + 1U;
  ok &= check(gwipc_contract_encode_surface_output_state(&surface, &payload) ==
                  GWIPC_STATUS_INVALID_ARGUMENT,
              "oversized output membership was accepted");

  gwipc_policy_output_upsert policy{};
  policy.struct_size = sizeof(policy);
  policy.output_id = 1;
  policy.logical_width = 640;
  policy.logical_height = 480;
  policy.work_width = 640;
  policy.work_height = 460;
  policy.scale_numerator = 5;
  policy.scale_denominator = 4;
  policy.transform = GWIPC_TRANSFORM_NORMAL;
  policy.enabled = 1;
  policy.primary = 1;
  if (!check(gwipc_contract_encode_policy_output_upsert(&policy, &payload) ==
                 GWIPC_STATUS_OK,
             "policy output encoding failed"))
    return false;
  decoded = decode(GWIPC_MESSAGE_POLICY_OUTPUT_UPSERT, payload);
  gwipc_contract_payload_destroy(payload);
  payload = nullptr;
  const auto *decoded_policy = gwipc_decoded_policy_output_upsert(decoded);
  ok &= check(decoded_policy && decoded_policy->output_id == policy.output_id &&
                  decoded_policy->logical_width == policy.logical_width &&
                  decoded_policy->work_height == policy.work_height &&
                  decoded_policy->scale_numerator == policy.scale_numerator &&
                  decoded_policy->enabled == 1 && decoded_policy->primary == 1,
              "policy output round trip failed");
  gwipc_decoded_contract_destroy(decoded);
  policy.enabled = 2;
  ok &= check(gwipc_contract_encode_policy_output_upsert(&policy, &payload) ==
                  GWIPC_STATUS_INVALID_ARGUMENT,
              "noncanonical policy boolean was accepted");

  gwipc_policy_window_output_hint hint{};
  hint.struct_size = sizeof(hint);
  hint.window_id = 35;
  hint.previous_output_id = 1;
  hint.preferred_output_id = 2;
  if (!check(gwipc_contract_encode_policy_window_output_hint(&hint, &payload) ==
                 GWIPC_STATUS_OK,
             "policy window hint encoding failed"))
    return false;
  decoded = decode(GWIPC_MESSAGE_POLICY_WINDOW_OUTPUT_HINT, payload);
  gwipc_contract_payload_destroy(payload);
  payload = nullptr;
  const auto *decoded_hint = gwipc_decoded_policy_window_output_hint(decoded);
  ok &= check(decoded_hint && decoded_hint->window_id == hint.window_id &&
                  decoded_hint->previous_output_id == hint.previous_output_id &&
                  decoded_hint->preferred_output_id == hint.preferred_output_id,
              "policy window hint round trip failed");
  gwipc_decoded_contract_destroy(decoded);

  gwipc_output_state_query query{};
  query.struct_size = sizeof(query);
  query.query_id = 100;
  query.flags = GWIPC_OUTPUT_QUERY_DESCRIPTORS | GWIPC_OUTPUT_QUERY_LAYOUT;
  if (!check(gwipc_contract_encode_output_state_query(&query, &payload) ==
                 GWIPC_STATUS_OK,
             "output query encoding failed"))
    return false;
  decoded = decode(GWIPC_MESSAGE_OUTPUT_STATE_QUERY, payload);
  gwipc_contract_payload_destroy(payload);
  payload = nullptr;
  const auto *decoded_query = gwipc_decoded_output_state_query(decoded);
  ok &= check(decoded_query && decoded_query->query_id == query.query_id &&
                  decoded_query->flags == query.flags,
              "output query round trip failed");
  gwipc_decoded_contract_destroy(decoded);
  query.flags = UINT32_C(0x80000000);
  ok &= check(gwipc_contract_encode_output_state_query(&query, &payload) ==
                  GWIPC_STATUS_INVALID_ARGUMENT,
              "unknown query flags were accepted");

  gwipc_output_configuration_commit commit{};
  commit.struct_size = sizeof(commit);
  commit.configuration_id = 101;
  commit.base_generation = 7;
  commit.primary_output_id = 1;
  if (!check(gwipc_contract_encode_output_configuration_commit(
                 &commit, &payload) == GWIPC_STATUS_OK,
             "configuration commit encoding failed"))
    return false;
  decoded = decode(GWIPC_MESSAGE_OUTPUT_CONFIGURATION_COMMIT, payload);
  gwipc_contract_payload_destroy(payload);
  payload = nullptr;
  const auto *decoded_commit =
      gwipc_decoded_output_configuration_commit(decoded);
  ok &= check(decoded_commit &&
                  decoded_commit->configuration_id == commit.configuration_id &&
                  decoded_commit->base_generation == commit.base_generation &&
                  decoded_commit->primary_output_id == commit.primary_output_id,
              "configuration commit round trip failed");
  gwipc_decoded_contract_destroy(decoded);
  commit.base_generation = 0;
  ok &= check(gwipc_contract_encode_output_configuration_commit(
                  &commit, &payload) == GWIPC_STATUS_INVALID_ARGUMENT,
              "zero base generation was accepted");

  gwipc_output_configuration_acknowledged acknowledged{};
  acknowledged.struct_size = sizeof(acknowledged);
  acknowledged.request_id = 101;
  acknowledged.applied_generation = 8;
  acknowledged.result = GWIPC_OUTPUT_CONFIGURATION_ACCEPTED;
  acknowledged.primary_output_id = 1;
  acknowledged.root_logical_width = 1280;
  acknowledged.root_logical_height = 480;
  acknowledged.enabled_output_count = 2;
  if (!check(gwipc_contract_encode_output_configuration_acknowledged(
                 &acknowledged, &payload) == GWIPC_STATUS_OK,
             "configuration acknowledgement encoding failed"))
    return false;
  decoded = decode(GWIPC_MESSAGE_OUTPUT_CONFIGURATION_ACKNOWLEDGED, payload);
  gwipc_contract_payload_destroy(payload);
  payload = nullptr;
  const auto *decoded_acknowledged =
      gwipc_decoded_output_configuration_acknowledged(decoded);
  ok &= check(decoded_acknowledged &&
                  decoded_acknowledged->request_id == acknowledged.request_id &&
                  decoded_acknowledged->applied_generation ==
                      acknowledged.applied_generation &&
                  decoded_acknowledged->result == acknowledged.result &&
                  decoded_acknowledged->primary_output_id ==
                      acknowledged.primary_output_id &&
                  decoded_acknowledged->root_logical_width ==
                      acknowledged.root_logical_width &&
                  decoded_acknowledged->root_logical_height ==
                      acknowledged.root_logical_height &&
                  decoded_acknowledged->enabled_output_count ==
                      acknowledged.enabled_output_count,
              "configuration acknowledgement round trip failed");
  gwipc_decoded_contract_destroy(decoded);
  acknowledged.reserved32 = 1;
  ok &= check(gwipc_contract_encode_output_configuration_acknowledged(
                  &acknowledged, &payload) == GWIPC_STATUS_INVALID_ARGUMENT,
              "reserved acknowledgement input was accepted");
  return ok;
}

} // namespace

int main() {
  const auto api = gwipc_get_api_version();
  const bool ok = check(api.major == 0 && api.minor == 8 && api.patch == 0,
                        "API version is not 0.8.0") &&
                  descriptor_round_trip() && remaining_contracts();
  return ok ? 0 : 1;
}
