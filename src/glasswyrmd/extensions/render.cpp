#include "glasswyrmd/extensions/render.hpp"

#include "glasswyrmd/extensions/render_internal.hpp"
#include "glasswyrmd/request_handlers/common.hpp"

namespace glasswyrm::server::extensions {
namespace x11 = gw::protocol::x11;

DispatchResult dispatch_render(ServerState& state,
                               const DispatchContext& context,
                               const x11::FramedRequest& request) {
  switch (request.data) {
    case 0: return render_query_version(context, request);
    case 1: return render_query_formats(state, context, request);
    case 2: return render_query_index_values(context, request);
    case 4:
    case 5:
    case 6:
    case 7:
    case 33: return render_picture_request(state, context, request);
    case 8:
    case 26: return render_raster_request(state, context, request);
    default:
      return request_handlers::error(context, request,
                                     x11::CoreErrorCode::BadRequest);
  }
}

}  // namespace glasswyrm::server::extensions
