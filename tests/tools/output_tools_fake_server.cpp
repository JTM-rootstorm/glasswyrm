#include <glasswyrm/ipc.h>

#include <array>
#include <cstdio>
#include <map>
#include <memory>
#include <poll.h>
#include <string>
#include <vector>

namespace {

template <typename Type, void (*Destroy)(Type *)>
using Owned = std::unique_ptr<Type, decltype(Destroy)>;

template <typename Value>
bool send_contract(gwipc_connection *connection, const std::uint16_t type,
                   const std::uint32_t flags, const std::uint64_t reply_to,
                   const Value &value,
                   gwipc_status (*encode)(const Value *,
                                          gwipc_contract_payload **)) {
  gwipc_contract_payload *raw = nullptr;
  const auto encode_status = encode(&value, &raw);
  if (encode_status != GWIPC_STATUS_OK) {
    std::fprintf(stderr, "fake output server could not encode type %u: %s\n",
                 type, gwipc_status_string(encode_status));
    return false;
  }
  Owned<gwipc_contract_payload, gwipc_contract_payload_destroy> payload(
      raw, gwipc_contract_payload_destroy);
  std::size_t size = 0;
  const auto *data = gwipc_contract_payload_data(payload.get(), &size);
  gwipc_outgoing_message message{};
  message.struct_size = sizeof(message);
  message.type = type;
  message.flags = flags;
  message.reply_to = reply_to;
  message.payload = data;
  message.payload_size = size;
  const auto enqueue_status = gwipc_connection_enqueue(connection, &message);
  if (enqueue_status != GWIPC_STATUS_OK)
    std::fprintf(stderr, "fake output server could not queue type %u: %s\n",
                 type, gwipc_status_string(enqueue_status));
  return enqueue_status == GWIPC_STATUS_OK;
}

template <typename Value>
bool send_control(gwipc_connection *connection, const std::uint16_t type,
                  const Value &value,
                  gwipc_status (*encode)(const Value *,
                                         gwipc_control_payload **)) {
  gwipc_control_payload *raw = nullptr;
  const auto encode_status = encode(&value, &raw);
  if (encode_status != GWIPC_STATUS_OK) {
    std::fprintf(stderr, "fake output server could not encode type %u: %s\n",
                 type, gwipc_status_string(encode_status));
    return false;
  }
  Owned<gwipc_control_payload, gwipc_control_payload_destroy> payload(
      raw, gwipc_control_payload_destroy);
  std::size_t size = 0;
  const auto *data = gwipc_control_payload_data(payload.get(), &size);
  gwipc_outgoing_message message{};
  message.struct_size = sizeof(message);
  message.type = type;
  message.payload = data;
  message.payload_size = size;
  const auto enqueue_status = gwipc_connection_enqueue(connection, &message);
  if (enqueue_status != GWIPC_STATUS_OK)
    std::fprintf(stderr, "fake output server could not queue type %u: %s\n",
                 type, gwipc_status_string(enqueue_status));
  return enqueue_status == GWIPC_STATUS_OK;
}

enum class ServerMode {
  Query,
  Commit,
  VrrQuery,
  VrrCommit,
  DuplicateVrr,
};

ServerMode parse_mode(const std::string &value) {
  if (value == "commit")
    return ServerMode::Commit;
  if (value == "vrr-query")
    return ServerMode::VrrQuery;
  if (value == "vrr-commit")
    return ServerMode::VrrCommit;
  if (value == "duplicate-vrr")
    return ServerMode::DuplicateVrr;
  return ServerMode::Query;
}

bool offers_vrr(const ServerMode mode) {
  return mode == ServerMode::VrrQuery || mode == ServerMode::VrrCommit ||
         mode == ServerMode::DuplicateVrr;
}

bool pump(gwipc_connection *connection) {
  pollfd descriptor{gwipc_connection_fd(connection),
                    gwipc_connection_wanted_poll_events(connection), 0};
  if (::poll(&descriptor, 1, 10) < 0)
    return false;
  if (descriptor.revents == 0)
    return true;
  const auto status =
      gwipc_connection_process_poll_events(connection, descriptor.revents);
  return status == GWIPC_STATUS_OK || status == GWIPC_STATUS_WOULD_BLOCK;
}

Owned<gwipc_message, gwipc_message_destroy>
receive(gwipc_connection *connection) {
  for (unsigned attempt = 0; attempt < 500; ++attempt) {
    if (!pump(connection))
      return {nullptr, gwipc_message_destroy};
    gwipc_message *raw = nullptr;
    const auto status = gwipc_connection_receive(connection, &raw);
    if (status == GWIPC_STATUS_OK)
      return {raw, gwipc_message_destroy};
    if (status != GWIPC_STATUS_WOULD_BLOCK)
      return {nullptr, gwipc_message_destroy};
  }
  return {nullptr, gwipc_message_destroy};
}

gwipc_output_descriptor_upsert descriptor(const std::uint64_t id,
                                          const char *name) {
  gwipc_output_descriptor_upsert value{};
  value.struct_size = sizeof(value);
  value.output_id = id;
  value.kind = GWIPC_OUTPUT_HEADLESS;
  value.capability_flags = GWIPC_OUTPUT_CAP_CONNECTED |
                           GWIPC_OUTPUT_CAP_ARBITRARY_HEADLESS_MODE |
                           GWIPC_OUTPUT_CAP_SCALE_CONFIGURABLE |
                           GWIPC_OUTPUT_CAP_TRANSFORM_CONFIGURABLE |
                           GWIPC_OUTPUT_CAP_PRIMARY_ELIGIBLE;
  value.name = name;
  value.name_length = std::char_traits<char>::length(name);
  value.supported_transform_mask = 0xff;
  value.minimum_scale_numerator = 1;
  value.minimum_scale_denominator = 1;
  value.maximum_scale_numerator = 4;
  value.maximum_scale_denominator = 1;
  value.maximum_scale_denominator_value = 120;
  value.maximum_physical_width = 4096;
  value.maximum_physical_height = 4096;
  return value;
}

gwipc_output_mode_upsert mode(const std::uint64_t output_id,
                              const std::uint64_t mode_id) {
  gwipc_output_mode_upsert value{};
  value.struct_size = sizeof(value);
  value.output_id = output_id;
  value.mode_id = mode_id;
  value.physical_width = 640;
  value.physical_height = 480;
  value.refresh_millihertz = 60000;
  value.preferred = value.current = 1;
  return value;
}

gwipc_output_upsert output(const std::uint64_t id, const std::int32_t x) {
  gwipc_output_upsert value{};
  value.struct_size = sizeof(value);
  value.output_id = id;
  value.enabled = 1;
  value.logical_x = x;
  value.logical_width = 640;
  value.logical_height = 480;
  value.physical_pixel_width = 640;
  value.physical_pixel_height = 480;
  value.refresh_millihertz = 60000;
  value.scale_numerator = value.scale_denominator = 1;
  value.transform = GWIPC_TRANSFORM_NORMAL;
  value.color = {GWIPC_SDR_COLOR_SPACE_SRGB,
                 GWIPC_TRANSFER_FUNCTION_SRGB,
                 GWIPC_COLOR_PRIMARIES_SRGB,
                 0,
                 0,
                 0,
                 0};
  return value;
}

bool send_window(gwipc_connection *connection, const std::uint64_t generation) {
  gwipc_surface_upsert surface{};
  surface.struct_size = sizeof(surface);
  surface.surface_id = (UINT64_C(1) << 32U) | 41U;
  surface.x11_window_id = 41;
  surface.output_id = 12;
  surface.logical_x = 600;
  surface.logical_y = 40;
  surface.logical_width = 100;
  surface.logical_height = 80;
  surface.visible = 1;
  surface.opacity = GWIPC_OPACITY_ONE;
  surface.scale_numerator = 2;
  surface.scale_denominator = 1;
  surface.color = {GWIPC_SDR_COLOR_SPACE_SRGB,
                   GWIPC_TRANSFER_FUNCTION_SRGB,
                   GWIPC_COLOR_PRIMARIES_SRGB,
                   0,
                   0,
                   0,
                   0};
  surface.presentation_flags = GWIPC_SURFACE_PRESENTATION_METADATA_ONLY;
  gwipc_surface_policy_upsert policy{};
  policy.struct_size = sizeof(policy);
  policy.surface_id = surface.surface_id;
  policy.x11_window_id = 41;
  policy.workspace_id = 1;
  policy.window_type = GWIPC_POLICY_WINDOW_NORMAL;
  policy.applied_state = GWIPC_POLICY_APPLIED_FULLSCREEN;
  policy.focused = policy.managed = 1;
  const std::array<std::uint64_t, 2> memberships{11, 12};
  gwipc_surface_output_state state{};
  state.struct_size = sizeof(state);
  state.surface_id = surface.surface_id;
  state.primary_output_id = 12;
  state.output_ids = memberships.data();
  state.output_count = memberships.size();
  state.preferred_scale_numerator = 5;
  state.preferred_scale_denominator = 4;
  state.client_buffer_scale = 2;
  state.scale_mode = GWIPC_SURFACE_SCALE_SCALED_PIXMAP;
  state.layout_generation = generation;
  return send_contract(connection, GWIPC_MESSAGE_SURFACE_UPSERT,
                       GWIPC_FLAG_SNAPSHOT_ITEM, 0, surface,
                       gwipc_contract_encode_surface_upsert) &&
         send_contract(connection, GWIPC_MESSAGE_SURFACE_POLICY_UPSERT,
                       GWIPC_FLAG_SNAPSHOT_ITEM, 0, policy,
                       gwipc_contract_encode_surface_policy_upsert) &&
         send_contract(connection, GWIPC_MESSAGE_SURFACE_OUTPUT_STATE,
                       GWIPC_FLAG_SNAPSHOT_ITEM, 0, state,
                       gwipc_contract_encode_surface_output_state);
}

gwipc_output_vrr_capability_upsert
vrr_capability(const std::uint64_t output_id) {
  gwipc_output_vrr_capability_upsert value{};
  value.struct_size = sizeof(value);
  value.output_id = output_id;
  if (output_id == 11) {
    value.reason_flags = GWIPC_VRR_REASON_OUTPUT_NOT_DRM |
                         GWIPC_VRR_REASON_OUTPUT_NOT_VRR_CAPABLE |
                         GWIPC_VRR_REASON_VRR_PROPERTY_MISSING;
  } else {
    value.kms_controllable = 1;
    value.simulated = 1;
    value.range_available = 1;
    value.minimum_refresh_millihertz = 40'000;
    value.maximum_refresh_millihertz = 144'000;
    value.reason_flags = GWIPC_VRR_REASON_SIMULATED_HEADLESS;
  }
  return value;
}

gwipc_output_vrr_policy_upsert
vrr_policy(const std::uint64_t output_id,
           const gwipc_vrr_policy_mode right_policy) {
  gwipc_output_vrr_policy_upsert value{};
  value.struct_size = sizeof(value);
  value.output_id = output_id;
  value.mode = output_id == 12 ? right_policy : GWIPC_VRR_POLICY_OFF;
  return value;
}

gwipc_output_vrr_state_upsert
vrr_state(const std::uint64_t output_id,
          const gwipc_vrr_policy_mode right_policy,
          const std::uint64_t generation) {
  gwipc_output_vrr_state_upsert value{};
  value.struct_size = sizeof(value);
  value.output_id = output_id;
  value.requested_mode = output_id == 12 ? right_policy : GWIPC_VRR_POLICY_OFF;
  value.session_active = 1;
  value.state_generation = generation;
  value.transition_serial = generation + 6;
  if (output_id == 11) {
    value.decision = GWIPC_VRR_DECISION_UNSUPPORTED;
    value.reason_flags = GWIPC_VRR_REASON_OUTPUT_NOT_DRM |
                         GWIPC_VRR_REASON_OUTPUT_NOT_VRR_CAPABLE |
                         GWIPC_VRR_REASON_VRR_PROPERTY_MISSING;
  } else {
    value.decision = GWIPC_VRR_DECISION_DISABLED;
    value.reason_flags =
        GWIPC_VRR_REASON_SIMULATED_HEADLESS |
        (right_policy == GWIPC_VRR_POLICY_OFF ? GWIPC_VRR_REASON_POLICY_OFF
                                              : GWIPC_VRR_REASON_NO_CANDIDATE);
    value.last_commit_id = 70 + generation;
    value.last_presented_generation = generation;
    value.last_flip_sequence = static_cast<std::uint32_t>(90 + generation);
    value.last_flip_timestamp_nanoseconds = 50'000'000 + generation;
    value.last_interval_nanoseconds = 16'666'667;
  }
  return value;
}

bool send_vrr(gwipc_connection *connection, const bool windows,
              const bool duplicate_capability,
              const gwipc_vrr_policy_mode right_policy,
              const std::uint64_t generation) {
  const auto left_capability = vrr_capability(11);
  const auto right_capability = vrr_capability(12);
  const auto left_policy = vrr_policy(11, right_policy);
  const auto right_policy_value = vrr_policy(12, right_policy);
  const auto left_state = vrr_state(11, right_policy, generation);
  const auto right_state = vrr_state(12, right_policy, generation);
  gwipc_presentation_timing timing{};
  timing.struct_size = sizeof(timing);
  timing.output_id = 12;
  timing.commit_id = 70 + generation;
  timing.presented_generation = generation;
  timing.flip_sequence = static_cast<std::uint32_t>(90 + generation);
  timing.flags = GWIPC_PRESENTATION_TIMING_SIMULATED;
  timing.kernel_timestamp_nanoseconds = 50'000'000 + generation;
  timing.interval_nanoseconds = 16'666'667;
  timing.timestamp_available = 1;
  if (!send_contract(connection, GWIPC_MESSAGE_OUTPUT_VRR_CAPABILITY_UPSERT,
                     GWIPC_FLAG_SNAPSHOT_ITEM, 0, left_capability,
                     gwipc_contract_encode_output_vrr_capability_upsert) ||
      !send_contract(connection, GWIPC_MESSAGE_OUTPUT_VRR_CAPABILITY_UPSERT,
                     GWIPC_FLAG_SNAPSHOT_ITEM, 0, right_capability,
                     gwipc_contract_encode_output_vrr_capability_upsert) ||
      !send_contract(connection, GWIPC_MESSAGE_OUTPUT_VRR_POLICY_UPSERT,
                     GWIPC_FLAG_SNAPSHOT_ITEM, 0, left_policy,
                     gwipc_contract_encode_output_vrr_policy_upsert) ||
      !send_contract(connection, GWIPC_MESSAGE_OUTPUT_VRR_POLICY_UPSERT,
                     GWIPC_FLAG_SNAPSHOT_ITEM, 0, right_policy_value,
                     gwipc_contract_encode_output_vrr_policy_upsert) ||
      !send_contract(connection, GWIPC_MESSAGE_OUTPUT_VRR_STATE_UPSERT,
                     GWIPC_FLAG_SNAPSHOT_ITEM, 0, left_state,
                     gwipc_contract_encode_output_vrr_state_upsert) ||
      !send_contract(connection, GWIPC_MESSAGE_OUTPUT_VRR_STATE_UPSERT,
                     GWIPC_FLAG_SNAPSHOT_ITEM, 0, right_state,
                     gwipc_contract_encode_output_vrr_state_upsert) ||
      !send_contract(connection, GWIPC_MESSAGE_PRESENTATION_TIMING,
                     GWIPC_FLAG_SNAPSHOT_ITEM, 0, timing,
                     gwipc_contract_encode_presentation_timing))
    return false;
  if (duplicate_capability &&
      !send_contract(connection, GWIPC_MESSAGE_OUTPUT_VRR_CAPABILITY_UPSERT,
                     GWIPC_FLAG_SNAPSHOT_ITEM, 0, right_capability,
                     gwipc_contract_encode_output_vrr_capability_upsert))
    return false;
  if (!windows)
    return true;
  gwipc_surface_vrr_state window{};
  window.struct_size = sizeof(window);
  window.surface_id = (UINT64_C(1) << 32U) | 41U;
  window.window_id = 41;
  window.output_id = 12;
  window.preference = GWIPC_VRR_PREFERENCE_PREFER;
  window.focused = 1;
  window.fullscreen = 1;
  window.reason_flags = GWIPC_VRR_REASON_WINDOW_SPANS_OUTPUTS;
  window.policy_generation = generation;
  return send_contract(connection, GWIPC_MESSAGE_SURFACE_VRR_STATE,
                       GWIPC_FLAG_SNAPSHOT_ITEM, 0, window,
                       gwipc_contract_encode_surface_vrr_state);
}

bool send_query_reply(gwipc_connection *connection,
                      const gwipc_message *query_message,
                      const ServerMode server_mode,
                      const gwipc_vrr_policy_mode right_policy,
                      const std::uint64_t generation) {
  gwipc_decoded_contract *raw = nullptr;
  if (gwipc_contract_decode_message(query_message, &raw) != GWIPC_STATUS_OK)
    return false;
  Owned<gwipc_decoded_contract, gwipc_decoded_contract_destroy> decoded(
      raw, gwipc_decoded_contract_destroy);
  const auto *query = gwipc_decoded_output_state_query(decoded.get());
  if (!query)
    return false;
  const bool inventory = (query->flags & (GWIPC_OUTPUT_QUERY_DESCRIPTORS |
                                          GWIPC_OUTPUT_QUERY_MODES |
                                          GWIPC_OUTPUT_QUERY_LAYOUT)) != 0;
  const bool windows = (query->flags & GWIPC_OUTPUT_QUERY_WINDOWS) != 0;
  const bool vrr = (query->flags & GWIPC_OUTPUT_QUERY_VRR) != 0;
  const bool duplicate_capability = server_mode == ServerMode::DuplicateVrr;
  const std::uint32_t count =
      (inventory ? 6U : 0U) + (windows ? 3U : 0U) +
      (vrr ? 7U + (windows ? 1U : 0U) + (duplicate_capability ? 1U : 0U) : 0U);
  gwipc_snapshot_begin begin{
      sizeof(begin), 91, GWIPC_SNAPSHOT_OUTPUTS, 0, generation, count, {}};
  if (!send_control(connection, GWIPC_MESSAGE_SNAPSHOT_BEGIN, begin,
                    gwipc_control_encode_snapshot_begin))
    return false;
  if (inventory) {
    const auto left = descriptor(11, "LEFT");
    const auto right = descriptor(12, "RIGHT");
    const auto left_mode = mode(11, 21);
    const auto right_mode = mode(12, 22);
    const auto left_state = output(11, 0);
    const auto right_state = output(12, 640);
    if (!send_contract(connection, GWIPC_MESSAGE_OUTPUT_DESCRIPTOR_UPSERT,
                       GWIPC_FLAG_SNAPSHOT_ITEM, 0, left,
                       gwipc_contract_encode_output_descriptor_upsert) ||
        !send_contract(connection, GWIPC_MESSAGE_OUTPUT_DESCRIPTOR_UPSERT,
                       GWIPC_FLAG_SNAPSHOT_ITEM, 0, right,
                       gwipc_contract_encode_output_descriptor_upsert) ||
        !send_contract(connection, GWIPC_MESSAGE_OUTPUT_MODE_UPSERT,
                       GWIPC_FLAG_SNAPSHOT_ITEM, 0, left_mode,
                       gwipc_contract_encode_output_mode_upsert) ||
        !send_contract(connection, GWIPC_MESSAGE_OUTPUT_MODE_UPSERT,
                       GWIPC_FLAG_SNAPSHOT_ITEM, 0, right_mode,
                       gwipc_contract_encode_output_mode_upsert) ||
        !send_contract(connection, GWIPC_MESSAGE_OUTPUT_UPSERT,
                       GWIPC_FLAG_SNAPSHOT_ITEM, 0, left_state,
                       gwipc_contract_encode_output_upsert) ||
        !send_contract(connection, GWIPC_MESSAGE_OUTPUT_UPSERT,
                       GWIPC_FLAG_SNAPSHOT_ITEM, 0, right_state,
                       gwipc_contract_encode_output_upsert))
      return false;
  }
  if (windows && !send_window(connection, generation))
    return false;
  if (vrr && !send_vrr(connection, windows, duplicate_capability, right_policy,
                       generation))
    return false;
  gwipc_snapshot_end end{sizeof(end), 91, generation, count, {}};
  gwipc_output_configuration_acknowledged ack{};
  ack.struct_size = sizeof(ack);
  ack.request_id = query->query_id;
  ack.applied_generation = generation;
  ack.result = GWIPC_OUTPUT_CONFIGURATION_ACCEPTED;
  ack.primary_output_id = 11;
  ack.root_logical_width = 1280;
  ack.root_logical_height = 480;
  ack.enabled_output_count = 2;
  return send_control(connection, GWIPC_MESSAGE_SNAPSHOT_END, end,
                      gwipc_control_encode_snapshot_end) &&
         send_contract(
             connection, GWIPC_MESSAGE_OUTPUT_CONFIGURATION_ACKNOWLEDGED,
             GWIPC_FLAG_REPLY, gwipc_message_sequence(query_message), ack,
             gwipc_contract_encode_output_configuration_acknowledged);
}

bool handle_commit(gwipc_connection *connection, const ServerMode server_mode) {
  bool saw_begin = false;
  bool saw_end = false;
  std::uint32_t expected_count = 0;
  std::uint32_t actual_count = 0;
  std::map<std::uint64_t, gwipc_output_upsert> outputs;
  std::map<std::uint64_t, gwipc_vrr_policy_mode> policies;
  std::uint64_t configuration_id = 0;
  while (true) {
    auto message = receive(connection);
    if (!message)
      return false;
    const auto type = gwipc_message_type(message.get());
    if (type == GWIPC_MESSAGE_SNAPSHOT_BEGIN) {
      gwipc_decoded_control *raw = nullptr;
      if (gwipc_control_decode_message(message.get(), &raw) != GWIPC_STATUS_OK)
        return false;
      Owned<gwipc_decoded_control, gwipc_decoded_control_destroy> decoded(
          raw, gwipc_decoded_control_destroy);
      const auto *begin = gwipc_decoded_snapshot_begin(decoded.get());
      const std::uint32_t required_count =
          server_mode == ServerMode::VrrCommit ? 4U : 2U;
      if (!begin || saw_begin || begin->domain != GWIPC_SNAPSHOT_OUTPUTS ||
          begin->generation != 1 ||
          begin->expected_item_count != required_count)
        return false;
      saw_begin = true;
      configuration_id = begin->snapshot_id;
      expected_count = begin->expected_item_count;
    } else if (type == GWIPC_MESSAGE_OUTPUT_UPSERT) {
      gwipc_decoded_contract *raw = nullptr;
      if (gwipc_contract_decode_message(message.get(), &raw) != GWIPC_STATUS_OK)
        return false;
      Owned<gwipc_decoded_contract, gwipc_decoded_contract_destroy> decoded(
          raw, gwipc_decoded_contract_destroy);
      const auto *value = gwipc_decoded_output_upsert(decoded.get());
      if (!value || !outputs.emplace(value->output_id, *value).second)
        return false;
      ++actual_count;
    } else if (type == GWIPC_MESSAGE_OUTPUT_VRR_POLICY_UPSERT) {
      gwipc_decoded_contract *raw = nullptr;
      if (gwipc_contract_decode_message(message.get(), &raw) != GWIPC_STATUS_OK)
        return false;
      Owned<gwipc_decoded_contract, gwipc_decoded_contract_destroy> decoded(
          raw, gwipc_decoded_contract_destroy);
      const auto *value = gwipc_decoded_output_vrr_policy_upsert(decoded.get());
      if (!value || !policies.emplace(value->output_id, value->mode).second)
        return false;
      ++actual_count;
    } else if (type == GWIPC_MESSAGE_SNAPSHOT_END) {
      gwipc_decoded_control *raw = nullptr;
      if (gwipc_control_decode_message(message.get(), &raw) != GWIPC_STATUS_OK)
        return false;
      Owned<gwipc_decoded_control, gwipc_decoded_control_destroy> decoded(
          raw, gwipc_decoded_control_destroy);
      const auto *end = gwipc_decoded_snapshot_end(decoded.get());
      if (!end || !saw_begin || saw_end ||
          end->snapshot_id != configuration_id || end->generation != 1 ||
          end->actual_item_count != actual_count ||
          actual_count != expected_count)
        return false;
      saw_end = true;
    } else if (type == GWIPC_MESSAGE_OUTPUT_CONFIGURATION_COMMIT) {
      gwipc_decoded_contract *raw = nullptr;
      if (gwipc_contract_decode_message(message.get(), &raw) != GWIPC_STATUS_OK)
        return false;
      Owned<gwipc_decoded_contract, gwipc_decoded_contract_destroy> decoded(
          raw, gwipc_decoded_contract_destroy);
      const auto *commit =
          gwipc_decoded_output_configuration_commit(decoded.get());
      const auto left = outputs.find(11);
      const auto right = outputs.find(12);
      if (!commit || !saw_end || commit->configuration_id != configuration_id ||
          commit->base_generation != 1 || commit->primary_output_id != 11 ||
          outputs.size() != 2 || left == outputs.end() ||
          right == outputs.end())
        return false;
      if (server_mode == ServerMode::Commit) {
        if (right->second.logical_x != 640 ||
            right->second.scale_numerator != 5 ||
            right->second.scale_denominator != 4 ||
            right->second.logical_width != 512 || !policies.empty())
          return false;
      } else if (server_mode == ServerMode::VrrCommit) {
        const auto left_policy = policies.find(11);
        const auto right_policy = policies.find(12);
        if (right->second.logical_x != 640 ||
            right->second.scale_numerator != 1 ||
            right->second.scale_denominator != 1 ||
            right->second.logical_width != 640 || policies.size() != 2 ||
            left_policy == policies.end() ||
            left_policy->second != GWIPC_VRR_POLICY_OFF ||
            right_policy == policies.end() ||
            right_policy->second != GWIPC_VRR_POLICY_FULLSCREEN)
          return false;
      } else {
        return false;
      }
      gwipc_output_configuration_acknowledged ack{};
      ack.struct_size = sizeof(ack);
      ack.request_id = configuration_id;
      ack.applied_generation = 2;
      ack.result = GWIPC_OUTPUT_CONFIGURATION_ACCEPTED;
      ack.primary_output_id = 11;
      ack.root_logical_width = server_mode == ServerMode::Commit ? 1152 : 1280;
      ack.root_logical_height = 480;
      ack.enabled_output_count = 2;
      return send_contract(
          connection, GWIPC_MESSAGE_OUTPUT_CONFIGURATION_ACKNOWLEDGED,
          GWIPC_FLAG_REPLY, gwipc_message_sequence(message.get()), ack,
          gwipc_contract_encode_output_configuration_acknowledged);
    }
  }
}

} // namespace

