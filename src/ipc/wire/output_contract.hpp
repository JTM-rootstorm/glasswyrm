#pragma once

#include "ipc/wire/compositor_contract.hpp"
#include "ipc/wire/types.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace gw::ipc::wire {

inline constexpr std::size_t kMaximumOutputNameBytes = 63;
inline constexpr std::size_t kMaximumManagedOutputs = 8;
inline constexpr std::uint16_t kOutputContractFdCount = 0;
inline constexpr std::size_t kOutputDescriptorFixedPayloadSize = 56;
inline constexpr std::size_t kOutputModePayloadSize = 40;
inline constexpr std::size_t kSurfaceOutputStateFixedPayloadSize = 48;
inline constexpr std::size_t kPolicyOutputPayloadSize = 56;
inline constexpr std::size_t kPolicyWindowOutputHintPayloadSize = 24;
inline constexpr std::size_t kOutputStateQueryPayloadSize = 16;
inline constexpr std::size_t kOutputConfigurationCommitPayloadSize = 32;
inline constexpr std::size_t kOutputConfigurationAcknowledgedPayloadSize = 48;

inline constexpr std::uint32_t kOutputConnected = UINT32_C(1) << 0U;
inline constexpr std::uint32_t kOutputArbitraryHeadlessMode =
    UINT32_C(1) << 1U;
inline constexpr std::uint32_t kOutputModeFixed = UINT32_C(1) << 2U;
inline constexpr std::uint32_t kOutputScaleConfigurable = UINT32_C(1) << 3U;
inline constexpr std::uint32_t kOutputTransformConfigurable =
    UINT32_C(1) << 4U;
inline constexpr std::uint32_t kOutputPrimaryEligible = UINT32_C(1) << 5U;
inline constexpr std::uint32_t kOutputPhysicalDimensionsKnown =
    UINT32_C(1) << 6U;
inline constexpr std::uint32_t kKnownOutputCapabilityFlags = UINT32_C(0x7f);

inline constexpr std::uint32_t kQueryOutputDescriptors = UINT32_C(1) << 0U;
inline constexpr std::uint32_t kQueryOutputModes = UINT32_C(1) << 1U;
inline constexpr std::uint32_t kQueryOutputLayout = UINT32_C(1) << 2U;
inline constexpr std::uint32_t kQueryOutputWindows = UINT32_C(1) << 3U;
inline constexpr std::uint32_t kKnownOutputQueryFlags = UINT32_C(0x0f);

enum class OutputKind : std::uint16_t { Headless = 1, Drm = 2 };
enum class SurfaceScaleMode : std::uint16_t { Legacy = 1, ScaledPixmap = 2 };
enum class OutputConfigurationResult : std::uint16_t {
  Accepted = 1,
  StaleGeneration,
  Busy,
  InvalidLayout,
  UnknownOutput,
  UnsupportedMode,
  UnsupportedScale,
  UnsupportedTransform,
  PolicyRejected,
  CompositorRejected,
  PresenterRejected,
  InternalError,
};

struct OutputDescriptorUpsert {
  std::uint64_t output_id{};
  OutputKind kind{OutputKind::Headless};
  std::uint32_t capability_flags{};
  std::string name;
  std::uint32_t physical_width_millimeters{};
  std::uint32_t physical_height_millimeters{};
  std::uint32_t supported_transform_mask{1};
  std::uint32_t minimum_scale_numerator{1};
  std::uint32_t minimum_scale_denominator{1};
  std::uint32_t maximum_scale_numerator{4};
  std::uint32_t maximum_scale_denominator{1};
  std::uint32_t maximum_scale_denominator_value{120};
  std::uint32_t maximum_physical_width{4096};
  std::uint32_t maximum_physical_height{4096};
};

struct OutputModeUpsert {
  std::uint64_t output_id{};
  std::uint64_t mode_id{};
  std::uint32_t physical_width{};
  std::uint32_t physical_height{};
  std::uint32_t refresh_millihertz{};
  bool preferred{};
  bool current{};
  std::uint32_t flags{};
};

struct SurfaceOutputState {
  std::uint64_t surface_id{};
  std::uint64_t primary_output_id{};
  std::vector<std::uint64_t> output_ids;
  std::uint32_t preferred_scale_numerator{1};
  std::uint32_t preferred_scale_denominator{1};
  std::uint32_t client_buffer_scale{1};
  SurfaceScaleMode scale_mode{SurfaceScaleMode::Legacy};
  std::uint64_t layout_generation{};
  std::uint32_t flags{};
};

struct PolicyOutputUpsert {
  std::uint64_t output_id{};
  std::int32_t logical_x{}, logical_y{};
  std::uint32_t logical_width{}, logical_height{};
  std::int32_t work_x{}, work_y{};
  std::uint32_t work_width{}, work_height{};
  std::uint32_t scale_numerator{1}, scale_denominator{1};
  Transform transform{Transform::Normal};
  bool enabled{}, primary{};
  std::uint32_t flags{};
};

struct PolicyWindowOutputHint {
  std::uint32_t window_id{};
  std::uint64_t previous_output_id{};
  std::uint64_t preferred_output_id{};
  std::uint32_t flags{};
};

struct OutputStateQuery {
  std::uint64_t query_id{};
  std::uint32_t flags{};
};

struct OutputConfigurationCommit {
  std::uint64_t configuration_id{};
  std::uint64_t base_generation{};
  std::uint64_t primary_output_id{};
  std::uint32_t flags{};
};

struct OutputConfigurationAcknowledged {
  std::uint64_t request_id{};
  std::uint64_t applied_generation{};
  OutputConfigurationResult result{OutputConfigurationResult::Accepted};
  std::uint32_t flags{};
  std::uint64_t primary_output_id{};
  std::uint32_t root_logical_width{};
  std::uint32_t root_logical_height{};
  std::uint32_t enabled_output_count{};
};

#define GWIPC_OUTPUT_CODEC(Type)                                              \
  [[nodiscard]] std::vector<std::uint8_t> encode(const Type& value);          \
  [[nodiscard]] CodecStatus decode(std::span<const std::uint8_t> bytes,       \
                                   Type& value)

GWIPC_OUTPUT_CODEC(OutputDescriptorUpsert);
GWIPC_OUTPUT_CODEC(OutputModeUpsert);
GWIPC_OUTPUT_CODEC(SurfaceOutputState);
GWIPC_OUTPUT_CODEC(PolicyOutputUpsert);
GWIPC_OUTPUT_CODEC(PolicyWindowOutputHint);
GWIPC_OUTPUT_CODEC(OutputStateQuery);
GWIPC_OUTPUT_CODEC(OutputConfigurationCommit);
GWIPC_OUTPUT_CODEC(OutputConfigurationAcknowledged);

#undef GWIPC_OUTPUT_CODEC

}  // namespace gw::ipc::wire
