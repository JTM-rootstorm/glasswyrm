#include "glasswyrmd/extension_dispatcher.hpp"

#include "glasswyrmd/extension_registry.hpp"
#include "glasswyrmd/request_handlers/common.hpp"
#include "glasswyrmd/extensions/mit_shm.hpp"
#include "glasswyrmd/extensions/xfixes.hpp"
#include "glasswyrmd/extensions/damage.hpp"
#include "glasswyrmd/extensions/randr.hpp"
#include "glasswyrmd/extensions/gw_scale.hpp"
#include "glasswyrmd/extensions/gw_vrr.hpp"
#include "glasswyrmd/extensions/render.hpp"
#include "glasswyrmd/extensions/composite.hpp"
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
      return extensions::dispatch_render(state, context, request);
    case ExtensionKind::Composite:
      return extensions::dispatch_composite(state, context, request);
    case ExtensionKind::RandR:
      return extensions::dispatch_randr(state, context, request);
    case ExtensionKind::GwScale:
      return extensions::dispatch_gw_scale(state, context, request);
    case ExtensionKind::GwVrr: {
      auto result = extensions::dispatch_gw_vrr(
          state, state.vrr(), context, request);
      if (!result.preference_change)
        return std::move(result.dispatch);
      if (context.integrated_lifecycle) {
        auto deferred = DispatchResult::deferred_vrr_change(
            {result.preference_change->window,
             result.preference_change->preference,
             std::move(result.dispatch.output)});
        return deferred;
      }
      state.vrr()
          .ensure_window(result.preference_change->window)
          .preference = result.preference_change->preference;
      return std::move(result.dispatch);
    }
  }

  return request_handlers::error(context, request,
                                 x11::CoreErrorCode::BadRequest);
}

}  // namespace glasswyrm::server
