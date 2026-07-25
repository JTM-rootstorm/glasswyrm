#pragma once

#include "backends/drm/fake_kms_api.hpp"

#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace gw::test {

inline std::vector<glasswyrm::drm::ObjectProperty> kms_properties(
    const std::initializer_list<std::string_view> names,
    std::uint32_t first_id) {
  std::vector<glasswyrm::drm::ObjectProperty> result;
  for (const auto name : names)
    result.push_back({first_id++, std::string(name), 0, 64});
  return result;
}

} // namespace gw::test
