#pragma once

#include "output/model/types.hpp"
#include "protocol/x11/screen_model.hpp"

#include <optional>

namespace glasswyrm::server {

[[nodiscard]] std::optional<gw::protocol::x11::ScreenModel>
derive_output_screen_model(
    const output::OutputLayout& layout,
    gw::protocol::x11::ScreenModel fixed =
        gw::protocol::x11::kScreenModel);

}  // namespace glasswyrm::server
