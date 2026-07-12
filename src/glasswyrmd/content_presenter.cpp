#include "glasswyrmd/content_presenter.hpp"

#include "core/geometry/region.hpp"

#include <algorithm>
#include <utility>

namespace glasswyrm::server {

void ContentPresenter::damage(const std::uint32_t window,
                              const geometry::Rectangle rectangle) {
  if (!rectangle.empty()) windows_[window].pending.push_back(rectangle);
}

std::vector<geometry::Rectangle> ContentPresenter::normalize(
    const std::span<const geometry::Rectangle> rectangles,
    const geometry::Rectangle bounds) {
  geometry::Region region(bounds);
  for (const auto rectangle : rectangles) region.add(rectangle);
  return region.rectangles();
}

std::shared_ptr<PixelStorage> ContentPresenter::stage_storage(
    const std::uint32_t xid, const std::uint32_t width,
    const std::uint32_t height, ResourceTable& resources) {
  auto& content = windows_[xid];
  const auto* window = resources.find_window(xid);
  std::shared_ptr<PixelStorage> source =
      window != nullptr ? window->storage : nullptr;
  if (content.staged_storage && content.staged_storage->width() == width &&
      content.staged_storage->height() == height)
    return content.staged_storage;
  if (source && source->width() == width && source->height() == height) {
    content.staged_storage = source;
    return source;
  }
  std::optional<PixelStorage> replacement =
      source ? source->resize_preserving_overlap(width, height)
             : PixelStorage::create(width, height);
  if (!replacement) return nullptr;
  content.staged_storage =
      std::make_shared<PixelStorage>(std::move(*replacement));
  content.pending = {{0, 0, width, height}};
  return content.staged_storage;
}

PublishedWindowBuffer* ContentPresenter::ensure_buffer(
    const std::uint32_t xid, const PixelStorage& storage, bool& replaced) {
  replaced = false;
  if (auto* current = buffers_.current(xid)) {
    if (current->width() == storage.width() &&
        current->height() == storage.height() && !force_replacement_)
      return current;
    replaced = true;
  }
  auto& content = windows_[xid];
  if (content.staged_buffer &&
      content.staged_buffer->width() == storage.width() &&
      content.staged_buffer->height() == storage.height())
    return content.staged_buffer.get();
  const auto id = buffers_.next_buffer_id();
  content.staged_buffer = PublishedWindowBuffer::create(id, xid, storage);
  return content.staged_buffer.get();
}

CompositorSnapshotSubmission::Damage ContentPresenter::make_damage(
    const std::uint32_t xid,
    const std::span<const geometry::Rectangle> rectangles) {
  CompositorSnapshotSubmission::Damage damage;
  damage.surface_id = (UINT64_C(1) << 32U) | xid;
  damage.rectangles.reserve(rectangles.size());
  for (const auto rectangle : rectangles)
    damage.rectangles.push_back(
        {rectangle.x, rectangle.y, rectangle.width, rectangle.height});
  return damage;
}

bool ContentPresenter::prepare_lifecycle(
    const LifecycleSnapshot& snapshot, ResourceTable& resources,
    CompositorSnapshotSubmission& submission) {
  if (in_flight_) return false;
  for (const auto& [xid, projected] : snapshot.windows) {
    auto storage = stage_storage(xid, projected.applied_width,
                                 projected.applied_height, resources);
    if (!storage) return false;
    bool replaced = false;
    auto* buffer = ensure_buffer(xid, *storage, replaced);
    if (!buffer) return false;
    auto& content = windows_[xid];
    auto dirty = normalize(content.pending,
                           {0, 0, storage->width(), storage->height()});
    if (!buffer->announced() || replaced)
      dirty = {{0, 0, storage->width(), storage->height()}};
    if (!dirty.empty() && !buffer->copy_from(*storage, dirty)) return false;
    if (!buffer->announced()) {
      CompositorSnapshotSubmission::Buffer attachment;
      attachment.attach.struct_size = sizeof(attachment.attach);
      attachment.attach.buffer_id = buffer->buffer_id();
      attachment.attach.surface_id = (UINT64_C(1) << 32U) | xid;
      attachment.attach.width = buffer->width();
      attachment.attach.height = buffer->height();
      attachment.attach.stride = buffer->stride();
      attachment.attach.storage_size = buffer->size();
      attachment.attach.pixel_format = GWIPC_PIXEL_FORMAT_XRGB8888;
      attachment.attach.alpha_semantics = GWIPC_ALPHA_OPAQUE;
      attachment.attach.color.color_space = GWIPC_SDR_COLOR_SPACE_SRGB;
      attachment.attach.color.transfer_function = GWIPC_TRANSFER_FUNCTION_SRGB;
      attachment.attach.color.primaries = GWIPC_COLOR_PRIMARIES_SRGB;
      attachment.attach.synchronization = GWIPC_SYNCHRONIZATION_NONE;
      attachment.fd = buffer->fd();
      submission.buffers.push_back(attachment);
    }
    if (projected.policy_visible && !dirty.empty())
      submission.damages.push_back(make_damage(xid, dirty));
    content.pending.clear();
    content.inflight = std::move(dirty);
  }
  in_flight_ = true;
  return true;
}

bool ContentPresenter::prepare_replay(
    const LifecycleSnapshot& snapshot, ResourceTable& resources,
    CompositorSnapshotSubmission& submission) {
  force_replacement_ = true;
  const bool result = prepare_lifecycle(snapshot, resources, submission);
  force_replacement_ = false;
  return result;
}

void ContentPresenter::accept_lifecycle(const LifecycleSnapshot& snapshot,
                                        ResourceTable& resources) {
  for (const auto& [xid, projected] : snapshot.windows) {
    (void)projected;
    auto found = windows_.find(xid);
    if (found == windows_.end()) continue;
    if (auto* window = resources.find_window(xid); window != nullptr) {
      if (window->storage && found->second.staged_storage != window->storage &&
          !found->second.pending.empty()) {
        auto latest = window->storage->resize_preserving_overlap(
            found->second.staged_storage->width(),
            found->second.staged_storage->height());
        if (latest)
          found->second.staged_storage =
              std::make_shared<PixelStorage>(std::move(*latest));
      }
      window->storage = found->second.staged_storage;
    }
    found->second.inflight.clear();
    found->second.staged_storage.reset();
    if (found->second.staged_buffer) {
      if (buffers_.current(xid) &&
          !buffers_.retire(xid, PublishedBufferRetirement::Replaced))
        continue;
      auto replacement = std::move(found->second.staged_buffer);
      replacement->set_announced(true);
      (void)buffers_.install(xid, std::move(replacement));
    }
    if (auto* buffer = buffers_.current(xid)) buffer->set_announced(true);
  }
  for (auto iterator = windows_.begin(); iterator != windows_.end();) {
    if (snapshot.windows.contains(iterator->first)) {
      ++iterator;
      continue;
    }
    if (buffers_.current(iterator->first))
      (void)buffers_.retire(iterator->first,
                            PublishedBufferRetirement::SurfaceRemoved);
    iterator = windows_.erase(iterator);
  }
  in_flight_ = false;
}

void ContentPresenter::reject_lifecycle() noexcept {
  for (auto& [xid, content] : windows_) {
    (void)xid;
    content.pending.insert(content.pending.end(), content.inflight.begin(),
                           content.inflight.end());
    content.inflight.clear();
    content.staged_storage.reset();
    if (content.staged_buffer) {
      discarded_buffers_.insert(content.staged_buffer->buffer_id());
      content.staged_buffer.reset();
    }
  }
  in_flight_ = false;
}

bool ContentPresenter::prepare_content(
    const LifecycleSnapshot& snapshot, ResourceTable& resources,
    const std::uint64_t commit, const std::uint64_t generation,
    CompositorContentSubmission& submission) {
  if (in_flight_ || commit == 0 || generation == 0) return false;
  submission = {commit, generation, {}};
  for (const auto& [xid, projected] : snapshot.windows) {
    auto found = windows_.find(xid);
    auto* window = resources.find_window(xid);
    auto* buffer = buffers_.current(xid);
    if (!projected.policy_visible || found == windows_.end() ||
        found->second.pending.empty() || window == nullptr ||
        !window->storage || buffer == nullptr)
      continue;
    auto dirty = normalize(found->second.pending,
                           {0, 0, window->storage->width(),
                            window->storage->height()});
    if (dirty.empty()) continue;
    if (!buffer->copy_from(*window->storage, dirty)) return false;
    found->second.pending.clear();
    found->second.inflight = dirty;
    submission.damages.push_back(make_damage(xid, dirty));
  }
  if (submission.damages.empty()) return false;
  in_flight_ = true;
  return true;
}

void ContentPresenter::accept_content() noexcept {
  for (auto& [xid, content] : windows_) {
    (void)xid;
    content.inflight.clear();
  }
  in_flight_ = false;
}

void ContentPresenter::reject_content() noexcept {
  for (auto& [xid, content] : windows_) {
    (void)xid;
    content.pending.insert(content.pending.end(), content.inflight.begin(),
                           content.inflight.end());
    content.inflight.clear();
  }
  in_flight_ = false;
}

bool ContentPresenter::release(const std::uint64_t buffer_id,
                               const gwipc_buffer_release_reason reason) {
  if (reason == GWIPC_BUFFER_RELEASE_CONSUMER_DONE &&
      discarded_buffers_.erase(buffer_id) == 1)
    return true;
  const auto translated =
      reason == GWIPC_BUFFER_RELEASE_REPLACED
          ? std::optional(PublishedBufferRetirement::Replaced)
      : reason == GWIPC_BUFFER_RELEASE_SURFACE_REMOVED
          ? std::optional(PublishedBufferRetirement::SurfaceRemoved)
      : reason == GWIPC_BUFFER_RELEASE_CONSUMER_DONE
          ? std::optional(PublishedBufferRetirement::ConsumerDone)
          : std::nullopt;
  return translated && buffers_.release(buffer_id, *translated);
}

void ContentPresenter::peer_disconnected() noexcept {
  reject_content();
  buffers_.peer_disconnected();
  discarded_buffers_.clear();
}

bool ContentPresenter::has_pending_damage() const noexcept {
  return std::any_of(windows_.begin(), windows_.end(), [](const auto& item) {
    return !item.second.pending.empty();
  });
}

}  // namespace glasswyrm::server
