#include "glasswyrmd/request_handlers/common.hpp"

#include "protocol/x11/byte_cursor.hpp"
#include "protocol/x11/reply.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <utility>

namespace glasswyrm::server::request_handlers {
namespace x11 = gw::protocol::x11;

DispatchResult intern_atom(ServerState& state, const DispatchContext& context,
                           const x11::FramedRequest& request) {
  if (request.data > 1 || request.bytes.size() < 8) {
    return request.data > 1
               ? error(context, request, x11::CoreErrorCode::BadValue,
                       request.data)
               : error(context, request, x11::CoreErrorCode::BadLength);
  }
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint16_t name_length = 0;
  std::span<const std::uint8_t> name;
  if (!reader.read_u16(name_length) || !reader.skip(2) ||
      request.bytes.size() != 8 + ((name_length + 3U) & ~std::size_t{3}) ||
      !reader.read_bytes(name_length, name)) {
    return error(context, request, x11::CoreErrorCode::BadLength);
  }
  const auto result = state.atoms().intern(
      std::string_view(reinterpret_cast<const char*>(name.data()), name.size()),
      request.data != 0);
  if (result.status == InternAtomStatus::Exhausted) {
    return error(context, request, x11::CoreErrorCode::BadAlloc);
  }
  x11::ReplyBuilder reply(context.byte_order, context.sequence);
  reply.write_u32(result.atom);
  return {std::move(reply).finish()};
}

DispatchResult get_atom_name(ServerState& state,
                             const DispatchContext& context,
                             const x11::FramedRequest& request) {
  if (!exact_size(request, 8)) {
    return error(context, request, x11::CoreErrorCode::BadLength);
  }
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t atom = 0;
  (void)reader.read_u32(atom);
  const auto name = state.atoms().name(atom);
  if (!name) {
    return error(context, request, x11::CoreErrorCode::BadAtom, atom);
  }
  if (name->size() > std::numeric_limits<std::uint16_t>::max()) {
    return error(context, request, x11::CoreErrorCode::BadAlloc);
  }
  x11::ReplyBuilder reply(context.byte_order, context.sequence);
  reply.write_u16(static_cast<std::uint16_t>(name->size()));
  reply.write_padding(22);
  reply.write_payload(std::span<const std::uint8_t>(
      reinterpret_cast<const std::uint8_t*>(name->data()), name->size()));
  return {std::move(reply).finish()};
}


}  // namespace glasswyrm::server::request_handlers
