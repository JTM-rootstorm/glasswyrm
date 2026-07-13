#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace glasswyrm::drm {

enum class ConnectionStatus;

[[nodiscard]] std::string_view connector_type_name(
    std::uint32_t connector_type) noexcept;
[[nodiscard]] std::string_view connection_status_name(
    ConnectionStatus status) noexcept;
[[nodiscard]] std::string connector_name(std::uint32_t connector_type,
                                         std::uint32_t connector_type_id);

}  // namespace glasswyrm::drm
