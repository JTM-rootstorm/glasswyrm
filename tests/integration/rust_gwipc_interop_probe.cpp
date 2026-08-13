#include "ipc/wire/compositor_contract.hpp"
#include "ipc/wire/control.hpp"
#include "ipc/wire/envelope.hpp"
#include "ipc/wire/input_contract.hpp"
#include "ipc/wire/lifecycle_contract.hpp"
#include "ipc/wire/output_contract.hpp"
#include "ipc/wire/policy_contract.hpp"
#include "ipc/wire/session_contract.hpp"
#include "ipc/wire/vrr_contract.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace gw::ipc::wire;
using Bytes = std::vector<std::uint8_t>;

struct Fixture {
  std::string_view name;
  std::string_view file;
  bool expected_decode;
};

constexpr std::array kFixtures{
    Fixture{"pong-record", "pong-record.hex", true},
    Fixture{"hello", "hello.hex", true},
    Fixture{"welcome", "welcome.hex", true},
    Fixture{"reject", "reject.hex", true},
    Fixture{"output-upsert", "output-upsert.hex", true},
    Fixture{"buffer-attach", "buffer-attach.hex", true},
    Fixture{"output-descriptor-upsert", "output-descriptor-upsert.hex", true},
    Fixture{"policy-window-upsert", "policy-window-upsert.hex", true},
    Fixture{"policy-lifecycle-window-upsert",
            "policy-lifecycle-window-upsert.hex", true},
    Fixture{"synthetic-motion", "synthetic-motion.hex", true},
    Fixture{"session-state-change", "session-state-change.hex", true},
    Fixture{"output-configuration-acknowledged",
            "output-configuration-acknowledged.hex", true},
    Fixture{"output-vrr-capability-upsert", "output-vrr-capability-upsert.hex",
            true},
    Fixture{"presentation-timing", "presentation-timing.hex", true},
    Fixture{"malformed-envelope-magic", "malformed-envelope-magic.hex", false},
    Fixture{"malformed-hello-truncated", "malformed-hello-truncated.hex",
            false},
    Fixture{"malformed-vrr-capability-boolean",
            "malformed-vrr-capability-boolean.hex", false},
};

SdrColorMetadata color() {
  return {SdrColorSpace::Srgb,
          TransferFunction::Srgb,
          ColorPrimaries::Srgb,
          true,
          1,
          100000,
          50000};
}

Hello hello() {
  Hello value;
  value.sender_role = Role::TestProducer;
  value.offered_capabilities = UINT64_C(0x0102030405060708);
  value.required_capabilities =
      static_cast<std::uint64_t>(Capability::Snapshots);
  value.maximum_payload = 65536;
  value.maximum_fd_count = 4;
  for (std::size_t index = 0; index < value.sender_instance_id.size(); ++index)
    value.sender_instance_id[index] = static_cast<std::uint8_t>(index + 1);
  value.name = "producer";
  return value;
}

Welcome welcome() {
  Welcome value;
  value.sender_role = Role::TestConsumer;
  value.negotiated_capabilities = 2;
  value.negotiated_maximum_payload = 32768;
  value.negotiated_maximum_fd_count = 2;
  value.connection_id = 99;
  value.sender_instance_id.fill(0xa5);
  return value;
}

OutputUpsert output_upsert() {
  return {9,      true, -10,   20, 1920, 1080,
          3840,   2160, 60000, 2,  1,    Transform::Normal,
          color()};
}

BufferAttach buffer_attach() {
  BufferAttach value;
  value.buffer_id = 20;
  value.surface_id = 10;
  value.width = 64;
  value.height = 32;
  value.stride = 256;
  value.byte_offset = 128;
  value.storage_size = 128 + 256 * 32;
  value.pixel_format = PixelFormat::Argb8888;
  value.alpha_semantics = AlphaSemantics::Premultiplied;
  value.color = color();
  return value;
}

