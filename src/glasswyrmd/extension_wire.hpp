#pragma once

#include "glasswyrmd/extension_registry.hpp"
#include "protocol/x11/byte_order.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace glasswyrm::server {

[[nodiscard]] std::optional<std::vector<std::uint8_t>>
encode_extension_error(gw::protocol::x11::ByteOrder order,
                       const ExtensionDescriptor& extension,
                       std::uint8_t relative_error, std::uint64_t sequence,
                       std::uint32_t bad_value, std::uint8_t major_opcode,
                       std::uint8_t minor_opcode);

[[nodiscard]] std::optional<std::vector<std::uint8_t>>
encode_extension_event(gw::protocol::x11::ByteOrder order,
                       const ExtensionDescriptor& extension,
                       std::uint8_t relative_event, std::uint64_t sequence,
                       std::uint8_t detail,
                       std::span<const std::uint8_t> event_body,
                       bool send_event = false);

}  // namespace glasswyrm::server
