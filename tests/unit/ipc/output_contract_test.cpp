#include "ipc/wire/output_contract.hpp"

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
void require_exact_framing(const Value& seed, const char* message) {
  const auto bytes = encode(seed);
  require(!bytes.empty(), message);
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
void require_invalid_byte(const Value& seed, const std::size_t offset,
                          const std::uint8_t byte, const char* message) {
  auto bytes = encode(seed);
  require(offset < bytes.size(), message);
  bytes[offset] = byte;
  Value decoded;
  require(decode(bytes, decoded) == CodecStatus::InvalidValue, message);
}

OutputDescriptorUpsert descriptor() {
  OutputDescriptorUpsert value;
  value.output_id = UINT64_C(0x0102030405060708);
  value.kind = OutputKind::Drm;
  value.capability_flags =
      kOutputConnected | kOutputModeFixed | kOutputScaleConfigurable |
      kOutputTransformConfigurable | kOutputPrimaryEligible |
      kOutputPhysicalDimensionsKnown;
  value.name = "DP-1";
  value.physical_width_millimeters = 600;
  value.physical_height_millimeters = 340;
  value.supported_transform_mask = 0xff;
  value.maximum_scale_numerator = 5;
  value.maximum_scale_denominator = 4;
  return value;
}

void test_inventory_contracts() {
  const auto output = descriptor();
  OutputDescriptorUpsert decoded_output;
  require(encode(output).size() ==
              kOutputDescriptorFixedPayloadSize + output.name.size() &&
              encode(output) ==
              hex("0807060504030201020004007d0000005802000054010000ff000000"
                  "01000000010000000500000004000000780000000010000000100000"
                  "44502d31") &&
              decode(encode(output), decoded_output) == CodecStatus::Ok &&
              decoded_output.name == "DP-1" &&
              decoded_output.maximum_scale_denominator == 4,
          "OutputDescriptorUpsert matches its exact variable-name golden");

  OutputModeUpsert mode{output.output_id, UINT64_C(0x1112131415161718),
                        1920, 1080, 60'000, true, true, 0};
  OutputModeUpsert decoded_mode;
  require(encode(mode).size() == kOutputModePayloadSize &&
              encode(mode) ==
              hex("08070605040302011817161514131211800700003804000060ea0000"
                  "010100000000000000000000") &&
              decode(encode(mode), decoded_mode) == CodecStatus::Ok &&
              decoded_mode.current,
          "OutputModeUpsert matches its exact fixed golden");

  require_exact_framing(output,
                        "descriptor rejects every truncation and trailing byte");
  require_exact_framing(mode,
                        "mode rejects every truncation and trailing byte");

  auto invalid_output = output;
  invalid_output.name = std::string(64, 'x');
  require(encode(invalid_output).empty(),
          "descriptor encoder refuses names above the wire limit");
  invalid_output = output;
  invalid_output.name = std::string(1, static_cast<char>(0xc0));
  require(decode(encode(invalid_output), decoded_output) ==
              CodecStatus::InvalidValue,
          "descriptor decoder requires canonical UTF-8 names");
  invalid_output = output;
  invalid_output.capability_flags |= UINT32_C(0x80);
  require(decode(encode(invalid_output), decoded_output) ==
              CodecStatus::InvalidValue,
          "descriptor decoder rejects unknown capability flags");
  invalid_output = output;
  invalid_output.minimum_scale_numerator = 2;
  invalid_output.minimum_scale_denominator = 2;
  require(decode(encode(invalid_output), decoded_output) ==
              CodecStatus::InvalidValue,
          "descriptor scales must already be reduced");

  auto invalid_mode = mode;
  invalid_mode.flags = 1;
  require(decode(encode(invalid_mode), decoded_mode) ==
              CodecStatus::InvalidValue,
          "M13 mode flags are reserved zero");
  auto invalid_mode_bytes = encode(mode);
  invalid_mode_bytes[28] = 2;
  require(decode(invalid_mode_bytes, decoded_mode) == CodecStatus::InvalidValue,
          "mode booleans have canonical zero-or-one encodings");
  require_invalid_byte(mode, 30, 1, "mode reserved word must be zero");
  require_invalid_byte(mode, 36, 1,
                       "mode trailing reserved word must be zero");
  require_invalid_byte(output, 8, 3,
                       "descriptor rejects unknown output kinds");
  auto excessive_name = encode(output);
  excessive_name[10] = 64;
  require(decode(excessive_name, decoded_output) == CodecStatus::LimitExceeded,
          "descriptor rejects oversized name lengths before allocation");
}

void test_surface_and_policy_contracts() {
  constexpr std::uint64_t output = UINT64_C(0x0102030405060708);
  SurfaceOutputState surface{0x21, output,
                             {output, UINT64_C(0x0102030405060709)},
                             5, 4, 2, SurfaceScaleMode::ScaledPixmap, 9, 0};
  SurfaceOutputState decoded_surface;
  require(encode(surface).size() ==
              kSurfaceOutputStateFixedPayloadSize +
                  surface.output_ids.size() * sizeof(std::uint64_t) &&
              encode(surface) ==
              hex("21000000000000000807060504030201090000000000000005000000"
                  "040000000200000002000000000000000200000008070605"
                  "040302010907060504030201") &&
              decode(encode(surface), decoded_surface) == CodecStatus::Ok &&
              decoded_surface.output_ids.size() == 2,
          "SurfaceOutputState matches its exact bounded-array golden");

  PolicyOutputUpsert policy;
  policy.output_id = output;
  policy.logical_x = 640;
  policy.logical_width = 640;
  policy.logical_height = 480;
  policy.work_x = 640;
  policy.work_y = 20;
  policy.work_width = 640;
  policy.work_height = 460;
  policy.scale_numerator = 5;
  policy.scale_denominator = 4;
  policy.transform = Transform::Rotate90;
  policy.enabled = true;
  PolicyOutputUpsert decoded_policy;
  require(encode(policy).size() == kPolicyOutputPayloadSize &&
              encode(policy) ==
              hex("0807060504030201800200000000000080020000e001000080020000"
                  "1400000080020000cc01000005000000040000000100010000000000") &&
              decode(encode(policy), decoded_policy) == CodecStatus::Ok &&
              decoded_policy.work_y == 20,
          "PolicyOutputUpsert matches its exact work-area golden");

  PolicyWindowOutputHint hint{0x400001, 0, output, 0};
  PolicyWindowOutputHint decoded_hint;
  require(encode(hint).size() == kPolicyWindowOutputHintPayloadSize &&
              encode(hint) ==
              hex("010040000000000000000000000000000807060504030201") &&
              decode(encode(hint), decoded_hint) == CodecStatus::Ok,
          "PolicyWindowOutputHint matches its exact aligned golden");

  require_exact_framing(surface,
                        "surface state rejects all truncations and trailing data");
  require_exact_framing(policy,
                        "policy output rejects all truncations and trailing data");
  require_exact_framing(hint,
                        "window hint rejects all truncations and trailing data");
  require(decode(encode(PolicyWindowOutputHint{0x400001, 0, 0, 0}),
                 decoded_hint) == CodecStatus::Ok,
          "policy output hint permits absent previous and preferred outputs");

  auto invalid_surface = surface;
  invalid_surface.output_ids.push_back(output);
  require(decode(encode(invalid_surface), decoded_surface) ==
              CodecStatus::InvalidValue,
          "surface memberships reject duplicate output IDs");
  invalid_surface = surface;
  invalid_surface.output_ids.assign(kMaximumManagedOutputs + 1, output);
  require(encode(invalid_surface).empty(),
          "surface encoder refuses arrays above the output limit");
  invalid_surface = surface;
  invalid_surface.scale_mode = SurfaceScaleMode::Legacy;
  require(decode(encode(invalid_surface), decoded_surface) ==
              CodecStatus::InvalidValue,
          "legacy surfaces require client buffer scale one");

  auto invalid_policy = policy;
  invalid_policy.work_x = 639;
  require(decode(encode(invalid_policy), decoded_policy) ==
              CodecStatus::InvalidValue,
          "policy work rectangles stay within their logical output");
  auto invalid_hint = hint;
  invalid_hint.flags = 1;
  require(decode(encode(invalid_hint), decoded_hint) ==
              CodecStatus::InvalidValue,
          "policy hint flags are reserved zero");
  invalid_surface = surface;
  invalid_surface.flags = 1;
  require(decode(encode(invalid_surface), decoded_surface) ==
              CodecStatus::InvalidValue,
          "surface output flags are reserved zero");
  invalid_policy = policy;
  invalid_policy.flags = 1;
  require(decode(encode(invalid_policy), decoded_policy) ==
              CodecStatus::InvalidValue,
          "policy output flags are reserved zero");
  require_invalid_byte(surface, 38, 1,
                       "surface reserved16 field must be zero");
  require_invalid_byte(policy, 48, 8,
                       "policy output rejects unknown transforms");
  require_invalid_byte(policy, 50, 2,
                       "policy enabled flag is a canonical boolean");
  require_invalid_byte(hint, 4, 1,
                       "policy hint flags must be zero");
  auto excessive_count = encode(surface);
  excessive_count[44] = 9;
  require(decode(excessive_count, decoded_surface) ==
              CodecStatus::LimitExceeded,
          "surface decoder rejects oversized counts before allocation");
}

void test_control_contracts() {
  constexpr std::uint64_t output = UINT64_C(0x0102030405060708);
  OutputStateQuery query{0x31, kKnownOutputQueryFlags};
  OutputConfigurationCommit commit{0x41, 9, output, 0};
  OutputConfigurationAcknowledged acknowledged{
      0x41, 10, OutputConfigurationResult::Accepted, 0, output, 1280, 480, 2};
  OutputStateQuery decoded_query;
  OutputConfigurationCommit decoded_commit;
  OutputConfigurationAcknowledged decoded_acknowledged;
  require(encode(query).size() == kOutputStateQueryPayloadSize &&
              encode(query) == hex("31000000000000001f00000000000000") &&
              decode(encode(query), decoded_query) == CodecStatus::Ok,
          "OutputStateQuery matches its exact flag golden");
  require(encode(commit).size() == kOutputConfigurationCommitPayloadSize &&
              encode(commit) ==
              hex("41000000000000000900000000000000080706050403020100000000"
                  "00000000") &&
              decode(encode(commit), decoded_commit) == CodecStatus::Ok,
          "OutputConfigurationCommit matches its exact correlation golden");
  require(encode(acknowledged).size() ==
              kOutputConfigurationAcknowledgedPayloadSize &&
              encode(acknowledged) ==
              hex("41000000000000000a00000000000000010000000000000008070605"
                  "0403020100050000e00100000200000000000000") &&
              decode(encode(acknowledged), decoded_acknowledged) ==
                  CodecStatus::Ok &&
              decoded_acknowledged.enabled_output_count == 2,
          "OutputConfigurationAcknowledged matches its exact result golden");
  for (const auto result : {OutputConfigurationResult::UnsupportedVrr,
                            OutputConfigurationResult::VrrPolicyRejected,
                            OutputConfigurationResult::VrrPresenterRejected}) {
    acknowledged.result = result;
    require(decode(encode(acknowledged), decoded_acknowledged) ==
                    CodecStatus::Ok &&
                decoded_acknowledged.result == result,
            "M14 output configuration results round-trip");
  }
  acknowledged.result = OutputConfigurationResult::Accepted;

  require_exact_framing(query,
                        "query rejects every truncation and trailing byte");
  require_exact_framing(commit,
                        "commit rejects every truncation and trailing byte");
  require_exact_framing(
      acknowledged,
      "acknowledgement rejects every truncation and trailing byte");

  query.flags = 0;
  require(decode(encode(query), decoded_query) == CodecStatus::InvalidValue,
          "query requires at least one known inventory flag");
  commit.base_generation = 0;
  require(decode(encode(commit), decoded_commit) == CodecStatus::InvalidValue,
          "configuration commit requires a base generation");
  acknowledged.result = static_cast<OutputConfigurationResult>(16);
  require(decode(encode(acknowledged), decoded_acknowledged) ==
              CodecStatus::InvalidValue,
          "acknowledgement rejects unknown results");
  query = {0x31, UINT32_C(0x20)};
  require(decode(encode(query), decoded_query) == CodecStatus::InvalidValue,
          "query rejects unknown inventory flags");
  commit = {0x41, 9, output, 1};
  require(decode(encode(commit), decoded_commit) == CodecStatus::InvalidValue,
          "configuration commit flags are reserved zero");
  acknowledged = {0x41, 10, OutputConfigurationResult::Accepted,
                  1, output, 1280, 480, 2};
  require(decode(encode(acknowledged), decoded_acknowledged) ==
              CodecStatus::InvalidValue,
          "acknowledgement flags are reserved zero");
  require_invalid_byte(OutputStateQuery{0x31, kKnownOutputQueryFlags}, 12, 1,
                       "query reserved word must be zero");
  require_invalid_byte(OutputConfigurationCommit{0x41, 9, output, 0}, 28, 1,
                       "commit reserved word must be zero");
  require_invalid_byte(
      OutputConfigurationAcknowledged{
          0x41, 10, OutputConfigurationResult::Accepted, 0, output, 1280, 480,
          2},
      18, 1, "acknowledgement reserved16 field must be zero");
  require_invalid_byte(
      OutputConfigurationAcknowledged{
          0x41, 10, OutputConfigurationResult::Accepted, 0, output, 1280, 480,
          2},
      44, 1, "acknowledgement reserved32 field must be zero");
  require(kOutputContractFdCount == 0,
          "every output-management payload carries exactly zero FDs");
}

}  // namespace

int main() {
  test_inventory_contracts();
  test_surface_and_policy_contracts();
  test_control_contracts();
  return 0;
}
