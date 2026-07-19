#include <glasswyrm/ipc.h>

#include "ipc/internal.hpp"

#include <cstdint>
#include <cstdio>

namespace {

bool check(const bool condition, const char* message) {
  if (!condition)
    std::fprintf(stderr, "public VRR API: %s\n", message);
  return condition;
}

gwipc_decoded_contract* decode(const std::uint16_t type,
                               const gwipc_contract_payload* payload) {
  std::size_t size = 0;
  const auto* data = gwipc_contract_payload_data(payload, &size);
  if (!data || size == 0)
    return nullptr;
  auto* message = new gwipc_message;
  message->type = type;
  message->payload.assign(data, data + size);
  gwipc_decoded_contract* decoded = nullptr;
  const auto status = gwipc_contract_decode_message(message, &decoded);
  gwipc_message_destroy(message);
  return status == GWIPC_STATUS_OK ? decoded : nullptr;
}

template <typename Value, typename Encoder, typename Accessor, typename Verify>
bool round_trip(const Value& value, const std::uint16_t type, Encoder encode,
                Accessor access, Verify verify, const char* message) {
  gwipc_contract_payload* payload = nullptr;
  if (!check(encode(&value, &payload) == GWIPC_STATUS_OK && payload,
             message))
    return false;
  auto* decoded = decode(type, payload);
  gwipc_contract_payload_destroy(payload);
  const auto* view = access(decoded);
  const bool ok = check(view && verify(*view), message);
  gwipc_decoded_contract_destroy(decoded);
  return ok;
}

template <typename Value, typename Encoder>
bool rejects_reserved(Value value, Encoder encode, const char* message) {
  value.reserved[0] = 1;
  auto* const sentinel = reinterpret_cast<gwipc_contract_payload*>(1);
  auto* payload = sentinel;
  return check(encode(&value, &payload) == GWIPC_STATUS_INVALID_ARGUMENT &&
                   payload == sentinel,
               message);
}

bool output_records() {
  gwipc_output_vrr_capability_upsert capability{};
  capability.struct_size = sizeof(capability);
  capability.output_id = 1;
  capability.simulated = 1;
  capability.range_available = 1;
  capability.minimum_refresh_millihertz = 40'000;
  capability.maximum_refresh_millihertz = 144'000;
  capability.reason_flags = GWIPC_VRR_REASON_SIMULATED_HEADLESS;

  gwipc_output_vrr_policy_upsert policy{};
  policy.struct_size = sizeof(policy);
  policy.output_id = 1;
  policy.mode = GWIPC_VRR_POLICY_FULLSCREEN;

  gwipc_output_vrr_state_upsert state{};
  state.struct_size = sizeof(state);
  state.output_id = 1;
  state.requested_mode = GWIPC_VRR_POLICY_FOCUSED;
  state.decision = GWIPC_VRR_DECISION_ENABLED;
  state.desired_enabled = 1;
  state.effective_enabled = 1;
  state.property_readback_valid = 1;
  state.session_active = 1;
  state.candidate_window_id = 0x400001;
  state.candidate_surface_id = 9;
  state.state_generation = 10;
  state.transition_serial = 11;
  state.last_commit_id = 12;
  state.last_presented_generation = 13;
  state.last_flip_sequence = 14;
  state.last_flip_timestamp_nanoseconds = 15;
  state.last_interval_nanoseconds = 16;

  bool ok = round_trip(
      capability, GWIPC_MESSAGE_OUTPUT_VRR_CAPABILITY_UPSERT,
      gwipc_contract_encode_output_vrr_capability_upsert,
      gwipc_decoded_output_vrr_capability_upsert,
      [](const auto& v) {
        return v.output_id == 1 && v.simulated == 1 &&
               v.minimum_refresh_millihertz == 40'000 &&
               v.maximum_refresh_millihertz == 144'000;
      },
      "capability round trip failed");
  ok &= round_trip(
      policy, GWIPC_MESSAGE_OUTPUT_VRR_POLICY_UPSERT,
      gwipc_contract_encode_output_vrr_policy_upsert,
      gwipc_decoded_output_vrr_policy_upsert,
      [](const auto& v) {
        return v.output_id == 1 && v.mode == GWIPC_VRR_POLICY_FULLSCREEN;
      },
      "output policy round trip failed");
  ok &= round_trip(
      state, GWIPC_MESSAGE_OUTPUT_VRR_STATE_UPSERT,
      gwipc_contract_encode_output_vrr_state_upsert,
      gwipc_decoded_output_vrr_state_upsert,
      [](const auto& v) {
        return v.output_id == 1 && v.effective_enabled == 1 &&
               v.candidate_window_id == 0x400001 &&
               v.last_interval_nanoseconds == 16;
      },
      "effective state round trip failed");
  ok &= rejects_reserved(capability,
                         gwipc_contract_encode_output_vrr_capability_upsert,
                         "capability reserved fields were accepted");
  ok &= rejects_reserved(policy, gwipc_contract_encode_output_vrr_policy_upsert,
                         "policy reserved fields were accepted");
  policy.mode = static_cast<gwipc_vrr_policy_mode>(0);
  gwipc_contract_payload* payload = nullptr;
  ok &= check(gwipc_contract_encode_output_vrr_policy_upsert(&policy, &payload) ==
                  GWIPC_STATUS_INVALID_ARGUMENT,
              "invalid policy enum was accepted");
  state.effective_enabled = 2;
  ok &= check(gwipc_contract_encode_output_vrr_state_upsert(&state, &payload) ==
                  GWIPC_STATUS_INVALID_ARGUMENT,
              "noncanonical effective-state boolean was accepted");
  return ok;
}

bool surface_and_policy_records() {
  gwipc_surface_vrr_state surface{};
  surface.struct_size = sizeof(surface);
  surface.surface_id = 9;
  surface.window_id = 0x400001;
  surface.output_id = 1;
  surface.preference = GWIPC_VRR_PREFERENCE_PREFER;
  surface.policy_selected = 1;
  surface.policy_eligible = 1;
  surface.focused = 1;
  surface.fullscreen = 1;
  surface.borderless_fullscreen = 1;
  surface.exclusive_output_membership = 1;
  surface.policy_generation = 10;

  gwipc_policy_window_vrr_upsert window_input{};
  window_input.struct_size = sizeof(window_input);
  window_input.window_id = surface.window_id;
  window_input.preference = surface.preference;

  gwipc_policy_output_vrr_upsert output_input{};
  output_input.struct_size = sizeof(output_input);
  output_input.output_id = 1;
  output_input.mode = GWIPC_VRR_POLICY_FULLSCREEN;
  output_input.hardware_capable = 1;
  output_input.kms_controllable = 1;

  gwipc_policy_window_vrr_state window_state{};
  window_state.struct_size = sizeof(window_state);
  window_state.window_id = surface.window_id;
  window_state.output_id = 1;
  window_state.preference = surface.preference;
  window_state.selected = 1;
  window_state.eligible = 1;
  window_state.focused = 1;
  window_state.fullscreen = 1;
  window_state.borderless_fullscreen = 1;
  window_state.exclusive_output_membership = 1;

  gwipc_policy_output_vrr_state output_state{};
  output_state.struct_size = sizeof(output_state);
  output_state.output_id = 1;
  output_state.mode = GWIPC_VRR_POLICY_FULLSCREEN;
  output_state.selected_window_id = surface.window_id;
  output_state.desired_enabled = 1;
  output_state.candidate_required = 1;

  bool ok = round_trip(
      surface, GWIPC_MESSAGE_SURFACE_VRR_STATE,
      gwipc_contract_encode_surface_vrr_state,
      gwipc_decoded_surface_vrr_state,
      [](const auto& v) {
        return v.surface_id == 9 && v.window_id == 0x400001 &&
               v.preference == GWIPC_VRR_PREFERENCE_PREFER &&
               v.policy_selected == 1;
      },
      "surface state round trip failed");
  ok &= round_trip(
      window_input, GWIPC_MESSAGE_POLICY_WINDOW_VRR_UPSERT,
      gwipc_contract_encode_policy_window_vrr_upsert,
      gwipc_decoded_policy_window_vrr_upsert,
      [](const auto& v) {
        return v.window_id == 0x400001 &&
               v.preference == GWIPC_VRR_PREFERENCE_PREFER;
      },
      "window policy input round trip failed");
  ok &= round_trip(
      output_input, GWIPC_MESSAGE_POLICY_OUTPUT_VRR_UPSERT,
      gwipc_contract_encode_policy_output_vrr_upsert,
      gwipc_decoded_policy_output_vrr_upsert,
      [](const auto& v) {
        return v.output_id == 1 && v.hardware_capable == 1 &&
               v.kms_controllable == 1;
      },
      "output policy input round trip failed");
  ok &= round_trip(
      window_state, GWIPC_MESSAGE_POLICY_WINDOW_VRR_STATE,
      gwipc_contract_encode_policy_window_vrr_state,
      gwipc_decoded_policy_window_vrr_state,
      [](const auto& v) {
        return v.window_id == 0x400001 && v.output_id == 1 && v.selected == 1;
      },
      "window policy result round trip failed");
  ok &= round_trip(
      output_state, GWIPC_MESSAGE_POLICY_OUTPUT_VRR_STATE,
      gwipc_contract_encode_policy_output_vrr_state,
      gwipc_decoded_policy_output_vrr_state,
      [](const auto& v) {
        return v.output_id == 1 && v.selected_window_id == 0x400001 &&
               v.desired_enabled == 1;
      },
      "output policy result round trip failed");
  ok &= rejects_reserved(surface, gwipc_contract_encode_surface_vrr_state,
                         "surface reserved fields were accepted");
  ok &= rejects_reserved(window_input,
                         gwipc_contract_encode_policy_window_vrr_upsert,
                         "window input reserved fields were accepted");
  ok &= rejects_reserved(output_state,
                         gwipc_contract_encode_policy_output_vrr_state,
                         "output result reserved fields were accepted");
  return ok;
}

bool timing_record() {
  gwipc_presentation_timing timing{};
  timing.struct_size = sizeof(timing);
  timing.output_id = 1;
  timing.commit_id = 2;
  timing.presented_generation = 3;
  timing.flip_sequence = 4;
  timing.flags = GWIPC_PRESENTATION_TIMING_SIMULATED;
  timing.kernel_timestamp_nanoseconds = UINT64_C(50'000'000);
  timing.interval_nanoseconds = UINT64_C(16'666'667);
  timing.effective_vrr_enabled = 1;
  timing.timestamp_available = 1;

  bool ok = round_trip(
      timing, GWIPC_MESSAGE_PRESENTATION_TIMING,
      gwipc_contract_encode_presentation_timing,
      gwipc_decoded_presentation_timing,
      [](const auto& v) {
        return v.output_id == 1 && v.commit_id == 2 &&
               v.presented_generation == 3 && v.flip_sequence == 4 &&
               v.interval_nanoseconds == UINT64_C(16'666'667);
      },
      "presentation timing round trip failed");
  ok &= rejects_reserved(timing, gwipc_contract_encode_presentation_timing,
                         "timing reserved fields were accepted");
  timing.timestamp_available = 2;
  gwipc_contract_payload* payload = nullptr;
  ok &= check(gwipc_contract_encode_presentation_timing(&timing, &payload) ==
                  GWIPC_STATUS_INVALID_ARGUMENT,
              "noncanonical timing boolean was accepted");
  return ok;
}

}  // namespace

int main() {
  const auto api = gwipc_get_api_version();
  const bool ok =
      check(api.major == 0 && api.minor == 9 && api.patch == 0,
            "API version is not 0.9.0") &&
      output_records() && surface_and_policy_records() && timing_record();
  return ok ? 0 : 1;
}
