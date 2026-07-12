#pragma once

#include "glasswyrmd/compositor_peer.hpp"
#include "glasswyrmd/lifecycle_types.hpp"
#include "glasswyrmd/published_buffer.hpp"
#include "glasswyrmd/resource_table.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <vector>

namespace glasswyrm::server {

class ContentPresenter {
 public:
  void damage(std::uint32_t window, geometry::Rectangle rectangle);

  [[nodiscard]] bool prepare_lifecycle(
      const LifecycleSnapshot& snapshot, ResourceTable& resources,
      CompositorSnapshotSubmission& submission);
  void accept_lifecycle(const LifecycleSnapshot& snapshot,
                        ResourceTable& resources);
  void reject_lifecycle() noexcept;

  [[nodiscard]] bool prepare_content(
      const LifecycleSnapshot& snapshot, ResourceTable& resources,
      std::uint64_t commit, std::uint64_t generation,
      CompositorContentSubmission& submission);
  void accept_content() noexcept;
  void reject_content() noexcept;

  [[nodiscard]] bool release(std::uint64_t buffer_id,
                             gwipc_buffer_release_reason reason);
  void peer_disconnected() noexcept;
  [[nodiscard]] bool frame_in_flight() const noexcept { return in_flight_; }
  [[nodiscard]] bool has_pending_damage() const noexcept;
  [[nodiscard]] PublishedBufferStore& buffers() noexcept { return buffers_; }

 private:
  struct WindowContent {
    std::shared_ptr<PixelStorage> staged_storage;
    std::vector<geometry::Rectangle> pending;
    std::vector<geometry::Rectangle> inflight;
  };

  [[nodiscard]] static std::vector<geometry::Rectangle> normalize(
      std::span<const geometry::Rectangle> rectangles,
      geometry::Rectangle bounds);
  [[nodiscard]] std::shared_ptr<PixelStorage> stage_storage(
      std::uint32_t xid, std::uint32_t width, std::uint32_t height,
      ResourceTable& resources);
  [[nodiscard]] bool ensure_buffer(std::uint32_t xid,
                                   const PixelStorage& storage,
                                   bool& replaced);
  static CompositorSnapshotSubmission::Damage make_damage(
      std::uint32_t xid, std::span<const geometry::Rectangle> rectangles);

  PublishedBufferStore buffers_;
  std::map<std::uint32_t, WindowContent> windows_;
  bool in_flight_{};
};

}  // namespace glasswyrm::server