OutputDescriptorUpsert output_descriptor() {
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

PolicyWindowUpsert policy_window() {
  return {10,
          1,
          0,
          2,
          -3,
          4,
          100,
          80,
          1,
          PolicyWindowType::Dialog,
          PolicyMapIntent::WantsMap,
          false,
          2,
          true,
          false,
          false,
          true,
          11,
          12,
          13,
          0};
}

PolicyLifecycleWindowUpsert lifecycle_window() {
  return {policy_window(), 14, 15, 16, PolicyStackMode::Above, 0};
}

OutputVrrCapabilityUpsert vrr_capability() {
  OutputVrrCapabilityUpsert value;
  value.output_id = UINT64_C(0x0102030405060708);
  value.connector_property_present = true;
  value.hardware_capable = true;
  value.kms_controllable = true;
  value.range_available = true;
  value.atomic_required = true;
  value.minimum_refresh_millihertz = 40000;
  value.maximum_refresh_millihertz = 144000;
  value.reason_flags = UINT64_C(1) << 3U;
  return value;
}

template <typename Value> bool canonical_decode(const Bytes &bytes) {
  Value value;
  return decode(bytes, value) == CodecStatus::Ok && encode(value) == bytes;
}

Bytes encoded(std::string_view name) {
  if (name == "hello" || name == "malformed-hello-truncated") {
    auto bytes = encode(hello());
    if (name == "malformed-hello-truncated")
      bytes.pop_back();
    return bytes;
  }
  if (name == "welcome")
    return encode(welcome());
  if (name == "reject")
    return encode(Reject{RejectReason::RoleNotAllowed, 1, 0, 1, 0, "role"});
  if (name == "output-upsert")
    return encode(output_upsert());
  if (name == "buffer-attach")
    return encode(buffer_attach());
  if (name == "output-descriptor-upsert")
    return encode(output_descriptor());
  if (name == "policy-window-upsert")
    return encode(policy_window());
  if (name == "policy-lifecycle-window-upsert")
    return encode(lifecycle_window());
  if (name == "synthetic-motion")
    return encode(SyntheticMotion{7, 11, -2, 300, 0});
  if (name == "session-state-change")
    return encode(SessionStateChange{UINT64_C(0x0102030405060708),
                                     SessionState::Active, 0});
  if (name == "output-configuration-acknowledged")
    return encode(OutputConfigurationAcknowledged{
        0x41, 10, OutputConfigurationResult::Accepted, 0,
        UINT64_C(0x0102030405060708), 1280, 480, 2});
  if (name == "output-vrr-capability-upsert" ||
      name == "malformed-vrr-capability-boolean") {
    auto bytes = encode(vrr_capability());
    if (name == "malformed-vrr-capability-boolean")
      bytes[8] = 2;
    return bytes;
  }
  if (name == "presentation-timing")
    return encode(PresentationTiming{UINT64_C(0x0102030405060708), 0x11, 0x12,
                                     0x13, kPresentationTimingSimulated, 0x14,
                                     0x15, true, true});
  if (name == "pong-record" || name == "malformed-envelope-magic") {
    const auto payload = encode(Pong{UINT64_C(0xa5a5a5a5a5a5a5a5)});
    const Envelope envelope{1,
                            0,
                            MessageType::Pong,
                            static_cast<std::uint32_t>(MessageFlag::Reply),
                            static_cast<std::uint32_t>(payload.size()),
                            0,
                            UINT64_C(0x0102030405060708),
                            UINT64_C(0x1112131415161718)};
    const auto header = encode_envelope(envelope);
    Bytes bytes(header.begin(), header.end());
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    if (name == "malformed-envelope-magic")
      bytes[0] = 'X';
    return bytes;
  }
  return {};
}

bool decoded(std::string_view name, const Bytes &bytes) {
  if (name == "hello" || name == "malformed-hello-truncated")
    return canonical_decode<Hello>(bytes);
  if (name == "welcome")
    return canonical_decode<Welcome>(bytes);
  if (name == "reject")
    return canonical_decode<Reject>(bytes);
  if (name == "output-upsert")
    return canonical_decode<OutputUpsert>(bytes);
  if (name == "buffer-attach")
    return canonical_decode<BufferAttach>(bytes);
  if (name == "output-descriptor-upsert")
    return canonical_decode<OutputDescriptorUpsert>(bytes);
  if (name == "policy-window-upsert")
    return canonical_decode<PolicyWindowUpsert>(bytes);
  if (name == "policy-lifecycle-window-upsert")
    return canonical_decode<PolicyLifecycleWindowUpsert>(bytes);
  if (name == "synthetic-motion")
    return canonical_decode<SyntheticMotion>(bytes);
  if (name == "session-state-change")
    return canonical_decode<SessionStateChange>(bytes);
  if (name == "output-configuration-acknowledged")
    return canonical_decode<OutputConfigurationAcknowledged>(bytes);
  if (name == "output-vrr-capability-upsert" ||
      name == "malformed-vrr-capability-boolean")
    return canonical_decode<OutputVrrCapabilityUpsert>(bytes);
  if (name == "presentation-timing")
    return canonical_decode<PresentationTiming>(bytes);
  if (name == "pong-record" || name == "malformed-envelope-magic") {
    Envelope envelope;
    if (decode_envelope(bytes, 0, 65536, envelope) != CodecStatus::Ok ||
        envelope.type != MessageType::Pong ||
        bytes.size() != kEnvelopeSize + envelope.payload_size)
      return false;
    const auto payload = std::span(bytes).subspan(kEnvelopeSize);
    Pong pong;
    if (decode(payload, pong) != CodecStatus::Ok)
      return false;
    const auto encoded_payload = encode(pong);
    return encoded_payload.size() == payload.size() &&
           std::equal(encoded_payload.begin(), encoded_payload.end(),
                      payload.begin());
  }
  return false;
}

const Fixture *fixture(std::string_view name) {
  for (const auto &candidate : kFixtures)
    if (candidate.name == name)
      return &candidate;
  return nullptr;
}

bool read_hex(const std::filesystem::path &path, Bytes &bytes) {
  std::ifstream input(path, std::ios::binary);
  if (!input)
    return false;
  const std::string text((std::istreambuf_iterator<char>(input)), {});
  if (input.bad() || text.empty())
    return false;
  const std::string_view hex =
      text.back() == '\n' ? std::string_view(text).substr(0, text.size() - 1)
                          : std::string_view(text);
  if (hex.empty() || hex.size() % 2 != 0 || hex.find('\n') != hex.npos ||
      hex.find('\r') != hex.npos)
    return false;
  const auto digit = [](char value) -> int {
    if (value >= '0' && value <= '9')
      return value - '0';
    if (value >= 'a' && value <= 'f')
      return value - 'a' + 10;
    return -1;
  };
  bytes.clear();
  bytes.reserve(hex.size() / 2);
  for (std::size_t index = 0; index < hex.size(); index += 2) {
    const int high = digit(hex[index]);
    const int low = digit(hex[index + 1]);
    if (high < 0 || low < 0)
      return false;
    bytes.push_back(static_cast<std::uint8_t>((high << 4) | low));
  }
  return true;
}

bool write_hex(const std::filesystem::path &path, const Bytes &bytes) {
  static constexpr char digits[] = "0123456789abcdef";
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output)
    return false;
  for (const auto byte : bytes)
    output << digits[byte >> 4U] << digits[byte & 0xfU];
  output << '\n';
  return output.good();
}

