#include "glasswyrmd/resource_table.hpp"

namespace {

glasswyrm::server::WindowCreateSpec make_window(std::uint32_t xid,
                                                std::uint32_t parent) {
  glasswyrm::server::WindowCreateSpec result;
  result.xid = xid;
  result.parent = parent;
  result.x = -4;
  result.y = 7;
  result.width = 80;
  result.height = 60;
  return result;
}

}  // namespace

int main() {
  using namespace glasswyrm::server;
  constexpr std::uint32_t base = 0x00400000;
  constexpr std::uint32_t mask = 0x001fffff;
  ResourceTable table;
  if (table.create_window(1, base, mask, make_window(base + 1, 1)) !=
          CreateWindowStatus::Success ||
      table.create_window(1, base, mask,
                          make_window(base + 2, base + 1)) !=
          CreateWindowStatus::Success ||
      table.create_window(1, base, mask, make_window(base + 3, 1)) !=
          CreateWindowStatus::Success) {
    return 1;
  }
  const auto* root = table.find_window(1);
  if (root == nullptr ||
      root->children != std::vector<std::uint32_t>{base + 1, base + 3}) {
    return 2;
  }
  CleanupResult cleanup;
  if (table.destroy_window(base + 1, &cleanup) !=
          DestroyWindowStatus::Success ||
      cleanup.resources_destroyed != 2 || table.find(base + 2) != nullptr ||
      table.find_window(1)->children != std::vector<std::uint32_t>{base + 3} ||
      table.destroy_window(1) != DestroyWindowStatus::RootPreserved ||
      !table.invariants_hold()) {
    return 3;
  }
  return 0;
}
