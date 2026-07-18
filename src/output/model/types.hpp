#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace glasswyrm::output {

inline constexpr std::size_t kMaximumOutputs = 8;
inline constexpr std::size_t kMaximumOutputNameBytes = 63;
inline constexpr std::uint32_t kMaximumPhysicalExtent = 4096;
inline constexpr std::uint64_t kMaximumPhysicalPixels = 16'777'216;
inline constexpr std::uint64_t kMaximumTotalOutputPixels = 67'108'864;
inline constexpr std::uint32_t kMaximumRootLogicalWidth = 32'767;
inline constexpr std::uint32_t kMaximumRootLogicalHeight = 32'767;
inline constexpr std::uint32_t kMaximumScaleDenominator = 120;
inline constexpr std::size_t kMaximumModesPerOutput = 128;

struct OutputId {
  std::uint64_t value{};

  [[nodiscard]] constexpr explicit operator bool() const noexcept {
    return value != 0;
  }
  friend constexpr auto operator<=>(const OutputId &,
                                    const OutputId &) = default;
};

struct OutputModeId {
  std::uint64_t value{};

  [[nodiscard]] constexpr explicit operator bool() const noexcept {
    return value != 0;
  }
  friend constexpr auto operator<=>(const OutputModeId &,
                                    const OutputModeId &) = default;
};

struct RationalScale {
  std::uint32_t numerator{1};
  std::uint32_t denominator{1};

  friend constexpr bool operator==(const RationalScale &,
                                   const RationalScale &) = default;
};

enum class OutputTransform : std::uint8_t {
  Normal = 0,
  Rotate90 = 1,
  Rotate180 = 2,
  Rotate270 = 3,
  Flipped = 4,
  Flipped90 = 5,
  Flipped180 = 6,
  Flipped270 = 7,
};

[[nodiscard]] constexpr std::uint32_t
output_transform_bit(const OutputTransform transform) noexcept {
  const auto value = static_cast<std::uint8_t>(transform);
  return value <= static_cast<std::uint8_t>(OutputTransform::Flipped270)
             ? UINT32_C(1) << value
             : 0;
}

inline constexpr std::uint32_t kAllOutputTransformsMask =
    output_transform_bit(OutputTransform::Normal) |
    output_transform_bit(OutputTransform::Rotate90) |
    output_transform_bit(OutputTransform::Rotate180) |
    output_transform_bit(OutputTransform::Rotate270) |
    output_transform_bit(OutputTransform::Flipped) |
    output_transform_bit(OutputTransform::Flipped90) |
    output_transform_bit(OutputTransform::Flipped180) |
    output_transform_bit(OutputTransform::Flipped270);

enum class OutputKind : std::uint8_t { Headless = 1, Drm = 2 };

enum class OutputCapabilityFlags : std::uint32_t {
  None = 0,
  Connected = UINT32_C(1) << 0U,
  ArbitraryHeadlessMode = UINT32_C(1) << 1U,
  ModeConfigurable = UINT32_C(1) << 2U,
  ScaleConfigurable = UINT32_C(1) << 3U,
  TransformConfigurable = UINT32_C(1) << 4U,
  PrimaryEligible = UINT32_C(1) << 5U,
  PhysicalDimensionsKnown = UINT32_C(1) << 6U,
};

[[nodiscard]] constexpr OutputCapabilityFlags
operator|(const OutputCapabilityFlags left,
          const OutputCapabilityFlags right) noexcept {
  return static_cast<OutputCapabilityFlags>(static_cast<std::uint32_t>(left) |
                                            static_cast<std::uint32_t>(right));
}

[[nodiscard]] constexpr bool
has_capability(const OutputCapabilityFlags flags,
               const OutputCapabilityFlags capability) noexcept {
  return (static_cast<std::uint32_t>(flags) &
          static_cast<std::uint32_t>(capability)) != 0;
}

enum class SdrColorSpace : std::uint8_t { Srgb = 1, DisplayP3 = 2 };
enum class SdrTransferFunction : std::uint8_t { Srgb = 1, Linear = 2 };
enum class SdrColorPrimaries : std::uint8_t { Srgb = 1, DisplayP3 = 2 };

