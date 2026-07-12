#include "glasswyrmd/resource_table.hpp"

namespace {

using glasswyrm::server::CreateWindowStatus;
using glasswyrm::server::ResourceTable;
using glasswyrm::server::WindowCreateSpec;

WindowCreateSpec window(std::uint32_t xid, std::uint32_t parent = 1) {
  WindowCreateSpec result;
  result.xid = xid;
  result.parent = parent;
  result.width = 100;
  result.height = 50;
  return result;
}

}  // namespace

int main() {
  constexpr std::uint32_t base_a = 0x00400000;
  constexpr std::uint32_t base_b = 0x00800000;
  constexpr std::uint32_t mask = 0x001fffff;
  ResourceTable table;

  if (!table.invariants_hold() || table.resource_count_by_owner(1) != 0 ||
      table.valid_new_resource_id(0, base_a, mask) ||
      table.valid_new_resource_id(1, base_a, mask) ||
      table.valid_new_resource_id(base_b + 1, base_a, mask)) {
    return 1;
  }
  if (table.create_window(1, base_a, mask, window(base_a + 1)) !=
          CreateWindowStatus::Success ||
      table.create_window(1, base_a, mask, window(base_a + 1)) !=
          CreateWindowStatus::BadIdChoice ||
      table.create_window(2, base_b, mask,
                          window(base_b + 1, base_a + 1)) !=
          CreateWindowStatus::Success) {
    return 2;
  }
  if (table.resource_count_by_owner(1) != 1 ||
      table.resource_count_by_owner(2) != 1 || !table.invariants_hold()) {
    return 3;
  }
  if (!table.set_event_selection(1, 1, 0x00020000) ||
      !table.set_event_selection(1, 2, 0x00080000) ||
      table.event_selection(1, 1) != 0x00020000 ||
      table.all_event_selections(1) != 0x000a0000) {
    return 6;
  }

  const auto cleanup = table.cleanup_client(1);
  if (cleanup.resources_destroyed != 2 || table.find(base_a + 1) != nullptr ||
      table.find(base_b + 1) != nullptr || table.find_window(1) == nullptr ||
      !table.invariants_hold()) {
    return 4;
  }
  if (table.event_selection(1, 1) != 0 ||
      table.event_selection(1, 2) != 0x00080000 ||
      !table.set_event_selection(1, 2, 0) ||
      table.all_event_selections(1) != 0) {
    return 7;
  }
  if (table.create_window(3, base_a, mask, window(base_a + 1)) !=
      CreateWindowStatus::Success) {
    return 5;
  }
  return 0;
}
