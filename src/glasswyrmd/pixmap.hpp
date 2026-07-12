#pragma once
#include "glasswyrmd/pixel_storage.hpp"
#include <cstdint>
#include <memory>
namespace glasswyrm::server {
struct PixmapResource {
  std::uint32_t root{};
  std::uint8_t depth{24};
  std::uint16_t width{}, height{};
  std::shared_ptr<PixelStorage> storage;
};
}  // namespace glasswyrm::server
