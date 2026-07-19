#include "glasswyrmd/cursor_presenter.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <new>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <vector>

namespace glasswyrm::server {
namespace {

int create_cursor_memfd() noexcept {
#ifdef SYS_memfd_create
  return static_cast<int>(::syscall(SYS_memfd_create, "glasswyrm-cursor",
                                    MFD_CLOEXEC | MFD_ALLOW_SEALING));
#else
  errno = ENOSYS;
  return -1;
#endif
}

gwipc_sdr_color_metadata srgb() noexcept {
  gwipc_sdr_color_metadata color{};
  color.color_space = GWIPC_SDR_COLOR_SPACE_SRGB;
  color.transfer_function = GWIPC_TRANSFER_FUNCTION_SRGB;
  color.primaries = GWIPC_COLOR_PRIMARIES_SRGB;
  return color;
}

}  // namespace

struct CursorPresenter::Buffer {
  std::uint64_t id{};
  std::uint32_t width{};
  std::uint32_t height{};
  int fd{-1};

  ~Buffer() {
    if (fd >= 0) (void)::close(fd);
  }

  [[nodiscard]] static std::unique_ptr<Buffer> create(
      const std::uint64_t id, const input::CursorImage& image,
      const std::uint32_t scale, std::string& error) {
    if (id == 0 || image.width == 0 || image.height == 0 ||
        image.width > 64 || image.height > 64 ||
        image.premultiplied_argb.size() !=
            static_cast<std::size_t>(image.width) * image.height ||
        scale == 0 || scale > 4) {
      error = "invalid cursor image for compositor publication";
      return nullptr;
    }
    const auto scaled_width = image.width * scale;
    const auto scaled_height = image.height * scale;
    std::vector<std::uint32_t> pixels;
    try {
      pixels.resize(static_cast<std::size_t>(scaled_width) * scaled_height);
      for (std::uint32_t y = 0; y < scaled_height; ++y)
        for (std::uint32_t x = 0; x < scaled_width; ++x)
          pixels[static_cast<std::size_t>(y) * scaled_width + x] =
              image.premultiplied_argb[
                  static_cast<std::size_t>(y / scale) * image.width +
                  x / scale];
    } catch (const std::bad_alloc&) {
      error = "could not allocate scaled cursor buffer";
      return nullptr;
    }
    const auto bytes = pixels.size() * sizeof(std::uint32_t);
    if (bytes > static_cast<std::size_t>(std::numeric_limits<off_t>::max())) {
      error = "invalid cursor image for compositor publication";
      return nullptr;
    }
    const int fd = create_cursor_memfd();
    if (fd < 0 || ::ftruncate(fd, static_cast<off_t>(bytes)) != 0 ||
        ::pwrite(fd, pixels.data(), bytes, 0) !=
            static_cast<ssize_t>(bytes) ||
        ::fcntl(fd, F_ADD_SEALS,
                F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE | F_SEAL_SEAL) !=
            0) {
      const int saved = errno;
      if (fd >= 0) (void)::close(fd);
      error = std::string("could not publish immutable cursor buffer: ") +
              std::strerror(saved);
      return nullptr;
    }
    return std::unique_ptr<Buffer>(
        new Buffer{id, scaled_width, scaled_height, fd});
  }
};

CursorPresenter::CursorPresenter() = default;
CursorPresenter::~CursorPresenter() = default;

bool CursorPresenter::needs_update(
    const std::shared_ptr<const input::CursorImage>& image,
    const std::int32_t pointer_x, const std::int32_t pointer_y,
    const bool visible, const std::uint32_t buffer_scale) const noexcept {
  if (!image || in_flight_ || !accepted_valid_) return !in_flight_;
  return accepted_image_.get() != image.get() ||
         accepted_.x != pointer_x - image->hotspot_x ||
         accepted_.y != pointer_y - image->hotspot_y ||
         accepted_.visible != visible ||
         accepted_buffer_scale_ != buffer_scale;
}

gwipc_surface_upsert CursorPresenter::project_surface(
    const input::CursorSurfacePublication& publication,
    const std::uint32_t buffer_scale) noexcept {
  gwipc_surface_upsert surface{};
  surface.struct_size = sizeof(surface);
  surface.surface_id = publication.surface_id;
  surface.output_id = publication.output_id;
  surface.logical_x = publication.x;
  surface.logical_y = publication.y;
  surface.logical_width = publication.image->width;
  surface.logical_height = publication.image->height;
  surface.stacking = std::numeric_limits<std::int32_t>::max();
  surface.visible = publication.visible ? 1 : 0;
  surface.transform = GWIPC_TRANSFORM_NORMAL;
  surface.opacity = GWIPC_OPACITY_ONE;
  surface.scale_numerator = buffer_scale;
  surface.scale_denominator = 1;
  surface.color = srgb();
  surface.presentation_flags = GWIPC_SURFACE_PRESENTATION_CURSOR;
  surface.fullscreen_eligible = GWIPC_TRI_STATE_UNKNOWN;
  surface.direct_scanout_eligible = GWIPC_TRI_STATE_UNKNOWN;
  return surface;
}

CompositorSnapshotSubmission::Buffer CursorPresenter::project_buffer(
    const Buffer& buffer) noexcept {
  CompositorSnapshotSubmission::Buffer result;
  auto& attach = result.attach;
  attach.struct_size = sizeof(attach);
  attach.buffer_id = buffer.id;
  attach.surface_id = kSurfaceId;
  attach.width = buffer.width;
  attach.height = buffer.height;
  attach.stride = buffer.width * 4U;
  attach.storage_size = static_cast<std::uint64_t>(attach.stride) * buffer.height;
  attach.pixel_format = GWIPC_PIXEL_FORMAT_ARGB8888;
  attach.alpha_semantics = GWIPC_ALPHA_PREMULTIPLIED;
  attach.color = srgb();
  attach.synchronization = GWIPC_SYNCHRONIZATION_NONE;
  result.fd = buffer.fd;
  return result;
}

CompositorSnapshotSubmission::Damage CursorPresenter::project_damage(
    const Buffer& buffer) {
  CompositorSnapshotSubmission::Damage result;
  result.surface_id = kSurfaceId;
  result.rectangles.push_back({0, 0, buffer.width, buffer.height});
  return result;
}

bool CursorPresenter::prepare(
    std::shared_ptr<const input::CursorImage> image,
    const std::int32_t pointer_x, const std::int32_t pointer_y,
    const bool visible, CompositorCursorSubmission& submission,
    std::string& error, const bool force_buffer,
    const std::uint32_t buffer_scale) {
  error.clear();
  if (in_flight_ || !image ||
      !needs_update(image, pointer_x, pointer_y, visible, buffer_scale)) {
    error = in_flight_ ? "cursor publication is already in flight"
                       : "cursor publication has no new state";
    return false;
  }
  const bool replace = force_buffer || !current_ ||
                       accepted_image_.get() != image.get() ||
                       accepted_buffer_scale_ != buffer_scale;
  const auto buffer_id = replace ? next_buffer_id_ : current_->id;
  if (buffer_id == 0 ||
      !input::make_cursor_publication(kSurfaceId, buffer_id, 1, image,
                                      pointer_x, pointer_y, visible,
                                      staged_publication_, error))
    return false;
  if (replace) {
    staged_ = Buffer::create(buffer_id, *image, buffer_scale, error);
    if (!staged_) return false;
    if (next_buffer_id_ == std::numeric_limits<std::uint64_t>::max())
      next_buffer_id_ = 0;
    else
      ++next_buffer_id_;
  }
  submission = {};
  submission.surface = project_surface(staged_publication_, buffer_scale);
  submission.pointer_x = pointer_x;
  submission.pointer_y = pointer_y;
  if (staged_) {
    submission.buffer = project_buffer(*staged_);
    submission.damage = project_damage(*staged_);
  }
  staged_image_ = std::move(image);
  staged_buffer_scale_ = buffer_scale;
  in_flight_ = true;
  return true;
}

void CursorPresenter::accept() noexcept {
  if (!in_flight_) return;
  if (staged_) {
    if (current_) retired_.emplace(current_->id, std::move(current_));
    current_ = std::move(staged_);
  }
  accepted_ = staged_publication_;
  accepted_image_ = std::move(staged_image_);
  accepted_buffer_scale_ = staged_buffer_scale_;
  accepted_valid_ = true;
  in_flight_ = false;
}

void CursorPresenter::reject() noexcept {
  staged_.reset();
  staged_image_.reset();
  staged_publication_ = {};
  staged_buffer_scale_ = 1;
  in_flight_ = false;
}

bool CursorPresenter::release(
    const std::uint64_t buffer_id,
    const gwipc_buffer_release_reason reason) noexcept {
  if (reason == GWIPC_BUFFER_RELEASE_SURFACE_REMOVED && current_ &&
      current_->id == buffer_id) {
    current_.reset();
    accepted_image_.reset();
    accepted_ = {};
    accepted_buffer_scale_ = 1;
    accepted_valid_ = false;
    return true;
  }
  if (reason != GWIPC_BUFFER_RELEASE_REPLACED &&
      reason != GWIPC_BUFFER_RELEASE_CONSUMER_DONE)
    return false;
  return retired_.erase(buffer_id) == 1;
}

void CursorPresenter::peer_disconnected() noexcept {
  reject();
  retired_.clear();
  current_.reset();
  accepted_image_.reset();
  accepted_ = {};
  accepted_buffer_scale_ = 1;
  accepted_valid_ = false;
}

}  // namespace glasswyrm::server
