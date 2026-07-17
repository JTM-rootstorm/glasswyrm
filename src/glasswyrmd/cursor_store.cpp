#include "glasswyrmd/resource_table.hpp"

#include <algorithm>
#include <new>
#include <string>

namespace glasswyrm::server {

CreateCursorStatus ResourceTable::create_cursor(
    const ClientId owner, const std::uint32_t resource_base,
    const std::uint32_t resource_mask, const std::uint32_t xid,
    std::shared_ptr<const input::CursorImage> image) {
  if (!valid_new_resource_id(xid, resource_base, resource_mask))
    return CreateCursorStatus::BadIdChoice;
  if (!image) return CreateCursorStatus::BadAlloc;
  const auto client_cursors = static_cast<std::size_t>(std::count_if(
      resources_.begin(), resources_.end(), [&](const auto& entry) {
        return entry.second.type == ResourceType::Cursor &&
               entry.second.owner == owner;
      }));
  if (client_cursors >= limits_.maximum_cursors_per_client ||
      total_cursor_bytes_ > limits_.maximum_total_cursor_bytes ||
      image->byte_size() >
          limits_.maximum_total_cursor_bytes - total_cursor_bytes_)
    return CreateCursorStatus::BadAlloc;
  try {
    const auto bytes = image->byte_size();
    resources_.emplace(
        xid, ResourceRecord{ResourceType::Cursor, owner,
                            CursorResource{std::move(image)}});
    try {
      resources_by_owner_[owner].push_back(xid);
    } catch (...) {
      resources_.erase(xid);
      throw;
    }
    total_cursor_bytes_ += bytes;
  } catch (const std::bad_alloc&) {
    return CreateCursorStatus::BadAlloc;
  }
  return CreateCursorStatus::Success;
}

FreeCursorStatus ResourceTable::free_cursor(const std::uint32_t xid) {
  const auto* cursor = find_cursor(xid);
  const auto* record = find(xid);
  if (!cursor || !record || !record->owner) return FreeCursorStatus::BadCursor;
  const auto owner = *record->owner;
  const auto bytes = cursor->image->byte_size();
  for (auto& [resource_xid, resource] : resources_) {
    static_cast<void>(resource_xid);
    auto* window = std::get_if<WindowResource>(&resource.payload);
    if (window && !window->attributes.cursor_inherit &&
        window->attributes.cursor == xid) {
      // The XID dies immediately; the shared image remains active until the
      // window changes its cursor or is destroyed.
      window->attributes.cursor = 0;
    }
  }
  resources_.erase(xid);
  auto owned = resources_by_owner_.find(owner);
  if (owned != resources_by_owner_.end()) {
    std::erase(owned->second, xid);
    if (owned->second.empty()) resources_by_owner_.erase(owned);
  }
  total_cursor_bytes_ -= bytes;
  return FreeCursorStatus::Success;
}

RecolorCursorStatus ResourceTable::recolor_cursor(
    const std::uint32_t xid, const input::CursorColor foreground,
    const input::CursorColor background) {
  const auto* cursor = find_cursor(xid);
  if (!cursor) return RecolorCursorStatus::BadCursor;
  std::string recolor_error;
  auto replacement = input::recolor_cursor(*cursor->image, foreground,
                                           background, recolor_error);
  if (!replacement) return RecolorCursorStatus::BadAlloc;
  const auto old_bytes = cursor->image->byte_size();
  const auto new_bytes = replacement->byte_size();
  if (total_cursor_bytes_ < old_bytes ||
      total_cursor_bytes_ - old_bytes > limits_.maximum_total_cursor_bytes ||
      new_bytes > limits_.maximum_total_cursor_bytes -
                      (total_cursor_bytes_ - old_bytes))
    return RecolorCursorStatus::BadAlloc;

  auto* mutable_cursor =
      std::get_if<CursorResource>(&resources_.find(xid)->second.payload);
  for (auto& [resource_xid, resource] : resources_) {
    static_cast<void>(resource_xid);
    auto* window = std::get_if<WindowResource>(&resource.payload);
    if (window && !window->attributes.cursor_inherit &&
        window->attributes.cursor == xid)
      window->attributes.cursor_image = replacement;
  }
  mutable_cursor->image = std::move(replacement);
  total_cursor_bytes_ = total_cursor_bytes_ - old_bytes + new_bytes;
  return RecolorCursorStatus::Success;
}

}  // namespace glasswyrm::server
