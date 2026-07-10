#pragma once

#include "glasswyrmd/server_state.hpp"
#include "protocol/x11/byte_order.hpp"
#include "protocol/x11/request.hpp"

#include <cstdint>
#include <vector>

namespace glasswyrm::server {

struct DispatchContext {
  ClientId client_id{0};
  std::uint32_t resource_base{0};
  std::uint32_t resource_mask{0};
  std::uint64_t sequence{0};
  gw::protocol::x11::ByteOrder byte_order{
      gw::protocol::x11::ByteOrder::LittleEndian};
};

struct DispatchResult {
  std::vector<std::uint8_t> output;
};

[[nodiscard]] DispatchResult dispatch_request(
    ServerState& state, const DispatchContext& context,
    const gw::protocol::x11::FramedRequest& request);

}  // namespace glasswyrm::server
