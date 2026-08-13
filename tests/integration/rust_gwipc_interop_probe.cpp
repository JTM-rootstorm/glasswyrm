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
    Fixture{"ping", "ping.hex", true},
    Fixture{"pong", "pong.hex", true},
    Fixture{"protocol-error", "protocol-error.hex", true},
    Fixture{"snapshot-begin", "snapshot-begin.hex", true},
    Fixture{"snapshot-end", "snapshot-end.hex", true},
    Fixture{"snapshot-abort", "snapshot-abort.hex", true},
    Fixture{"output-upsert", "output-upsert.hex", true},
    Fixture{"output-remove", "output-remove.hex", true},
    Fixture{"surface-upsert", "surface-upsert.hex", true},
    Fixture{"surface-remove", "surface-remove.hex", true},
    Fixture{"buffer-attach", "buffer-attach.hex", true},
    Fixture{"buffer-detach", "buffer-detach.hex", true},
    Fixture{"buffer-release", "buffer-release.hex", true},
    Fixture{"surface-damage", "surface-damage.hex", true},
    Fixture{"frame-commit", "frame-commit.hex", true},
    Fixture{"frame-acknowledged", "frame-acknowledged.hex", true},
    Fixture{"output-descriptor-upsert", "output-descriptor-upsert.hex", true},
    Fixture{"output-mode-upsert", "output-mode-upsert.hex", true},
    Fixture{"surface-output-state", "surface-output-state.hex", true},
    Fixture{"policy-output-upsert", "policy-output-upsert.hex", true},
    Fixture{"policy-window-output-hint", "policy-window-output-hint.hex", true},
    Fixture{"output-state-query", "output-state-query.hex", true},
    Fixture{"output-configuration-commit", "output-configuration-commit.hex",
            true},
    Fixture{"policy-window-upsert", "policy-window-upsert.hex", true},
    Fixture{"policy-context-upsert", "policy-context-upsert.hex", true},
    Fixture{"policy-window-remove", "policy-window-remove.hex", true},
    Fixture{"policy-commit", "policy-commit.hex", true},
    Fixture{"policy-window-state", "policy-window-state.hex", true},
    Fixture{"policy-acknowledged", "policy-acknowledged.hex", true},
    Fixture{"policy-bindings-upsert", "policy-bindings-upsert.hex", true},
    Fixture{"policy-lifecycle-window-upsert",
            "policy-lifecycle-window-upsert.hex", true},
    Fixture{"surface-policy-upsert", "surface-policy-upsert.hex", true},
    Fixture{"synthetic-motion", "synthetic-motion.hex", true},
    Fixture{"synthetic-button", "synthetic-button.hex", true},
    Fixture{"synthetic-key", "synthetic-key.hex", true},
    Fixture{"synthetic-barrier", "synthetic-barrier.hex", true},
    Fixture{"synthetic-input-acknowledged", "synthetic-input-acknowledged.hex",
            true},
    Fixture{"session-state-change", "session-state-change.hex", true},
    Fixture{"session-state-acknowledged", "session-state-acknowledged.hex",
            true},
    Fixture{"output-configuration-acknowledged",
            "output-configuration-acknowledged.hex", true},
    Fixture{"output-vrr-capability-upsert", "output-vrr-capability-upsert.hex",
            true},
    Fixture{"output-vrr-policy-upsert", "output-vrr-policy-upsert.hex", true},
    Fixture{"output-vrr-state-upsert", "output-vrr-state-upsert.hex", true},
    Fixture{"surface-vrr-state", "surface-vrr-state.hex", true},
    Fixture{"policy-window-vrr-upsert", "policy-window-vrr-upsert.hex", true},
    Fixture{"policy-output-vrr-upsert", "policy-output-vrr-upsert.hex", true},
    Fixture{"policy-window-vrr-state", "policy-window-vrr-state.hex", true},
    Fixture{"policy-output-vrr-state", "policy-output-vrr-state.hex", true},
    Fixture{"presentation-timing", "presentation-timing.hex", true},
    Fixture{"malformed-envelope-magic", "malformed-envelope-magic.hex", false},
    Fixture{"malformed-hello-truncated", "malformed-hello-truncated.hex",
            false},
    Fixture{"malformed-vrr-capability-boolean",
            "malformed-vrr-capability-boolean.hex", false},
    Fixture{"rust-malformed-synthetic-button-boolean",
            "rust-malformed-synthetic-button-boolean.hex", false},
    Fixture{"rust-malformed-surface-output-count",
            "rust-malformed-surface-output-count.hex", false},
    Fixture{"rust-malformed-policy-vrr-reserved",
            "rust-malformed-policy-vrr-reserved.hex", false},
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

