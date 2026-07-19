#include "ipc/wire/vrr_contract.hpp"

#include "tests/helpers/test_support.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace {

using namespace gw::ipc::wire;
using gw::test::require;

std::vector<std::uint8_t> hex(const std::string_view text) {
  const auto digit = [](const char value) -> std::uint8_t {
    return static_cast<std::uint8_t>(value <= '9' ? value - '0'
                                                  : value - 'a' + 10);
  };
  std::vector<std::uint8_t> result;
  result.reserve(text.size() / 2);
  for (std::size_t index = 0; index < text.size(); index += 2)
    result.push_back(static_cast<std::uint8_t>((digit(text[index]) << 4U) |
                                               digit(text[index + 1])));
  return result;
}

template <typename Value>
void require_golden(const Value& value, const std::string_view expected_hex,
                    const std::size_t expected_size, const char* message) {
  const auto expected = hex(expected_hex);
  Value decoded;
  require(expected.size() == expected_size && encode(value) == expected &&
              decode(expected, decoded) == CodecStatus::Ok &&
              encode(decoded) == expected,
          message);
}

template <typename Value>
void require_exact_framing(const Value& value, const char* message) {
  const auto bytes = encode(value);
  Value decoded;
  for (std::size_t size = 0; size < bytes.size(); ++size)
    require(decode(std::span(bytes).first(size), decoded) ==
                CodecStatus::Truncated,
            message);
  auto trailing = bytes;
  trailing.push_back(0);
  require(decode(trailing, decoded) == CodecStatus::TrailingData, message);
}

template <typename Value>
void require_invalid(const Value& value, const char* message) {
  Value decoded;
  require(decode(encode(value), decoded) == CodecStatus::InvalidValue, message);
}

template <typename Value>
void require_invalid_byte(const Value& value, const std::size_t offset,
                          const std::uint8_t byte, const char* message) {
  auto bytes = encode(value);
  require(offset < bytes.size(), message);
  bytes[offset] = byte;
  Value decoded;
  require(decode(bytes, decoded) == CodecStatus::InvalidValue, message);
}

constexpr std::uint64_t kOutput = UINT64_C(0x0102030405060708);
constexpr std::uint32_t kWindow = UINT32_C(0x00400001);
constexpr std::uint64_t kSurface = UINT64_C(0x1112131415161718);

OutputVrrCapabilityUpsert capability() {
  OutputVrrCapabilityUpsert value;
  value.output_id = kOutput;
  value.connector_property_present = true;
  value.hardware_capable = true;
  value.kms_controllable = true;
  value.range_available = true;
  value.atomic_required = true;
  value.minimum_refresh_millihertz = 40'000;
  value.maximum_refresh_millihertz = 144'000;
  value.reason_flags = UINT64_C(1) << 3U;
  return value;
}

OutputVrrStateUpsert output_state() {
  OutputVrrStateUpsert value;
  value.output_id = kOutput;
  value.requested_mode = VrrPolicyMode::Focused;
  value.decision = VrrDecision::Enabled;
  value.desired_enabled = true;
  value.effective_enabled = true;
  value.property_readback_valid = true;
  value.session_active = true;
  value.candidate_window_id = kWindow;
  value.candidate_surface_id = kSurface;
  value.state_generation = 0x21;
  value.transition_serial = 0x22;
  value.last_commit_id = 0x23;
  value.last_presented_generation = 0x24;
  value.last_flip_sequence = 0x25;
  value.last_flip_timestamp_nanoseconds = 0x26;
  value.last_interval_nanoseconds = 0x27;
  return value;
}

SurfaceVrrState surface_state() {
  SurfaceVrrState value;
  value.surface_id = kSurface;
  value.window_id = kWindow;
  value.output_id = kOutput;
  value.preference = VrrWindowPreference::Prefer;
  value.policy_selected = true;
  value.policy_eligible = true;
  value.focused = true;
  value.fullscreen = true;
  value.borderless_fullscreen = true;
  value.exclusive_output_membership = true;
  value.policy_generation = 0x21;
  return value;
}

