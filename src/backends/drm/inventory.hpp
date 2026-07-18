#pragma once

#include "backends/drm/drm_api.hpp"
#include "output/model/types.hpp"

#include <cstddef>
#include <optional>
#include <string>

namespace glasswyrm::drm {

struct DrmInventorySelection {
  std::size_t connector_index{};
  std::size_t mode_index{};
};

struct DrmOutputInventory {
  output::OutputLayout layout;
  bool edid_participated{};
};

[[nodiscard]] std::optional<DrmOutputInventory>
build_drm_output_inventory(const DeviceSnapshot &snapshot,
                           DrmInventorySelection selection, std::string &error);

} // namespace glasswyrm::drm
