#include "glasswyrmd/resource_table.hpp"

#include <cstdint>
#include <vector>

namespace {

glasswyrm::server::Property bytes(std::uint32_t type,
                                  std::vector<std::uint8_t> value) {
  return {type, std::move(value)};
}

glasswyrm::server::WindowCreateSpec make_window(std::uint32_t xid) {
  glasswyrm::server::WindowCreateSpec result;
  result.xid = xid;
  result.parent = 1;
  result.width = 10;
  result.height = 10;
  return result;
}

}  // namespace

int main() {
  using namespace glasswyrm::server;
  constexpr std::uint32_t base = 0x00400000;
  constexpr std::uint32_t mask = 0x001fffff;
  constexpr std::uint32_t window_id = base + 1;
  ResourceTable table;
  if (table.create_window(1, base, mask, make_window(window_id)) !=
      CreateWindowStatus::Success) {
    return 1;
  }

  if (table.change_property(window_id, 39, bytes(31, {'a', 'b', 'c'}),
                            PropertyMode::Replace) !=
          PropertyMutationStatus::Success ||
      table.change_property(window_id, 39, bytes(31, {'d'}),
                            PropertyMode::Append) !=
          PropertyMutationStatus::Success ||
      table.change_property(window_id, 39, bytes(31, {'0'}),
                            PropertyMode::Prepend) !=
          PropertyMutationStatus::Success ||
      table.total_property_bytes() != 5) {
    return 2;
  }
  const auto full = table.get_property(window_id, 39, 31, false, 0, 2);
  if (!full.present || !full.type_matched || full.value.bytes_after != 0 ||
      std::get<std::vector<std::uint8_t>>(full.value.data) !=
          std::vector<std::uint8_t>({'0', 'a', 'b', 'c', 'd'})) {
    return 3;
  }

  const auto mismatch = table.get_property(window_id, 39, 6, true, 0, 10);
  if (!mismatch.present || mismatch.type_matched || mismatch.deleted ||
      mismatch.value.type != 31 || mismatch.value.format != 8 ||
      mismatch.value.bytes_after != 5 || table.total_property_bytes() != 5) {
    return 4;
  }
  const auto partial = table.get_property(window_id, 39, 31, true, 0, 1);
  if (partial.value.bytes_after != 1 || partial.deleted ||
      table.total_property_bytes() != 5) {
    return 5;
  }
  const auto tail = table.get_property(window_id, 39, 31, true, 1, 10);
  if (tail.value.bytes_after != 0 || !tail.deleted ||
      std::get<std::vector<std::uint8_t>>(tail.value.data) !=
          std::vector<std::uint8_t>({'d'}) ||
      table.total_property_bytes() != 0) {
    return 6;
  }

  Property words{19, std::vector<std::uint16_t>{0x1234, 0xabcd}};
  if (table.change_property(window_id, 40, std::move(words),
                            PropertyMode::Replace) !=
          PropertyMutationStatus::Success ||
      table.change_property(window_id, 40, bytes(19, {1}),
                            PropertyMode::Append) !=
          PropertyMutationStatus::BadMatch ||
      table.total_property_bytes() != 4 || !table.invariants_hold()) {
    return 7;
  }

  const auto cleanup = table.cleanup_client(1);
  if (cleanup.property_bytes_released != 4 || table.total_property_bytes() != 0 ||
      !table.invariants_hold()) {
    return 8;
  }

  if (table.create_window(2, base, mask, make_window(window_id)) !=
          CreateWindowStatus::Success ||
      table.change_property(
          window_id, 39,
          bytes(31, std::vector<std::uint8_t>(kMaximumBytesPerProperty + 1, 0)),
          PropertyMode::Replace) != PropertyMutationStatus::BadAlloc ||
      table.total_property_bytes() != 0 ||
      !table.find_window(window_id)->properties.empty()) {
    return 9;
  }

  ResourceTable bounded(kScreenModel, ResourceLimits{4, 6, 2});
  if (bounded.create_window(1, base, mask, make_window(window_id)) !=
          CreateWindowStatus::Success ||
      bounded.change_property(window_id, 39, bytes(31, {1, 2, 3}),
                              PropertyMode::Replace) !=
          PropertyMutationStatus::Success ||
      bounded.change_property(window_id, 39, bytes(31, {4, 5}),
                              PropertyMode::Append) !=
          PropertyMutationStatus::BadAlloc ||
      std::get<std::vector<std::uint8_t>>(
          bounded.find_window(window_id)->properties.at(39).data) !=
          std::vector<std::uint8_t>({1, 2, 3}) ||
      bounded.change_property(window_id, 40, bytes(31, {4, 5, 6}),
                              PropertyMode::Replace) !=
          PropertyMutationStatus::Success ||
      bounded.change_property(window_id, 41, bytes(31, {}),
                              PropertyMode::Replace) !=
          PropertyMutationStatus::BadAlloc ||
      bounded.change_property(window_id, 40, bytes(31, {4, 5, 6, 7}),
                              PropertyMode::Replace) !=
          PropertyMutationStatus::BadAlloc ||
      bounded.total_property_bytes() != 6 ||
      bounded.find_window(window_id)->properties.at(40).byte_size() != 3) {
    return 10;
  }
  return 0;
}
