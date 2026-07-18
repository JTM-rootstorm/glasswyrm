#pragma once

#include "core/geometry/rectangle.hpp"

#include <cstdint>
#include <vector>

namespace glasswyrm::server {

enum class DamageReportLevel : std::uint8_t {
  BoundingBox = 2,
  NonEmpty = 3,
};

struct DamageResource {
  std::uint32_t drawable{};
  DamageReportLevel level{DamageReportLevel::NonEmpty};
  std::vector<geometry::Rectangle> accumulated;
  bool non_empty_event_sent{false};
};

struct DamageNotification {
  std::uint64_t client{};
  std::uint32_t damage{};
  std::uint32_t drawable{};
  DamageReportLevel level{DamageReportLevel::NonEmpty};
  geometry::Rectangle area{};
  geometry::Rectangle geometry{};
};

}  // namespace glasswyrm::server
