#include "glasswyrmd/output_configuration_events.hpp"

#include "glasswyrmd/randr_state.hpp"

#include <algorithm>
#include <new>
#include <utility>

namespace glasswyrm::server {

std::optional<std::vector<ProtocolEventIntent>>
build_output_configuration_events(const ServerState& committed) {
  try {
    std::vector<ProtocolEventIntent> result;
    const auto& randr = committed.randr();
    const auto timestamp = randr.configuration_timestamp();
    const auto& screen = committed.screen();
    for (const auto& selection : randr.selections()) {
      if ((selection.mask & 0x1U) != 0) {
        ProtocolEventIntent intent;
        intent.delivery = ProtocolEventDelivery::DirectClient;
        intent.client = selection.client;
        intent.event = RandRScreenChangeNotifyEvent{
            kRandRRotate0, timestamp, timestamp, screen.root_window,
            selection.window, 0, 0, screen.width_pixels,
            screen.height_pixels, screen.width_millimeters,
            screen.height_millimeters};
        result.push_back(std::move(intent));
      }
      for (const auto& output : randr.outputs()) {
        const auto current = std::ranges::find(
            output.modes, true, &RandRModeObject::current);
        const auto mode = current == output.modes.end() ? 0 : current->xid;
        if ((selection.mask & 0x2U) != 0) {
          ProtocolEventIntent intent;
          intent.delivery = ProtocolEventDelivery::DirectClient;
          intent.client = selection.client;
          intent.event = RandRCrtcChangeNotifyEvent{
              timestamp, selection.window, output.crtc_xid,
              output.enabled ? mode : 0, kRandRRotate0,
              static_cast<std::int16_t>(output.logical_x),
              static_cast<std::int16_t>(output.logical_y),
              static_cast<std::uint16_t>(output.enabled
                                             ? output.logical_width
                                             : 0),
              static_cast<std::uint16_t>(output.enabled
                                             ? output.logical_height
                                             : 0)};
          result.push_back(std::move(intent));
        }
        if ((selection.mask & 0x4U) != 0) {
          ProtocolEventIntent intent;
          intent.delivery = ProtocolEventDelivery::DirectClient;
          intent.client = selection.client;
          intent.event = RandROutputChangeNotifyEvent{
              timestamp, timestamp, selection.window, output.xid,
              output.enabled ? output.crtc_xid : 0,
              output.enabled ? mode : 0, kRandRRotate0,
              static_cast<std::uint8_t>(output.connected ? 0 : 1), 0};
          result.push_back(std::move(intent));
        }
      }
    }
    return result;
  } catch (const std::bad_alloc&) {
    return std::nullopt;
  }
}

} // namespace glasswyrm::server