void test_output_goldens() {
  const auto cap = capability();
  require_golden(
      cap,
      "08070605040302010101010001010000409c0000803202000800000000000000"
      "0000000000000000",
      kOutputVrrCapabilityPayloadSize,
      "output VRR capability matches its exact fixed golden");

  const OutputVrrPolicyUpsert policy{kOutput, VrrPolicyMode::Fullscreen, 0};
  require_golden(policy, "08070605040302010200000000000000",
                 kOutputVrrPolicyPayloadSize,
                 "output VRR policy matches its exact fixed golden");

  const auto state = output_state();
  require_golden(
      state,
      "0807060504030201030002000101010101004000000000001817161514131211"
      "0000000000000000210000000000000022000000000000002300000000000000"
      "2400000000000000250000000000000026000000000000002700000000000000",
      kOutputVrrStatePayloadSize,
      "output VRR state matches its exact fixed golden");

  require_exact_framing(cap, "capability rejects truncation and trailing data");
  require_exact_framing(policy,
                        "policy rejects truncation and trailing data");
  require_exact_framing(state, "state rejects truncation and trailing data");
  require_invalid_byte(cap, 8, 2,
                       "capability booleans require canonical encoding");
  require_invalid_byte(cap, 28, 2,
                       "capability rejects unknown reason bits");
  require_invalid_byte(policy, 8, 0, "policy rejects unknown enum values");
  require_invalid_byte(policy, 10, 1, "policy reserved word must be zero");
  auto invalid_state = state;
  invalid_state.property_readback_valid = false;
  require_invalid(invalid_state,
                  "effective state requires successful property readback");
  invalid_state = state;
  invalid_state.decision = VrrDecision::Rejected;
  invalid_state.effective_enabled = false;
  invalid_state.reason_flags = 0;
  require_invalid(invalid_state,
                  "disabled and rejected decisions require a reason");
}

void test_policy_goldens() {
  const auto surface = surface_state();
  require_golden(
      surface,
      "1817161514131211010040000000000008070605040302010300010101010101"
      "000000000000000021000000000000000000000000000000",
      kSurfaceVrrStatePayloadSize,
      "surface VRR state matches its exact fixed golden");

  const PolicyWindowVrrUpsert window_input{kWindow,
                                            VrrWindowPreference::Prefer, 0};
  require_golden(window_input, "01004000030000000000000000000000",
                 kPolicyWindowVrrUpsertPayloadSize,
                 "window VRR input matches its exact fixed golden");

  const PolicyOutputVrrUpsert output_input{
      kOutput, VrrPolicyMode::Fullscreen, true, true, 0};
  require_golden(output_input, "08070605040302010200010100000000",
                 kPolicyOutputVrrUpsertPayloadSize,
                 "output VRR input matches its exact fixed golden");

  PolicyWindowVrrState window_result;
  window_result.window_id = kWindow;
  window_result.output_id = kOutput;
  window_result.preference = VrrWindowPreference::Prefer;
  window_result.selected = true;
  window_result.eligible = true;
  window_result.focused = true;
  window_result.fullscreen = true;
  window_result.borderless_fullscreen = true;
  window_result.exclusive_output_membership = true;
  require_golden(
      window_result,
      "0100400000000000080706050403020103000101010101010000000000000000"
      "0000000000000000",
      kPolicyWindowVrrStatePayloadSize,
      "window VRR result matches its exact fixed golden");

  const PolicyOutputVrrState output_result{
      kOutput, VrrPolicyMode::Fullscreen, kWindow, true, true, 0, 0};
  require_golden(
      output_result,
      "0807060504030201020001010100400000000000000000000000000000000000",
      kPolicyOutputVrrStatePayloadSize,
      "output VRR result matches its exact fixed golden");

  require_exact_framing(surface,
                        "surface state rejects truncation and trailing data");
  require_exact_framing(window_input,
                        "window input rejects truncation and trailing data");
  require_exact_framing(output_input,
                        "output input rejects truncation and trailing data");
  require_exact_framing(window_result,
                        "window result rejects truncation and trailing data");
  require_exact_framing(output_result,
                        "output result rejects truncation and trailing data");
  auto invalid_surface = surface;
  invalid_surface.policy_eligible = false;
  require_invalid(invalid_surface, "a selected surface must be eligible");
  auto simulated_input = output_input;
  simulated_input.hardware_capable = false;
  PolicyOutputVrrUpsert decoded_input;
  require(decode(encode(simulated_input), decoded_input) == CodecStatus::Ok &&
              !decoded_input.hardware_capable &&
              decoded_input.kms_controllable,
          "simulated headless control does not imply hardware capability");
  auto invalid_result = output_result;
  invalid_result.selected_window_id = 0;
  require_invalid(invalid_result,
                  "required enabled candidate must name a window");
}

void test_timing_golden() {
  const PresentationTiming timing{kOutput, 0x11, 0x12, 0x13,
                                  kPresentationTimingSimulated,
                                  0x14, 0x15, true, true};
  require_golden(
      timing,
      "0807060504030201110000000000000012000000000000001300000001000000"
      "140000000000000015000000000000000101000000000000",
      kPresentationTimingPayloadSize,
      "presentation timing matches its exact fixed golden");
  require_exact_framing(timing,
                        "timing rejects every truncation and trailing data");
  require_invalid_byte(timing, 28, 2, "timing rejects unknown flags");
  require_invalid_byte(timing, 48, 2,
                       "timing booleans require canonical encoding");
  auto invalid = timing;
  invalid.timestamp_available = false;
  require_invalid(invalid,
                  "unavailable timestamps require zero timing values");
  require(kVrrContractFdCount == 0,
          "VRR contracts never carry file descriptors");
}

}  // namespace

int main() {
  test_policy_goldens();
  test_timing_golden();
  test_output_goldens();
  return 0;
}
