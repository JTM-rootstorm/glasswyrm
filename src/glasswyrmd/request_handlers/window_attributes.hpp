#pragma once

#include "glasswyrmd/request_handlers/common.hpp"
#include "protocol/x11/byte_cursor.hpp"

#include <cstdint>
#include <optional>

namespace glasswyrm::server::request_handlers {

struct DecodedWindowAttributes {
  WindowAttributes attributes;
  std::optional<std::uint32_t> event_mask;
  x11::CoreErrorCode error{x11::CoreErrorCode::BadImplementation};
  std::uint32_t bad_value{0};
  bool success{false};
};

[[nodiscard]] DecodedWindowAttributes decode_window_attributes(
    x11::ByteReader& reader, std::uint32_t value_mask,
    WindowAttributes attributes, const ResourceTable& resources);

}  // namespace glasswyrm::server::request_handlers
