#include "glasswyrmd/extensions/randr.hpp"

#include "glasswyrmd/extensions/randr_internal.hpp"
#include "glasswyrmd/request_handlers/common.hpp"

namespace glasswyrm::server::extensions {
namespace x11 = gw::protocol::x11;

DispatchResult dispatch_randr(ServerState& state,
                              const DispatchContext& context,
                              const x11::FramedRequest& request) {
  switch (request.data) {
    case 0: return randr_query_version(context, request);
    case 4: return randr_select_input(state, context, request);
    case 5: return randr_get_screen_info(state, context, request);
    case 6: return randr_get_screen_size_range(state, context, request);
    case 8:
    case 25: return randr_get_screen_resources(state, context, request);
    case 9: return randr_get_output_info(state, context, request);
    case 10: return randr_list_output_properties(state, context, request);
    case 11: return randr_query_output_property(state, context, request);
    case 15: return randr_get_output_property(state, context, request);
    case 20: return randr_get_crtc_info(state, context, request);
    case 21: return randr_set_crtc_config(state, context, request);
    case 22: return randr_get_crtc_gamma_size(state, context, request);
    case 31: return randr_get_output_primary(state, context, request);
    default:
      return request_handlers::error(context, request,
                                     x11::CoreErrorCode::BadRequest);
  }
}

}  // namespace glasswyrm::server::extensions
