#pragma once

#include "ipc/wire/types.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace gw::ipc::wire {

inline constexpr std::size_t kOutputVrrCapabilityPayloadSize = 40;
inline constexpr std::size_t kOutputVrrPolicyPayloadSize = 16;
inline constexpr std::size_t kOutputVrrStatePayloadSize = 96;
inline constexpr std::size_t kSurfaceVrrStatePayloadSize = 56;
inline constexpr std::size_t kPolicyWindowVrrUpsertPayloadSize = 16;
inline constexpr std::size_t kPolicyOutputVrrUpsertPayloadSize = 16;
inline constexpr std::size_t kPolicyWindowVrrStatePayloadSize = 40;
inline constexpr std::size_t kPolicyOutputVrrStatePayloadSize = 32;
inline constexpr std::size_t kPresentationTimingPayloadSize = 56;
inline constexpr std::uint16_t kVrrContractFdCount = 0;

enum class VrrPolicyMode : std::uint16_t {
  Off = 1,
  Fullscreen,
  Focused,
  AppRequested,
  AlwaysEligible,
};

enum class VrrWindowPreference : std::uint16_t {
  Default = 0,
  Disable,
  Allow,
  Prefer,
};

enum class VrrDecision : std::uint16_t {
  Disabled = 1,
  Enabled,
  Unsupported,
  Rejected,
};

inline constexpr std::uint64_t kKnownVrrReasonMask =
    (UINT64_C(1) << 33U) - UINT64_C(1);
inline constexpr std::uint64_t kVrrReasonSimulatedHeadless = UINT64_C(1) << 31U;
inline constexpr std::uint32_t kPresentationTimingSimulated = UINT32_C(1) << 0U;

struct OutputVrrCapabilityUpsert {
  std::uint64_t output_id{};
  bool connector_property_present{};
  bool hardware_capable{};
  bool kms_controllable{};
  bool simulated{};
  bool range_available{};
  bool atomic_required{};
  std::uint32_t minimum_refresh_millihertz{};
  std::uint32_t maximum_refresh_millihertz{};
  std::uint64_t reason_flags{};
  std::uint32_t flags{};
};

struct OutputVrrPolicyUpsert {
  std::uint64_t output_id{};
  VrrPolicyMode mode{VrrPolicyMode::Off};
  std::uint32_t flags{};
};

struct OutputVrrStateUpsert {
  std::uint64_t output_id{};
  VrrPolicyMode requested_mode{VrrPolicyMode::Off};
  VrrDecision decision{VrrDecision::Disabled};
  bool desired_enabled{};
  bool effective_enabled{};
  bool property_readback_valid{};
  bool session_active{};
  std::uint32_t candidate_window_id{};
  std::uint64_t candidate_surface_id{};
  std::uint64_t reason_flags{};
  std::uint64_t state_generation{};
  std::uint64_t transition_serial{};
  std::uint64_t last_commit_id{};
  std::uint64_t last_presented_generation{};
  std::uint32_t last_flip_sequence{};
  std::uint32_t flags{};
  std::uint64_t last_flip_timestamp_nanoseconds{};
  std::uint64_t last_interval_nanoseconds{};
};

struct SurfaceVrrState {
  std::uint64_t surface_id{};
  std::uint32_t window_id{};
  std::uint64_t output_id{};
  VrrWindowPreference preference{VrrWindowPreference::Default};
  bool policy_selected{};
  bool policy_eligible{};
  bool focused{};
  bool fullscreen{};
  bool borderless_fullscreen{};
  bool exclusive_output_membership{};
  std::uint64_t reason_flags{};
  std::uint64_t policy_generation{};
  std::uint32_t flags{};
};

struct PolicyWindowVrrUpsert {
  std::uint32_t window_id{};
  VrrWindowPreference preference{VrrWindowPreference::Default};
  std::uint32_t flags{};
};

struct PolicyOutputVrrUpsert {
  std::uint64_t output_id{};
  VrrPolicyMode mode{VrrPolicyMode::Off};
  bool hardware_capable{};
  bool kms_controllable{};
  std::uint32_t flags{};
};

struct PolicyWindowVrrState {
  std::uint32_t window_id{};
  std::uint64_t output_id{};
  VrrWindowPreference preference{VrrWindowPreference::Default};
  bool selected{};
  bool eligible{};
  bool focused{};
  bool fullscreen{};
  bool borderless_fullscreen{};
  bool exclusive_output_membership{};
  std::uint64_t reason_flags{};
  std::uint32_t flags{};
};

struct PolicyOutputVrrState {
  std::uint64_t output_id{};
  VrrPolicyMode mode{VrrPolicyMode::Off};
  std::uint32_t selected_window_id{};
  bool desired_enabled{};
  bool candidate_required{};
  std::uint64_t reason_flags{};
  std::uint32_t flags{};
};

struct PresentationTiming {
  std::uint64_t output_id{};
  std::uint64_t commit_id{};
  std::uint64_t presented_generation{};
  std::uint32_t flip_sequence{};
  std::uint32_t flags{};
  std::uint64_t kernel_timestamp_nanoseconds{};
  std::uint64_t interval_nanoseconds{};
  bool effective_vrr_enabled{};
  bool timestamp_available{};
};

#define GWIPC_VRR_CODEC(Type)                                                  \
  [[nodiscard]] std::vector<std::uint8_t> encode(const Type &);                \
  [[nodiscard]] CodecStatus decode(std::span<const std::uint8_t>, Type &)

GWIPC_VRR_CODEC(OutputVrrCapabilityUpsert);
GWIPC_VRR_CODEC(OutputVrrPolicyUpsert);
GWIPC_VRR_CODEC(OutputVrrStateUpsert);
GWIPC_VRR_CODEC(SurfaceVrrState);
GWIPC_VRR_CODEC(PolicyWindowVrrUpsert);
GWIPC_VRR_CODEC(PolicyOutputVrrUpsert);
GWIPC_VRR_CODEC(PolicyWindowVrrState);
GWIPC_VRR_CODEC(PolicyOutputVrrState);
GWIPC_VRR_CODEC(PresentationTiming);

#undef GWIPC_VRR_CODEC

} // namespace gw::ipc::wire
