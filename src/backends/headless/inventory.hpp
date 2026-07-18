#pragma once

#include "output/model/types.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace glasswyrm::headless {

inline constexpr std::uint32_t kDefaultOutputWidth = 1024;
inline constexpr std::uint32_t kDefaultOutputHeight = 768;
inline constexpr std::uint32_t kDefaultOutputRefreshMillihertz = 60'000;
inline constexpr std::string_view kDefaultOutputName = "HEADLESS-1";

struct OutputRequest {
  std::string name;
  std::uint32_t width{kDefaultOutputWidth};
  std::uint32_t height{kDefaultOutputHeight};
  std::uint32_t refresh_millihertz{kDefaultOutputRefreshMillihertz};

  friend bool operator==(const OutputRequest &, const OutputRequest &) = default;
};

[[nodiscard]] constexpr bool
valid_output_name(const std::string_view name) noexcept {
  if (name.empty() || name.size() > output::kMaximumOutputNameBytes)
    return false;
  const auto is_alpha = [](const char value) {
    return (value >= 'A' && value <= 'Z') ||
           (value >= 'a' && value <= 'z');
  };
  const auto is_digit = [](const char value) {
    return value >= '0' && value <= '9';
  };
  if (!is_alpha(name.front()) && !is_digit(name.front()))
    return false;
  for (const char value : name)
    if (!is_alpha(value) && !is_digit(value) && value != '-' && value != '_' &&
        value != '.')
      return false;
  return true;
}

class OutputInventory final {
public:
  OutputInventory(OutputInventory &&) noexcept = default;
  OutputInventory &operator=(OutputInventory &&) = delete;
  OutputInventory(const OutputInventory &) = delete;
  OutputInventory &operator=(const OutputInventory &) = delete;

  [[nodiscard]] static std::optional<OutputInventory>
  build(std::span<const OutputRequest> requests, std::string &error);

  [[nodiscard]] const output::OutputLayout &layout() const noexcept {
    return layout_;
  }
  [[nodiscard]] bool uses_historical_default() const noexcept {
    return uses_historical_default_;
  }

private:
  explicit OutputInventory(output::OutputLayout layout,
                           bool uses_historical_default)
      : layout_(std::move(layout)),
        uses_historical_default_(uses_historical_default) {}

  output::OutputLayout layout_;
  bool uses_historical_default_{};
};

} // namespace glasswyrm::headless
