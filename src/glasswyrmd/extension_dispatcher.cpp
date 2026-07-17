#include "glasswyrmd/extension_dispatcher.hpp"

#include "glasswyrmd/extension_registry.hpp"
#include "glasswyrmd/request_handlers/common.hpp"
#include "glasswyrmd/extensions/mit_shm.hpp"
#include "glasswyrmd/extensions/xfixes.hpp"
#include "glasswyrmd/extensions/damage.hpp"
#include "protocol/x11/reply.hpp"

namespace glasswyrm::server {
namespace x11 = gw::protocol::x11;

DispatchResult dispatch_extension_request(
    ServerState& state, const DispatchContext& context,
    const x11::FramedRequest& request) {
  if (!context.extensions) return request_handlers::error(
      context, request, x11::CoreErrorCode::BadRequest);
  const auto* extension = context.extensions->query(request.opcode);
  if (!extension) return request_handlers::error(
      context, request, x11::CoreErrorCode::BadRequest);

  switch (extension->kind) {
    case ExtensionKind::BigRequests: {
      if (request.data != 0)
        return request_handlers::error(context, request,
                                       x11::CoreErrorCode::BadRequest);
      if (request.core_size() != 4)
        return request_handlers::error(context, request,
                                       x11::CoreErrorCode::BadLength);
      x11::ReplyBuilder reply(context.byte_order, context.sequence);
      reply.write_u32(x11::kMaximumBigRequestLengthUnits);
      DispatchResult result{std::move(reply).finish()};
      result.enable_big_requests = true;
      return result;
    }
    case ExtensionKind::MitShm:
      return extensions::dispatch_mit_shm(state, context, request);
    case ExtensionKind::XFixes:
      return extensions::dispatch_xfixes(state, context, request);
    case ExtensionKind::Damage:
      return extensions::dispatch_damage(state, context, request);
    case ExtensionKind::Render:
    case ExtensionKind::Composite:
    case ExtensionKind::RandR: break;
  }

  return request_handlers::error(context, request,
                                 x11::CoreErrorCode::BadRequest);
}

}  // namespace glasswyrm::server
