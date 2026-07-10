#pragma once

#include "glasswyrmd/atom_table.hpp"
#include "glasswyrmd/resource_table.hpp"

namespace glasswyrm::server {

class ServerState {
 public:
  explicit ServerState(ScreenModel screen = kScreenModel)
      : screen_(screen), resources_(screen) {}

  [[nodiscard]] const ScreenModel& screen() const noexcept { return screen_; }
  [[nodiscard]] ResourceTable& resources() noexcept { return resources_; }
  [[nodiscard]] const ResourceTable& resources() const noexcept {
    return resources_;
  }
  [[nodiscard]] AtomTable& atoms() noexcept { return atoms_; }
  [[nodiscard]] const AtomTable& atoms() const noexcept { return atoms_; }

  [[nodiscard]] CleanupResult cleanup_client(ClientId owner) {
    return resources_.cleanup_client(owner);
  }
  [[nodiscard]] bool invariants_hold() const noexcept {
    return resources_.invariants_hold();
  }

 private:
  ScreenModel screen_;
  ResourceTable resources_;
  AtomTable atoms_;
};

}  // namespace glasswyrm::server
