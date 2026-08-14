#include "glasswyrmd/resource_table.hpp"

#include <algorithm>
#include <new>
#include <string>

namespace glasswyrm::server {

void ResourceTable::reap_released_cursor_images() const noexcept {
  std::erase_if(cursor_allocations_, [&](const CursorAllocation& allocation) {
    if (!allocation.image.expired()) return false;
    if (allocation.bytes <= total_cursor_bytes_)
      total_cursor_bytes_ -= allocation.bytes;
    else
      total_cursor_bytes_ = 0;
    return true;
  });
}

CreateCursorStatus ResourceTable::create_cursor(
    const ClientId owner, const std::uint32_t resource_base,
    const std::uint32_t resource_mask, const std::uint32_t xid,
    std::shared_ptr<const input::CursorImage> image) {
  if (!valid_new_resource_id(xid, resource_base, resource_mask))
    return CreateCursorStatus::BadIdChoice;
  if (!image) return CreateCursorStatus::BadAlloc;
  reap_released_cursor_images();
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
    cursor_allocations_.push_back(CursorAllocation{image, bytes});
    try {
      insert_resource(
          xid,
          ResourceRecord{ResourceType::Cursor, owner,
                         CursorResource{std::move(image)}});
    } catch (...) {
      cursor_allocations_.pop_back();
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
  auto image = cursor->image;
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
  erase_resource(xid);
  image.reset();
  reap_released_cursor_images();
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
  reap_released_cursor_images();
  auto old_image = cursor->image;
  const auto old_bytes = cursor->image->byte_size();
  const auto new_bytes = replacement->byte_size();
  std::size_t replaced_references = 1;
  for (const auto& [resource_xid, resource] : resources_) {
    static_cast<void>(resource_xid);
    const auto* window = std::get_if<WindowResource>(&resource.payload);
    if (window && !window->attributes.cursor_inherit &&
        window->attributes.cursor == xid &&
        window->attributes.cursor_image == old_image)
      ++replaced_references;
  }
  const bool old_image_remains =
      old_image.use_count() > static_cast<long>(replaced_references + 1U);
  const auto replaced_bytes = old_image_remains ? 0U : old_bytes;
  if (total_cursor_bytes_ < replaced_bytes ||
      total_cursor_bytes_ - replaced_bytes >
          limits_.maximum_total_cursor_bytes ||
      new_bytes > limits_.maximum_total_cursor_bytes -
                      (total_cursor_bytes_ - replaced_bytes))
    return RecolorCursorStatus::BadAlloc;

  try {
    cursor_allocations_.push_back(CursorAllocation{replacement, new_bytes});
  } catch (const std::bad_alloc&) {
    return RecolorCursorStatus::BadAlloc;
  }

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
  total_cursor_bytes_ += new_bytes;
  old_image.reset();
  reap_released_cursor_images();
  return RecolorCursorStatus::Success;
}

}  // namespace glasswyrm::server
