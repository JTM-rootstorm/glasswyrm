#include "ipc/wire/compositor_contract.hpp"
#include "tests/helpers/test_support.hpp"

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
  for (std::size_t index = 0; index < text.size(); index += 2) {
    result.push_back(static_cast<std::uint8_t>((digit(text[index]) << 4U) |
                                               digit(text[index + 1])));
  }
  return result;
}

SdrColorMetadata color() {
  return {SdrColorSpace::Srgb, TransferFunction::Srgb, ColorPrimaries::Srgb,
          true, 1, 100000, 50000};
}

void test_output_and_surface() {
  OutputUpsert output{9, true, -10, 20, 1920, 1080, 3840, 2160,
                      60000, 2, 1, Transform::Normal, color()};
  OutputUpsert decoded_output;
  require(encode(output) ==
              hex("090000000000000001000000f6ffffff140000008007000038040000"
                  "000f00007008000060ea000002000000010000000100010001000100"
                  "01000000a086010050c30000") &&
              decode(encode(output), decoded_output) == CodecStatus::Ok &&
              decoded_output.output_id == 9 && decoded_output.logical_x == -10 &&
              decoded_output.color.maximum_luminance_millinit == 100000,
          "OutputUpsert round trips complete scale and color state");
  OutputRemove decoded_remove;
  require(encode(OutputRemove{0x0102030405060708ULL}) ==
              std::vector<std::uint8_t>({8, 7, 6, 5, 4, 3, 2, 1}) &&
              decode(encode(OutputRemove{9}), decoded_remove) == CodecStatus::Ok,
          "OutputRemove has an exact eight-byte golden encoding");

  SurfaceUpsert surface;
  surface.surface_id = 10;
  surface.x11_window_id = 0x1234;
  surface.output_id = 9;
  surface.logical_width = 800;
  surface.logical_height = 600;
  surface.visible = true;
  surface.clipping = true;
  surface.clip_width = 400;
  surface.clip_height = 300;
  surface.opacity = 0x8000;
  surface.scale_numerator = 3;
  surface.scale_denominator = 2;
  surface.color = color();
  surface.fullscreen_eligible = TriState::False;
  surface.direct_scanout_eligible = TriState::Unknown;
  SurfaceUpsert decoded_surface;
  require(encode(surface) ==
              hex("0a000000000000003412000000000000000000000000000009000000"
                  "00000000000000000000000020030000580200000000000001010000"
                  "0000000000000000900100002c010000008000000300000002000000"
                  "010001000100010001000000a086010050c300000000000001000000") &&
              decode(encode(surface), decoded_surface) == CodecStatus::Ok &&
              decoded_surface.opacity == 0x8000 && decoded_surface.clipping,
          "SurfaceUpsert round trips required full-state metadata");
  SurfaceRemove decoded_surface_remove;
  require(decode(encode(SurfaceRemove{10}), decoded_surface_remove) ==
              CodecStatus::Ok &&
              encode(SurfaceRemove{10}) == hex("0a00000000000000"),
          "SurfaceRemove round trips its nonzero identity");

  surface.scale_denominator = 0;
  require(decode(encode(surface), decoded_surface) == CodecStatus::InvalidValue,
          "zero surface scale denominators are rejected");
  surface.scale_denominator = 1;
  surface.opacity = kOpacityOne + 1;
  require(decode(encode(surface), decoded_surface) == CodecStatus::InvalidValue,
          "opacity greater than one is rejected");
  surface.opacity = kOpacityOne;
  surface.presentation_flags = 1;
  require(decode(encode(surface), decoded_surface) == CodecStatus::InvalidValue,
          "unknown presentation flags are rejected");
  surface.presentation_flags = 0;
  surface.clip_x = 0x7fffffff;
  surface.clip_width = 2;
  require(decode(encode(surface), decoded_surface) == CodecStatus::InvalidValue,
          "overflowing clip rectangles are rejected");
}