SurfaceUpsert surface_upsert() {
  SurfaceUpsert value;
  value.surface_id = 10;
  value.x11_window_id = 20;
  value.output_id = 9;
  value.logical_x = 1;
  value.logical_y = 2;
  value.logical_width = 640;
  value.logical_height = 480;
  value.visible = true;
  value.clip_width = 640;
  value.clip_height = 480;
  value.opacity = kOpacityOne;
  value.color = color();
  return value;
}

PolicyWindowState policy_window_state() {
  PolicyWindowState value;
  value.window_id = 10;
  value.workspace_id = 2;
  value.output_id = 9;
  value.final_x = 1;
  value.final_y = 2;
  value.final_width = 100;
  value.final_height = 80;
  value.window_type = PolicyWindowType::Normal;
  value.applied_state = PolicyAppliedState::Normal;
  value.visible = true;
  value.focused = true;
  value.managed = true;
  value.decoration_eligible = true;
  value.fullscreen_eligible = 2;
  return value;
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
  if (name == "ping")
    return encode(Ping{0x11});
  if (name == "pong")
    return encode(Pong{0x12});
  if (name == "protocol-error")
    return encode(ProtocolError{ProtocolErrorCode::MalformedPayload,
                                MessageType::Hello, 0x13, "bad payload"});
  if (name == "snapshot-begin")
    return encode(SnapshotBegin{0x21, SnapshotDomain::Test, 0, 0x22, 3});
  if (name == "snapshot-end")
    return encode(SnapshotEnd{0x21, 0x22, 3});
  if (name == "snapshot-abort")
    return encode(SnapshotAbort{0x21, 1, "cancelled"});
  if (name == "output-upsert")
    return encode(output_upsert());
  if (name == "output-remove")
    return encode(OutputRemove{9});
  if (name == "surface-upsert")
    return encode(surface_upsert());
  if (name == "surface-remove")
    return encode(SurfaceRemove{10});
  if (name == "buffer-attach")
    return encode(buffer_attach());
  if (name == "buffer-detach")
    return encode(BufferDetach{10, 20});
  if (name == "buffer-release")
    return encode(BufferRelease{20, BufferReleaseReason::ConsumerDone});
  if (name == "surface-damage")
    return encode(SurfaceDamage{10, {{-1, 2, 30, 40}, {50, 60, 70, 80}}});
  if (name == "frame-commit")
    return encode(FrameCommit{0x31, 9, 0x32, 0});
  if (name == "frame-acknowledged")
    return encode(FrameAcknowledged{0x31, 9, 0x32, FrameResult::Accepted});
  if (name == "output-descriptor-upsert")
    return encode(output_descriptor());
  if (name == "output-mode-upsert")
    return encode(OutputModeUpsert{9, 10, 1920, 1080, 60000, true, true, 0});
  if (name == "surface-output-state" ||
      name == "rust-malformed-surface-output-count") {
    auto bytes = encode(SurfaceOutputState{
        10, 9, {9, 11}, 2, 1, 2, SurfaceScaleMode::ScaledPixmap, 3, 0});
    if (name == "rust-malformed-surface-output-count")
      bytes[44] = 9;
    return bytes;
  }
  if (name == "policy-output-upsert")
    return encode(PolicyOutputUpsert{9, 0, 0, 1920, 1080, 0, 0, 1920, 1040, 1,
                                     1, Transform::Normal, true, true, 0});
  if (name == "policy-window-output-hint")
    return encode(PolicyWindowOutputHint{10, 9, 11, 0});
  if (name == "output-state-query")
    return encode(
        OutputStateQuery{0x41, kQueryOutputDescriptors | kQueryOutputLayout});
  if (name == "output-configuration-commit")
    return encode(OutputConfigurationCommit{0x41, 10, 9, 0});
  if (name == "policy-window-upsert")
    return encode(policy_window());
  if (name == "policy-context-upsert")
    return encode(PolicyContextUpsert{1, 2, 9, 0, 0, 1920, 1040, 0});
  if (name == "policy-window-remove")
    return encode(PolicyWindowRemove{10});
  if (name == "policy-commit")
    return encode(PolicyCommit{20, 2, 0});
  if (name == "policy-window-state")
    return encode(policy_window_state());
  if (name == "policy-acknowledged")
    return encode(PolicyAcknowledged{20, 2, 3, 0x0102030405060708, 1,
                                     PolicyResult::Accepted});
  if (name == "policy-bindings-upsert")
    return encode(
        PolicyBindingsUpsert{8, 8, 8, 1, 3, 0xffc1, 96, 64, true, true});
  if (name == "policy-lifecycle-window-upsert")
    return encode(lifecycle_window());
  if (name == "surface-policy-upsert")
    return encode(SurfacePolicyUpsert{10, 20, 2, PolicyWindowType::Dialog,
                                      PolicyAppliedState::Normal, true, true,
                                      true, false, false, 2, 0, 0});
  if (name == "synthetic-motion")
    return encode(SyntheticMotion{7, 11, -2, 300, 0});
  if (name == "synthetic-button" ||
      name == "rust-malformed-synthetic-button-boolean") {
    auto bytes = encode(SyntheticButton{7, 11, 1, 1, 0, 0});
    if (name == "rust-malformed-synthetic-button-boolean")
      bytes[13] = 2;
    return bytes;
  }
  if (name == "synthetic-key")
    return encode(SyntheticKey{7, 11, 38, 1, 0, 0});
  if (name == "synthetic-barrier")
    return encode(SyntheticBarrier{7, 0});
  if (name == "synthetic-input-acknowledged")
    return encode(SyntheticInputAcknowledged{
        7, 11, SyntheticInputResult::Accepted, -2, 300, 20, 21, 0, 0, 2, 0});
  if (name == "session-state-change")
    return encode(SessionStateChange{UINT64_C(0x0102030405060708),
                                     SessionState::Active, 0});
  if (name == "session-state-acknowledged")
    return encode(SessionStateAcknowledged{
        9, SessionState::Inactive, SessionStateResult::InputUnavailable, 0});
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
  if (name == "output-vrr-policy-upsert")
    return encode(OutputVrrPolicyUpsert{9, VrrPolicyMode::Focused, 0});
  if (name == "output-vrr-state-upsert")
    return encode(OutputVrrStateUpsert{
        9, VrrPolicyMode::Focused, VrrDecision::Enabled, true, true, true, true,
        10, 10, 0, 1, 2, 3, 4, 5, 0, 6, 7});
  if (name == "surface-vrr-state")
    return encode(SurfaceVrrState{10, 20, 9, VrrWindowPreference::Prefer, true,
                                  true, true, true, true, true, 0, 2, 0});
  if (name == "policy-window-vrr-upsert")
    return encode(PolicyWindowVrrUpsert{20, VrrWindowPreference::Allow, 0});
  if (name == "policy-output-vrr-upsert")
    return encode(
        PolicyOutputVrrUpsert{9, VrrPolicyMode::Focused, true, true, 0});
  if (name == "policy-window-vrr-state")
    return encode(PolicyWindowVrrState{20, 9, VrrWindowPreference::Prefer, true,
                                       true, true, true, true, true, 0, 0});
  if (name == "policy-output-vrr-state" ||
      name == "rust-malformed-policy-vrr-reserved") {
    auto bytes = encode(
        PolicyOutputVrrState{9, VrrPolicyMode::Focused, 20, true, true, 0, 0});
    if (name == "rust-malformed-policy-vrr-reserved")
      bytes[28] = 1;
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
  if (name == "ping")
    return canonical_decode<Ping>(bytes);
  if (name == "pong")
    return canonical_decode<Pong>(bytes);
  if (name == "protocol-error")
    return canonical_decode<ProtocolError>(bytes);
  if (name == "snapshot-begin")
    return canonical_decode<SnapshotBegin>(bytes);
  if (name == "snapshot-end")
    return canonical_decode<SnapshotEnd>(bytes);
  if (name == "snapshot-abort")
    return canonical_decode<SnapshotAbort>(bytes);
  if (name == "output-upsert")
    return canonical_decode<OutputUpsert>(bytes);
  if (name == "output-remove")
    return canonical_decode<OutputRemove>(bytes);
  if (name == "surface-upsert")
    return canonical_decode<SurfaceUpsert>(bytes);
  if (name == "surface-remove")
    return canonical_decode<SurfaceRemove>(bytes);
  if (name == "buffer-attach")
    return canonical_decode<BufferAttach>(bytes);
  if (name == "buffer-detach")
    return canonical_decode<BufferDetach>(bytes);
  if (name == "buffer-release")
    return canonical_decode<BufferRelease>(bytes);
  if (name == "surface-damage")
    return canonical_decode<SurfaceDamage>(bytes);
  if (name == "frame-commit")
    return canonical_decode<FrameCommit>(bytes);
  if (name == "frame-acknowledged")
    return canonical_decode<FrameAcknowledged>(bytes);
  if (name == "output-descriptor-upsert")
    return canonical_decode<OutputDescriptorUpsert>(bytes);
  if (name == "output-mode-upsert")
    return canonical_decode<OutputModeUpsert>(bytes);
  if (name == "surface-output-state" ||
      name == "rust-malformed-surface-output-count")
    return canonical_decode<SurfaceOutputState>(bytes);
  if (name == "policy-output-upsert")
    return canonical_decode<PolicyOutputUpsert>(bytes);
  if (name == "policy-window-output-hint")
    return canonical_decode<PolicyWindowOutputHint>(bytes);
  if (name == "output-state-query")
    return canonical_decode<OutputStateQuery>(bytes);
  if (name == "output-configuration-commit")
    return canonical_decode<OutputConfigurationCommit>(bytes);
  if (name == "policy-window-upsert")
    return canonical_decode<PolicyWindowUpsert>(bytes);
  if (name == "policy-context-upsert")
    return canonical_decode<PolicyContextUpsert>(bytes);
  if (name == "policy-window-remove")
    return canonical_decode<PolicyWindowRemove>(bytes);
  if (name == "policy-commit")
    return canonical_decode<PolicyCommit>(bytes);
  if (name == "policy-window-state")
    return canonical_decode<PolicyWindowState>(bytes);
  if (name == "policy-acknowledged")
    return canonical_decode<PolicyAcknowledged>(bytes);
  if (name == "policy-bindings-upsert")
    return canonical_decode<PolicyBindingsUpsert>(bytes);
  if (name == "policy-lifecycle-window-upsert")
    return canonical_decode<PolicyLifecycleWindowUpsert>(bytes);
  if (name == "surface-policy-upsert")
    return canonical_decode<SurfacePolicyUpsert>(bytes);
  if (name == "synthetic-motion")
    return canonical_decode<SyntheticMotion>(bytes);
  if (name == "synthetic-button" ||
      name == "rust-malformed-synthetic-button-boolean")
    return canonical_decode<SyntheticButton>(bytes);
  if (name == "synthetic-key")
    return canonical_decode<SyntheticKey>(bytes);
  if (name == "synthetic-barrier")
    return canonical_decode<SyntheticBarrier>(bytes);
  if (name == "synthetic-input-acknowledged")
    return canonical_decode<SyntheticInputAcknowledged>(bytes);
  if (name == "session-state-change")
    return canonical_decode<SessionStateChange>(bytes);
  if (name == "session-state-acknowledged")
    return canonical_decode<SessionStateAcknowledged>(bytes);
  if (name == "output-configuration-acknowledged")
    return canonical_decode<OutputConfigurationAcknowledged>(bytes);
  if (name == "output-vrr-capability-upsert" ||
      name == "malformed-vrr-capability-boolean")
    return canonical_decode<OutputVrrCapabilityUpsert>(bytes);
  if (name == "output-vrr-policy-upsert")
    return canonical_decode<OutputVrrPolicyUpsert>(bytes);
  if (name == "output-vrr-state-upsert")
    return canonical_decode<OutputVrrStateUpsert>(bytes);
  if (name == "surface-vrr-state")
    return canonical_decode<SurfaceVrrState>(bytes);
  if (name == "policy-window-vrr-upsert")
    return canonical_decode<PolicyWindowVrrUpsert>(bytes);
  if (name == "policy-output-vrr-upsert")
    return canonical_decode<PolicyOutputVrrUpsert>(bytes);
  if (name == "policy-window-vrr-state")
    return canonical_decode<PolicyWindowVrrState>(bytes);
  if (name == "policy-output-vrr-state" ||
      name == "rust-malformed-policy-vrr-reserved")
    return canonical_decode<PolicyOutputVrrState>(bytes);
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
