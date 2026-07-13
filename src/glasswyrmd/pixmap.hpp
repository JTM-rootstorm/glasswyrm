#pragma once
#include "glasswyrmd/bitmap_storage.hpp"
#include "glasswyrmd/pixel_storage.hpp"
#include <cstdint>
#include <memory>
#include <variant>
namespace glasswyrm::server {
struct PixmapResource {
  std::uint32_t root{};
  std::uint8_t depth{24};
  std::uint16_t width{}, height{};
  std::variant<std::shared_ptr<BitmapStorage>, std::shared_ptr<PixelStorage>>
      storage;

  [[nodiscard]] PixelStorage* pixels() noexcept {
    const auto value = std::get_if<std::shared_ptr<PixelStorage>>(&storage);
    return value ? value->get() : nullptr;
  }
  [[nodiscard]] const PixelStorage* pixels() const noexcept {
    const auto value = std::get_if<std::shared_ptr<PixelStorage>>(&storage);
    return value ? value->get() : nullptr;
  }
  [[nodiscard]] BitmapStorage* bitmap() noexcept {
    const auto value = std::get_if<std::shared_ptr<BitmapStorage>>(&storage);
    return value ? value->get() : nullptr;
  }
  [[nodiscard]] const BitmapStorage* bitmap() const noexcept {
    const auto value = std::get_if<std::shared_ptr<BitmapStorage>>(&storage);
    return value ? value->get() : nullptr;
  }
  [[nodiscard]] std::size_t byte_size() const noexcept {
    return std::visit([](const auto& value) { return value->byte_size(); },
                      storage);
  }
};
}  // namespace glasswyrm::server
