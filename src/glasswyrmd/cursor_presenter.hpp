#pragma once

#include "glasswyrmd/compositor_peer.hpp"
#include "input/cursor_model.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <string>

namespace glasswyrm::server {

class CursorPresenter {
 public:
  static constexpr std::uint64_t kSurfaceId = UINT64_C(0xffff000000000001);
  static constexpr std::uint64_t kFirstBufferId =
      UINT64_C(0xffff000000000100);

  CursorPresenter();
  ~CursorPresenter();
  CursorPresenter(const CursorPresenter&) = delete;
  CursorPresenter& operator=(const CursorPresenter&) = delete;

  [[nodiscard]] bool needs_update(
      const std::shared_ptr<const input::CursorImage>& image,
      std::int32_t pointer_x, std::int32_t pointer_y,
      bool visible) const noexcept;
  [[nodiscard]] bool prepare(
      std::shared_ptr<const input::CursorImage> image,
      std::int32_t pointer_x, std::int32_t pointer_y, bool visible,
      CompositorCursorSubmission& submission, std::string& error,
      bool force_buffer = false);
  void accept() noexcept;
  void reject() noexcept;
  [[nodiscard]] bool release(std::uint64_t buffer_id,
                             gwipc_buffer_release_reason reason) noexcept;
  void peer_disconnected() noexcept;
  [[nodiscard]] bool in_flight() const noexcept { return in_flight_; }

 private:
  struct Buffer;

  [[nodiscard]] static gwipc_surface_upsert project_surface(
      const input::CursorSurfacePublication& publication) noexcept;
  [[nodiscard]] static CompositorSnapshotSubmission::Buffer project_buffer(
      const Buffer& buffer) noexcept;
  [[nodiscard]] static CompositorSnapshotSubmission::Damage project_damage(
      const Buffer& buffer);

  std::unique_ptr<Buffer> current_;
  std::unique_ptr<Buffer> staged_;
  std::map<std::uint64_t, std::unique_ptr<Buffer>> retired_;
  std::shared_ptr<const input::CursorImage> accepted_image_;
  std::shared_ptr<const input::CursorImage> staged_image_;
  input::CursorSurfacePublication accepted_;
  input::CursorSurfacePublication staged_publication_;
  std::uint64_t next_buffer_id_{kFirstBufferId};
  bool accepted_valid_{};
  bool in_flight_{};
};

}  // namespace glasswyrm::server
