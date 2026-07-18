#include "glasswyrmd/extension_event_helpers.hpp"
#include "glasswyrmd/randr_state.hpp"
#include "protocol/x11/byte_cursor.hpp"
#include "tests/helpers/test_support.hpp"

#include <span>

namespace {

using namespace glasswyrm::server;
namespace x11 = gw::protocol::x11;
using gw::test::require;

std::uint16_t u16(const std::span<const std::uint8_t> bytes,
                  const x11::ByteOrder order, const std::size_t offset) {
  x11::ByteReader reader(bytes.subspan(offset), order);
  std::uint16_t value{};
  require(reader.read_u16(value), "read RANDR event u16");
  return value;
}

std::uint32_t u32(const std::span<const std::uint8_t> bytes,
                  const x11::ByteOrder order, const std::size_t offset) {
  x11::ByteReader reader(bytes.subspan(offset), order);
  std::uint32_t value{};
  require(reader.read_u32(value), "read RANDR event u32");
  return value;
}

void test_event_wire(const x11::ByteOrder order) {
  const auto screen = encode_randr_screen_change_notify(
      order, 0x12345,
      {kRandRRotate0, 10, 11, 1, 0x400010, 0, 0, 1024, 768, 270, 203});
  require(screen.size() == 32 && screen[0] == 67 &&
              screen[1] == kRandRRotate0 &&
              u16(screen, order, 2) == 0x2345 &&
              u32(screen, order, 4) == 10 &&
              u32(screen, order, 8) == 11 &&
              u32(screen, order, 12) == 1 &&
              u32(screen, order, 16) == 0x400010 &&
              u16(screen, order, 24) == 1024 &&
              u16(screen, order, 30) == 203,
          "ScreenChangeNotify follows the RANDR XML layout");

  const auto crtc = encode_randr_crtc_change_notify(
      order, 0x12346,
      {12, 0x400010, kRandRCrtcId, kRandRModeId, kRandRRotate0, -2, 3,
       1024, 768});
  require(crtc.size() == 32 && crtc[0] == 68 && crtc[1] == 0 &&
              u16(crtc, order, 2) == 0x2346 &&
              u32(crtc, order, 4) == 12 &&
              u32(crtc, order, 12) == kRandRCrtcId &&
              u32(crtc, order, 16) == kRandRModeId &&
              static_cast<std::int16_t>(u16(crtc, order, 24)) == -2 &&
              u16(crtc, order, 30) == 768,
          "Notify.CrtcChange follows the RANDR XML union layout");

  const auto output = encode_randr_output_change_notify(
      order, 0x12347,
      {13, 14, 0x400010, kRandROutputId, kRandRCrtcId, kRandRModeId,
       kRandRRotate0, 0, 0});
  require(output.size() == 32 && output[0] == 68 && output[1] == 1 &&
              u16(output, order, 2) == 0x2347 &&
              u32(output, order, 4) == 13 &&
              u32(output, order, 8) == 14 &&
              u32(output, order, 16) == kRandROutputId &&
              u32(output, order, 24) == kRandRModeId &&
              u16(output, order, 28) == kRandRRotate0,
          "Notify.OutputChange follows the RANDR XML union layout");

  const auto property = encode_randr_output_property_notify(
      order, 0x12348, {0x400010, kRandROutputId, 1, 15, 1});
  require(property.size() == 32 && property[0] == 68 && property[1] == 2 &&
              u16(property, order, 2) == 0x2348 &&
              u32(property, order, 4) == 0x400010 &&
              u32(property, order, 8) == kRandROutputId &&
              u32(property, order, 12) == 1 &&
              u32(property, order, 16) == 15 && property[20] == 1,
          "Notify.OutputProperty follows the RANDR XML union layout");
}

}  // namespace

int main() {
  for (const auto order : {x11::ByteOrder::LittleEndian,
                           x11::ByteOrder::BigEndian})
    test_event_wire(order);
  return 0;
}