void test_buffers_damage_and_frames() {
  BufferAttach attach;
  attach.buffer_id = 20;
  attach.surface_id = 10;
  attach.width = 64;
  attach.height = 32;
  attach.stride = 256;
  attach.byte_offset = 128;
  attach.storage_size = 128 + 256 * 32;
  attach.pixel_format = PixelFormat::Argb8888;
  attach.alpha_semantics = AlphaSemantics::Premultiplied;
  attach.color = color();
  BufferAttach decoded_attach;
  require(encode(attach) ==
              hex("14000000000000000a000000000000004000000020000000000100000000000080000000000000008020000000000000020002000000000000000000010001000100010001000000a086010050c300000000000000000000"),
          "BufferAttach matches its complete byte golden");
  require(decode(encode(attach), decoded_attach) == CodecStatus::Ok &&
              decoded_attach.storage_size == attach.storage_size,
          "BufferAttach validates and round trips covered storage geometry");
  attach.storage_size--;
  require(decode(encode(attach), decoded_attach) == CodecStatus::InvalidValue,
          "undersized buffer storage is rejected with checked geometry");

  BufferDetach detach{10, 20};
  BufferDetach decoded_detach;
  require(encode(detach) ==
              std::vector<std::uint8_t>({10, 0, 0, 0, 0, 0, 0, 0,
                                         20, 0, 0, 0, 0, 0, 0, 0}) &&
              decode(encode(detach), decoded_detach) == CodecStatus::Ok,
          "BufferDetach has an exact little-endian golden encoding");
  BufferRelease decoded_release;
  require(encode(BufferRelease{20, BufferReleaseReason::Replaced}) ==
              hex("14000000000000000100000000000000") &&
              decode(encode(BufferRelease{20, BufferReleaseReason::Replaced}),
                 decoded_release) == CodecStatus::Ok,
          "BufferRelease round trips its reason");

  SurfaceDamage damage{10, {{-5, 4, 20, 30}, {0, 0, 1, 1}}};
  SurfaceDamage decoded_damage;
  require(encode(damage) ==
              hex("0a000000000000000200000000000000fbffffff0400000014000000"
                  "1e00000000000000000000000100000001000000") &&
              decode(encode(damage), decoded_damage) == CodecStatus::Ok &&
              decoded_damage.rectangles.size() == 2,
          "SurfaceDamage validates and round trips bounded rectangles");
  damage.rectangles[0].width = 0;
  require(decode(encode(damage), decoded_damage) == CodecStatus::InvalidValue,
          "zero-area damage rectangles are rejected");
  damage.rectangles.assign(kMaximumDamageRectangles + 1, {0, 0, 1, 1});
  require(encode(damage).empty(),
          "oversized damage cannot narrow its count during encoding");

  FrameCommit commit{30, 9, 12, 0};
  FrameCommit decoded_commit;
  FrameAcknowledged acknowledged{30, 9, 13, FrameResult::Accepted};
  FrameAcknowledged decoded_acknowledged;
  require(encode(commit) ==
              hex("1e0000000000000009000000000000000c0000000000000000000000"
                  "00000000") &&
              encode(acknowledged) ==
                  hex("1e0000000000000009000000000000000d0000000000000001000000"
                      "00000000") &&
              decode(encode(commit), decoded_commit) == CodecStatus::Ok &&
              decode(encode(acknowledged), decoded_acknowledged) ==
                  CodecStatus::Ok &&
              decoded_acknowledged.commit_id == commit.commit_id,
          "frame commit and acknowledgement preserve correlation IDs");
}

void test_malformed_contracts() {
  OutputUpsert output{9, true, 0, 0, 100, 100, 100, 100,
                      60000, 1, 1, Transform::Normal, color()};
  const auto bytes = encode(output);
  OutputUpsert decoded;
  for (std::size_t size = 0; size < bytes.size(); ++size) {
    require(decode(std::span(bytes).first(size), decoded) ==
                CodecStatus::Truncated,
            "every truncated OutputUpsert prefix is rejected");
  }
  auto trailing = bytes;
  trailing.push_back(0);
  require(decode(trailing, decoded) == CodecStatus::TrailingData,
          "contract trailing bytes are rejected");
  auto invalid_boolean = bytes;
  invalid_boolean[8] = 2;
  require(decode(invalid_boolean, decoded) == CodecStatus::InvalidValue,
          "noncanonical contract booleans are rejected");
  auto unknown_transform = bytes;
  unknown_transform[10] = 0xff;
  require(decode(unknown_transform, decoded) == CodecStatus::InvalidValue,
          "unknown contract enums are rejected");
}

} // namespace

int main() {
  test_output_and_surface();
  test_buffers_damage_and_frames();
  test_malformed_contracts();
  return 0;
}
