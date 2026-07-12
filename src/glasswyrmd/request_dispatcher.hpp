#pragma once

#include "glasswyrmd/server_state.hpp"
#include "protocol/x11/byte_order.hpp"
#include "protocol/x11/request.hpp"
#include "protocol/x11/lifecycle_request.hpp"

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace glasswyrm::server {

struct DispatchContext {
  ClientId client_id{0};
  std::uint32_t resource_base{0};
  std::uint32_t resource_mask{0};
  std::uint64_t sequence{0};
  gw::protocol::x11::ByteOrder byte_order{
      gw::protocol::x11::ByteOrder::LittleEndian};
  bool integrated_lifecycle{false};
};

enum class DispatchKind { Immediate, DeferredLifecycle, CloseClient };
struct DispatchResult {
  std::vector<std::uint8_t> output;
  DispatchKind kind{DispatchKind::Immediate};
  std::uint32_t deferred_window{0};
  std::optional<gw::protocol::x11::ConfigureWindowRequest> deferred_configure;
  bool deferred_map{false};
  DispatchResult() = default;
  DispatchResult(std::vector<std::uint8_t> packet) : output(std::move(packet)) {}
  static DispatchResult deferred(
      std::uint32_t window,
      std::optional<gw::protocol::x11::ConfigureWindowRequest> configure = {},
      bool map = false) {
    DispatchResult result; result.kind = DispatchKind::DeferredLifecycle;
    result.deferred_window = window; result.deferred_configure = std::move(configure);
    result.deferred_map = map;
    return result;
  }
};

[[nodiscard]] DispatchResult dispatch_request(
    ServerState& state, const DispatchContext& context,
    const gw::protocol::x11::FramedRequest& request);

}  // namespace glasswyrm::server