int main(const int argc, char **argv) {
  if (argc != 3)
    return 2;
  const std::string path = argv[1];
  const auto server_mode = parse_mode(argv[2]);
  const bool expect_commit =
      server_mode == ServerMode::Commit || server_mode == ServerMode::VrrCommit;
  gwipc_listener_options options{};
  options.struct_size = sizeof(options);
  options.path = path.c_str();
  options.local_role = GWIPC_ROLE_PROTOCOL_SERVER;
  options.accepted_peer_roles = GWIPC_ROLE_BIT(GWIPC_ROLE_DIAGNOSTIC_TOOL);
  options.offered_capabilities =
      GWIPC_CAP_SNAPSHOTS | GWIPC_CAP_OUTPUT_STATE | GWIPC_CAP_OUTPUT_CONTROL |
      GWIPC_CAP_SURFACE_STATE | GWIPC_CAP_WINDOW_LIFECYCLE |
      GWIPC_CAP_SURFACE_OUTPUT_MEMBERSHIP | GWIPC_CAP_SCALE_METADATA;
  if (offers_vrr(server_mode))
    options.offered_capabilities |= GWIPC_CAP_VRR_METADATA |
                                    GWIPC_CAP_VRR_POLICY |
                                    GWIPC_CAP_PRESENTATION_TIMING;
  options.required_peer_capabilities = GWIPC_CAP_OUTPUT_CONTROL;
  options.maximum_payload = 4096;
  options.maximum_fd_count = 0;
  options.require_same_uid = 1;
  gwipc_listener *listener_raw = nullptr;
  const auto listener_status = gwipc_listener_create(&options, &listener_raw);
  if (listener_status != GWIPC_STATUS_OK) {
    std::fprintf(stderr, "fake output listener failed: %s\n",
                 gwipc_status_string(listener_status));
    return 1;
  }
  Owned<gwipc_listener, gwipc_listener_destroy> listener(
      listener_raw, gwipc_listener_destroy);
  gwipc_connection *connection_raw = nullptr;
  for (unsigned attempt = 0; attempt < 500 && !connection_raw; ++attempt) {
    pollfd descriptor{gwipc_listener_fd(listener.get()), POLLIN, 0};
    if (::poll(&descriptor, 1, 10) < 0)
      return 1;
    if ((descriptor.revents & POLLIN) != 0) {
      const auto status =
          gwipc_listener_accept(listener.get(), &connection_raw);
      if (status != GWIPC_STATUS_OK && status != GWIPC_STATUS_WOULD_BLOCK)
        return 1;
    }
  }
  Owned<gwipc_connection, gwipc_connection_destroy> connection(
      connection_raw, gwipc_connection_destroy);
  if (!connection)
    return 1;
  for (unsigned attempt = 0; attempt < 500; ++attempt) {
    if (gwipc_connection_get_state(connection.get()) ==
        GWIPC_CONNECTION_ESTABLISHED)
      break;
    if (!pump(connection.get()))
      return 1;
  }
  auto query = receive(connection.get());
  if (!query ||
      gwipc_message_type(query.get()) != GWIPC_MESSAGE_OUTPUT_STATE_QUERY ||
      !send_query_reply(connection.get(), query.get(), server_mode,
                        GWIPC_VRR_POLICY_OFF, 1))
    return 1;
  if (expect_commit && !handle_commit(connection.get(), server_mode))
    return 1;
  if (server_mode == ServerMode::VrrCommit) {
    auto applied_query = receive(connection.get());
    if (!applied_query ||
        gwipc_message_type(applied_query.get()) !=
            GWIPC_MESSAGE_OUTPUT_STATE_QUERY ||
        !send_query_reply(connection.get(), applied_query.get(), server_mode,
                          GWIPC_VRR_POLICY_FULLSCREEN, 2))
      return 1;
  }
  for (unsigned attempt = 0; attempt < 500; ++attempt) {
    if (!pump(connection.get()) ||
        gwipc_connection_get_state(connection.get()) == GWIPC_CONNECTION_CLOSED)
      return 0;
  }
  return 1;
}