int verify(const std::filesystem::path &root) {
  for (const auto &entry : kFixtures) {
    Bytes fixture_bytes;
    if (!read_hex(root / entry.file, fixture_bytes)) {
      std::cerr << entry.file << ": invalid strict lowercase hex fixture\n";
      return 1;
    }
    if (fixture_bytes != encoded(entry.name)) {
      std::cerr << entry.file << ": differs from legacy encoder\n";
      return 1;
    }
    if (decoded(entry.name, fixture_bytes) != entry.expected_decode) {
      std::cerr << entry.file << ": unexpected legacy decode result\n";
      return 1;
    }
  }
  return 0;
}

} // namespace

int main(int argc, char **argv) {
  if (argc == 3 && std::string_view(argv[1]) == "verify")
    return verify(argv[2]);
  if (argc != 4) {
    std::cerr << "usage: rust_gwipc_interop_probe "
                 "encode|decode NAME PATH\n"
                 "       rust_gwipc_interop_probe verify FIXTURE_ROOT\n";
    return 2;
  }
  const std::string_view operation = argv[1];
  const std::string_view name = argv[2];
  if (fixture(name) == nullptr) {
    std::cerr << "unknown fixture: " << name << '\n';
    return 2;
  }
  if (operation == "encode") {
    const auto bytes = encoded(name);
    if (bytes.empty() || !write_hex(argv[3], bytes)) {
      std::cerr << "could not write encoded fixture\n";
      return 1;
    }
    return 0;
  }
  if (operation == "decode") {
    Bytes bytes;
    if (!read_hex(argv[3], bytes)) {
      std::cerr << "invalid strict lowercase hex input\n";
      return 1;
    }
    if (!decoded(name, bytes)) {
      std::cerr << "legacy decoder rejected " << name << '\n';
      return 1;
    }
    return 0;
  }
  std::cerr << "unknown operation: " << operation << '\n';
  return 2;
}
