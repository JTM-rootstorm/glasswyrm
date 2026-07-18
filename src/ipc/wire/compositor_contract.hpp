#pragma once

#include "ipc/wire/types.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace gw::ipc::wire {

inline constexpr std::size_t kMaximumDamageRectangles = 1024;
inline constexpr std::uint32_t kOpacityOne = 0x00010000U;

enum class Transform : std::uint16_t {
  Normal = 0,
  Rotate90 = 1,
  Rotate180 = 2,
  Rotate270 = 3,
  Flipped = 4,
  Flipped90 = 5,
  Flipped180 = 6,
  Flipped270 = 7,
};

enum class SdrColorSpace : std::uint16_t { Srgb = 1, DisplayP3 = 2 };
enum class TransferFunction : std::uint16_t { Srgb = 1, Linear = 2 };
enum class ColorPrimaries : std::uint16_t { Srgb = 1, DisplayP3 = 2 };
enum class TriState : std::uint8_t { Unknown = 0, False = 1, True = 2 };
enum class PixelFormat : std::uint16_t { Xrgb8888 = 1, Argb8888 = 2 };
enum class AlphaSemantics : std::uint16_t { Opaque = 1, Premultiplied = 2 };
enum class SynchronizationMode : std::uint16_t { None = 0, EventFd = 1 };
enum class BufferReleaseReason : std::uint16_t {
  Replaced = 1,
  SurfaceRemoved = 2,
  ConsumerDone = 3,
  Invalid = 4,
};
enum class FrameResult : std::uint16_t {
  Accepted = 1,
  RejectedIncompleteMetadata = 2,
  RejectedInvalidBuffer = 3,
  RejectedUnknownSurface = 4,
  Dropped = 5,
};

struct SdrColorMetadata {
  SdrColorSpace color_space{SdrColorSpace::Srgb};
  TransferFunction transfer_function{TransferFunction::Srgb};
  ColorPrimaries primaries{ColorPrimaries::Srgb};
  bool luminance_available{false};
  std::uint32_t minimum_luminance_millinit{0};
  std::uint32_t maximum_luminance_millinit{0};
  std::uint32_t max_frame_average_luminance_millinit{0};
};

struct OutputUpsert {
  std::uint64_t output_id{0};
  bool enabled{false};
  std::int32_t logical_x{0};
  std::int32_t logical_y{0};
  std::uint32_t logical_width{0};
  std::uint32_t logical_height{0};
  std::uint32_t physical_pixel_width{0};
  std::uint32_t physical_pixel_height{0};
  std::uint32_t refresh_millihertz{0};
  std::uint32_t scale_numerator{1};
  std::uint32_t scale_denominator{1};
  Transform transform{Transform::Normal};
  SdrColorMetadata color;
};

struct OutputRemove { std::uint64_t output_id{0}; };

struct SurfaceUpsert {
  std::uint64_t surface_id{0};
  std::uint32_t x11_window_id{0};
  std::uint64_t parent_surface_id{0};
  std::uint64_t output_id{0};
  std::int32_t logical_x{0};
  std::int32_t logical_y{0};
  std::uint32_t logical_width{0};
  std::uint32_t logical_height{0};
  std::int32_t stacking{0};
  bool visible{false};
  bool clipping{false};
  std::int32_t clip_x{0};
  std::int32_t clip_y{0};
  std::uint32_t clip_width{0};
  std::uint32_t clip_height{0};
  Transform transform{Transform::Normal};
  std::uint32_t opacity{kOpacityOne};
  std::uint32_t scale_numerator{1};
  std::uint32_t scale_denominator{1};
  SdrColorMetadata color;
  std::uint32_t presentation_flags{0};
  TriState fullscreen_eligible{TriState::Unknown};
  TriState direct_scanout_eligible{TriState::Unknown};
};

struct SurfaceRemove { std::uint64_t surface_id{0}; };

struct BufferAttach {
  std::uint64_t buffer_id{0};
  std::uint64_t surface_id{0};
  std::uint32_t width{0};
  std::uint32_t height{0};
  std::uint32_t stride{0};
  std::uint64_t byte_offset{0};
  std::uint64_t storage_size{0};
  PixelFormat pixel_format{PixelFormat::Xrgb8888};
  std::uint64_t modifier{0};
  AlphaSemantics alpha_semantics{AlphaSemantics::Opaque};
  SdrColorMetadata color;
  SynchronizationMode synchronization{SynchronizationMode::None};
  std::uint32_t flags{0};
};

struct BufferDetach {
  std::uint64_t surface_id{0};
  std::uint64_t buffer_id{0};
};

struct BufferRelease {
  std::uint64_t buffer_id{0};
  BufferReleaseReason reason{BufferReleaseReason::ConsumerDone};
};

struct DamageRectangle {
  std::int32_t x{0};
  std::int32_t y{0};
  std::uint32_t width{0};
  std::uint32_t height{0};
};

struct SurfaceDamage {
  std::uint64_t surface_id{0};
  std::vector<DamageRectangle> rectangles;
};

struct FrameCommit {
  std::uint64_t commit_id{0};
  std::uint64_t output_id{0};
  std::uint64_t producer_generation{0};
  std::uint32_t flags{0};
};

struct FrameAcknowledged {
  std::uint64_t commit_id{0};
  std::uint64_t output_id{0};
  std::uint64_t presented_generation{0};
  FrameResult result{FrameResult::Accepted};
};

#define GWIPC_DECLARE_CONTRACT_CODEC(Type)                                    \
  [[nodiscard]] std::vector<std::uint8_t> encode(const Type &value);           \
  [[nodiscard]] CodecStatus decode(std::span<const std::uint8_t> bytes,        \
                                   Type &value)

GWIPC_DECLARE_CONTRACT_CODEC(OutputUpsert);
GWIPC_DECLARE_CONTRACT_CODEC(OutputRemove);
GWIPC_DECLARE_CONTRACT_CODEC(SurfaceUpsert);
GWIPC_DECLARE_CONTRACT_CODEC(SurfaceRemove);
GWIPC_DECLARE_CONTRACT_CODEC(BufferAttach);
GWIPC_DECLARE_CONTRACT_CODEC(BufferDetach);
GWIPC_DECLARE_CONTRACT_CODEC(BufferRelease);
GWIPC_DECLARE_CONTRACT_CODEC(SurfaceDamage);
GWIPC_DECLARE_CONTRACT_CODEC(FrameCommit);
GWIPC_DECLARE_CONTRACT_CODEC(FrameAcknowledged);

#undef GWIPC_DECLARE_CONTRACT_CODEC

} // namespace gw::ipc::wire
