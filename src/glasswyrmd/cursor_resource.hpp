#pragma once

#include "input/cursor_model.hpp"

#include <memory>

namespace glasswyrm::server {

struct CursorResource {
  std::shared_ptr<const input::CursorImage> image;
};

}  // namespace glasswyrm::server
