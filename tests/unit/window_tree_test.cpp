#include "glasswyrmd/resource_table.hpp"

#include <string_view>

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

constexpr std::uint32_t base = 0x00400000;
constexpr std::uint32_t mask = 0x001fffff;
constexpr std::uint32_t depth = 25000;

int test_basic() {
  using namespace glasswyrm::server;
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

int test_deep() {
  using namespace glasswyrm::server;
  ResourceTable deep;
  std::uint32_t parent = 1;
  for (std::uint32_t index = 1; index <= depth; ++index) {
    const auto xid = base + index;
    if (deep.create_window(1, base, mask, make_window(xid, parent)) !=
        CreateWindowStatus::Success) {
      return 4;
    }
    parent = xid;
  }
  CleanupResult deep_cleanup;
  if (deep.destroy_window(base + 1, &deep_cleanup) !=
          DestroyWindowStatus::Success ||
      deep_cleanup.resources_destroyed != depth ||
      deep.resource_count(ResourceType::Window) != 1 ||
      !deep.invariants_hold()) {
    return 2;
  }
  return 0;
}

int test_wide() {
  using namespace glasswyrm::server;
  ResourceTable wide;
  for (std::uint32_t index = 1; index <= depth; ++index) {
    if (wide.create_window(1, base, mask, make_window(base + index, 1)) !=
        CreateWindowStatus::Success) {
      return 1;
    }
  }
  const auto wide_cleanup = wide.cleanup_client(1);
  if (wide_cleanup.resources_destroyed != depth ||
      wide.resource_count(ResourceType::Window) != 1 ||
      !wide.invariants_hold()) {
    return 2;
  }
  return 0;
}

int test_mixed() {
  using namespace glasswyrm::server;
  ResourceTable mixed;
  if (mixed.create_window(1, base, mask, make_window(base + 1, 1)) !=
          CreateWindowStatus::Success ||
      mixed.create_window(2, base, mask,
                          make_window(base + 2, base + 1)) !=
          CreateWindowStatus::Success ||
      mixed.create_window(1, base, mask,
                          make_window(base + 3, base + 1)) !=
          CreateWindowStatus::Success ||
      mixed.create_window(2, base, mask,
                          make_window(base + 4, base + 2)) !=
          CreateWindowStatus::Success)
    return 1;
  constexpr std::uint32_t structure = 1U << 17U;
  constexpr std::uint32_t substructure = 1U << 19U;
  (void)mixed.set_event_selection(base + 2, 10, structure);
  (void)mixed.set_event_selection(base + 1, 20, substructure);
  const auto plan = mixed.capture_destroy_plan(base + 1);
  if (!plan || plan->postorder.size() != 4 ||
      std::vector<std::uint32_t>{plan->postorder[0].xid,
                                 plan->postorder[1].xid,
                                 plan->postorder[2].xid,
                                 plan->postorder[3].xid} !=
          std::vector<std::uint32_t>{base + 3, base + 4, base + 2,
                                     base + 1} ||
      plan->postorder[2].structure_recipients !=
          std::vector<ClientId>{10} ||
      plan->postorder[2].substructure_recipients !=
          std::vector<ClientId>{20} || plan->postorder[0].owner != 1 ||
      plan->postorder[1].owner != 2 || plan->postorder[2].owner != 2 ||
      plan->postorder[3].owner != 1 ||
      !mixed.find_window(base + 4))
    return 2;
  CleanupResult mixed_cleanup;
  if (mixed.commit_destroy_plan(*plan, &mixed_cleanup) !=
          DestroyWindowStatus::Success ||
      mixed_cleanup.resources_destroyed != 4 ||
      mixed.resource_count_by_owner(1) != 0 ||
      mixed.resource_count_by_owner(2) != 0 || !mixed.invariants_hold())
    return 3;
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) return 64;
  const std::string_view scenario = argv[1];
  if (scenario == "basic") return test_basic();
  if (scenario == "deep") return test_deep();
  if (scenario == "wide") return test_wide();
  if (scenario == "mixed") return test_mixed();
  return 64;
}