struct SdrMetadata {
  SdrColorSpace color_space{SdrColorSpace::Srgb};
  SdrTransferFunction transfer_function{SdrTransferFunction::Srgb};
  SdrColorPrimaries primaries{SdrColorPrimaries::Srgb};
  bool luminance_available{};
  std::uint32_t minimum_luminance_millinit{};
  std::uint32_t maximum_luminance_millinit{};
  std::uint32_t max_frame_average_luminance_millinit{};

  friend constexpr bool operator==(const SdrMetadata &,
                                   const SdrMetadata &) = default;
};

struct PhysicalExtent {
  std::uint32_t width{};
  std::uint32_t height{};

  friend constexpr bool operator==(const PhysicalExtent &,
                                   const PhysicalExtent &) = default;
};

struct LogicalExtent {
  std::uint32_t width{};
  std::uint32_t height{};

  friend constexpr bool operator==(const LogicalExtent &,
                                   const LogicalExtent &) = default;
};

struct LogicalPoint {
  std::int32_t x{};
  std::int32_t y{};

  friend constexpr bool operator==(const LogicalPoint &,
                                   const LogicalPoint &) = default;
};

struct PhysicalPoint {
  std::uint32_t x{};
  std::uint32_t y{};

  friend constexpr bool operator==(const PhysicalPoint &,
                                   const PhysicalPoint &) = default;
};

struct LogicalRectangle {
  std::int32_t x{};
  std::int32_t y{};
  std::uint32_t width{};
  std::uint32_t height{};

  [[nodiscard]] constexpr bool empty() const noexcept {
    return width == 0 || height == 0;
  }
  friend constexpr bool operator==(const LogicalRectangle &,
                                   const LogicalRectangle &) = default;
};

struct PhysicalRectangle {
  std::uint32_t x{};
  std::uint32_t y{};
  std::uint32_t width{};
  std::uint32_t height{};

  [[nodiscard]] constexpr bool empty() const noexcept {
    return width == 0 || height == 0;
  }
  friend constexpr bool operator==(const PhysicalRectangle &,
                                   const PhysicalRectangle &) = default;
};

struct OutputMode {
  OutputModeId id{};
  OutputId output_id{};
  std::uint32_t physical_width{};
  std::uint32_t physical_height{};
  std::uint32_t refresh_millihertz{};
  std::uint32_t flags{};
  std::string name;
  bool preferred{};
  bool current{};
};

struct OutputDescriptor {
  OutputId id{};
  std::string name;
  OutputKind kind{OutputKind::Headless};
  bool connected{};
  std::uint32_t physical_width_mm{};
  std::uint32_t physical_height_mm{};
  std::uint32_t supported_transform_mask{
      output_transform_bit(OutputTransform::Normal)};
  RationalScale minimum_scale{};
  RationalScale maximum_scale{4, 1};
  std::uint32_t maximum_scale_denominator{kMaximumScaleDenominator};
  bool mode_configurable{};
  bool scale_configurable{};
  bool transform_configurable{};
  bool primary_eligible{};
  bool arbitrary_headless_mode{};
  std::uint32_t maximum_physical_width{kMaximumPhysicalExtent};
  std::uint32_t maximum_physical_height{kMaximumPhysicalExtent};
  std::uint64_t maximum_physical_pixels{kMaximumPhysicalPixels};
  std::vector<OutputMode> modes;
};

struct OutputState {
  OutputId output_id{};
  bool enabled{};
  OutputModeId mode_id{};
  std::int32_t logical_x{};
  std::int32_t logical_y{};
  std::uint32_t logical_width{};
  std::uint32_t logical_height{};
  std::uint32_t physical_width{};
  std::uint32_t physical_height{};
  std::uint32_t refresh_millihertz{};
  RationalScale scale{};
  OutputTransform transform{OutputTransform::Normal};
  SdrMetadata color{};
  bool primary{};
  std::uint64_t generation{};
};

struct OutputLayout {
  std::map<OutputId, OutputDescriptor> descriptors;
  std::map<OutputId, OutputState> states;
  OutputId primary_output_id{};
  std::uint32_t root_logical_width{};
  std::uint32_t root_logical_height{};
  std::uint64_t generation{};
  std::size_t enabled_output_count{};
  std::vector<OutputId> output_order;
};

struct OutputMembership {
  OutputId primary_output_id{};
  std::vector<OutputId> output_ids;
};

} // namespace glasswyrm::output
