#include "glasswyrmd/request_handlers/common.hpp"

#include "protocol/x11/reply.hpp"

namespace glasswyrm::server::request_handlers {
namespace x11 = gw::protocol::x11;

DispatchResult error(const DispatchContext& context,
                     const x11::FramedRequest& request,
                     const x11::CoreErrorCode code,
                     const std::uint32_t bad_value) {
  return {x11::encode_core_error(
      context.byte_order,
      {code, context.sequence, bad_value, request.opcode, 0})};
}

bool exact_size(const x11::FramedRequest& request, const std::size_t size) {
  return request.bytes.size() == size;
}

std::size_t padded_size(const std::size_t size) { return (size + 3U) & ~std::size_t{3U}; }

}  // namespace glasswyrm::server::request_handlers
