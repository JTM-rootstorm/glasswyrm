#include "glasswyrmd/extensions/gw_scale.hpp"

#include "glasswyrmd/request_handlers/common.hpp"
#include "protocol/x11/byte_cursor.hpp"
#include "protocol/x11/reply.hpp"

#include <algorithm>

namespace glasswyrm::server::extensions {
namespace x11 = gw::protocol::x11;
using request_handlers::error;

namespace {

DispatchResult query_version(const DispatchContext& context,
                             const x11::FramedRequest& request) {
  if (request.core_size() != 12)
    return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t client_major{};
  std::uint32_t client_minor{};
  (void)reader.read_u32(client_major);
  (void)reader.read_u32(client_minor);
  const auto negotiated_minor =
      client_major == 0 ? std::min(client_minor, UINT32_C(1)) : UINT32_C(1);
  x11::ReplyBuilder reply(context.byte_order, context.sequence);
  reply.write_u32(0);
  reply.write_u32(negotiated_minor);
  reply.write_padding(16);
  return {std::move(reply).finish()};
}

}  // namespace

DispatchResult dispatch_gw_scale(ServerState& state,
                                 const DispatchContext& context,
                                 const x11::FramedRequest& request) {
  static_cast<void>(state);
  if (request.data == 0) return query_version(context, request);
  return error(context, request, x11::CoreErrorCode::BadRequest);
}

}  // namespace glasswyrm::server::extensions
