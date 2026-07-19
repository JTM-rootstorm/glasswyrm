#include <glasswyrm/ipc.h>

#include <array>
#include <cstdio>
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
  if (encode(&value, &raw) != GWIPC_STATUS_OK)
    return false;
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
  return gwipc_connection_enqueue(connection, &message) == GWIPC_STATUS_OK;
}

template <typename Value>
bool send_control(gwipc_connection *connection, const std::uint16_t type,
                  const Value &value,
                  gwipc_status (*encode)(const Value *,
                                         gwipc_control_payload **)) {
  gwipc_control_payload *raw = nullptr;
  if (encode(&value, &raw) != GWIPC_STATUS_OK)
    return false;
  Owned<gwipc_control_payload, gwipc_control_payload_destroy> payload(
      raw, gwipc_control_payload_destroy);
  std::size_t size = 0;
  const auto *data = gwipc_control_payload_data(payload.get(), &size);
  gwipc_outgoing_message message{};
  message.struct_size = sizeof(message);
  message.type = type;
  message.payload = data;
  message.payload_size = size;
  return gwipc_connection_enqueue(connection, &message) == GWIPC_STATUS_OK;
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
  value.color = {GWIPC_SDR_COLOR_SPACE_SRGB, GWIPC_TRANSFER_FUNCTION_SRGB,
                 GWIPC_COLOR_PRIMARIES_SRGB, 0, 0, 0, 0};
  return value;
}

bool send_window(gwipc_connection *connection) {
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
  surface.color = {GWIPC_SDR_COLOR_SPACE_SRGB, GWIPC_TRANSFER_FUNCTION_SRGB,
                   GWIPC_COLOR_PRIMARIES_SRGB, 0, 0, 0, 0};
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
  state.layout_generation = 1;
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

bool send_query_reply(gwipc_connection *connection,
                      const gwipc_message *query_message) {
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
  const std::uint32_t count = (inventory ? 6U : 0U) + (windows ? 3U : 0U);
  gwipc_snapshot_begin begin{sizeof(begin), 91, GWIPC_SNAPSHOT_OUTPUTS, 0, 1,
                             count, {}};
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
  if (windows && !send_window(connection))
    return false;
  gwipc_snapshot_end end{sizeof(end), 91, 1, count, {}};
  gwipc_output_configuration_acknowledged ack{};
  ack.struct_size = sizeof(ack);
  ack.request_id = query->query_id;
  ack.applied_generation = 1;
  ack.result = GWIPC_OUTPUT_CONFIGURATION_ACCEPTED;
  ack.primary_output_id = 11;
  ack.root_logical_width = 1280;
  ack.root_logical_height = 480;
  ack.enabled_output_count = 2;
  return send_control(connection, GWIPC_MESSAGE_SNAPSHOT_END, end,
                      gwipc_control_encode_snapshot_end) &&
         send_contract(connection,
                       GWIPC_MESSAGE_OUTPUT_CONFIGURATION_ACKNOWLEDGED,
                       GWIPC_FLAG_REPLY, gwipc_message_sequence(query_message),
                       ack,
                       gwipc_contract_encode_output_configuration_acknowledged);
}

bool handle_commit(gwipc_connection *connection) {
  bool saw_right = false;
  std::uint64_t configuration_id = 0;
  while (true) {
    auto message = receive(connection);
    if (!message)
      return false;
    if (gwipc_message_type(message.get()) == GWIPC_MESSAGE_OUTPUT_UPSERT) {
      gwipc_decoded_contract *raw = nullptr;
      if (gwipc_contract_decode_message(message.get(), &raw) != GWIPC_STATUS_OK)
        return false;
      Owned<gwipc_decoded_contract, gwipc_decoded_contract_destroy> decoded(
          raw, gwipc_decoded_contract_destroy);
      const auto *value = gwipc_decoded_output_upsert(decoded.get());
      if (value && value->output_id == 12)
        saw_right = value->logical_x == 640 && value->scale_numerator == 5 &&
                    value->scale_denominator == 4 &&
                    value->logical_width == 512;
    } else if (gwipc_message_type(message.get()) ==
               GWIPC_MESSAGE_OUTPUT_CONFIGURATION_COMMIT) {
      gwipc_decoded_contract *raw = nullptr;
      if (gwipc_contract_decode_message(message.get(), &raw) != GWIPC_STATUS_OK)
        return false;
      Owned<gwipc_decoded_contract, gwipc_decoded_contract_destroy> decoded(
          raw, gwipc_decoded_contract_destroy);
      const auto *commit =
          gwipc_decoded_output_configuration_commit(decoded.get());
      if (!commit || commit->base_generation != 1 ||
          commit->primary_output_id != 11 || !saw_right)
        return false;
      configuration_id = commit->configuration_id;
      gwipc_output_configuration_acknowledged ack{};
      ack.struct_size = sizeof(ack);
      ack.request_id = configuration_id;
      ack.applied_generation = 2;
      ack.result = GWIPC_OUTPUT_CONFIGURATION_ACCEPTED;
      ack.primary_output_id = 11;
      ack.root_logical_width = 1152;
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
  const bool expect_commit = std::string(argv[2]) == "commit";
  gwipc_listener_options options{};
  options.struct_size = sizeof(options);
  options.path = path.c_str();
  options.local_role = GWIPC_ROLE_PROTOCOL_SERVER;
  options.accepted_peer_roles = GWIPC_ROLE_BIT(GWIPC_ROLE_DIAGNOSTIC_TOOL);
  options.offered_capabilities =
      GWIPC_CAP_SNAPSHOTS | GWIPC_CAP_OUTPUT_STATE | GWIPC_CAP_OUTPUT_CONTROL |
      GWIPC_CAP_SURFACE_STATE | GWIPC_CAP_WINDOW_LIFECYCLE |
      GWIPC_CAP_SURFACE_OUTPUT_MEMBERSHIP | GWIPC_CAP_SCALE_METADATA;
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
      const auto status = gwipc_listener_accept(listener.get(), &connection_raw);
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
  if (!query || gwipc_message_type(query.get()) != GWIPC_MESSAGE_OUTPUT_STATE_QUERY ||
      !send_query_reply(connection.get(), query.get()))
    return 1;
  if (expect_commit && !handle_commit(connection.get()))
    return 1;
  for (unsigned attempt = 0; attempt < 500; ++attempt) {
    if (!pump(connection.get()) ||
        gwipc_connection_get_state(connection.get()) == GWIPC_CONNECTION_CLOSED)
      return 0;
  }
  return 1;
}
