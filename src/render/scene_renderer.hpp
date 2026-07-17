#pragma once

#include "backends/output/software_frame.hpp"
#include "compositor/buffer.hpp"
#include "compositor/scene.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <string>

namespace gw::render {

inline constexpr std::uint64_t kMaximumGlTextureCacheBytes =
    512ULL * 1024ULL * 1024ULL;

using BufferMappingMap =
    std::map<std::uint64_t, std::shared_ptr<compositor::BufferMapping>>;
using SurfaceAttachmentMap = std::map<std::uint64_t, std::uint64_t>;

enum class RendererRequest { Software, Gles, Auto };
enum class RenderDisposition { Complete, InvalidFrame, InvalidBuffer, Fatal };

struct RendererMetrics {
  std::uint64_t damage_rectangles{};
  std::uint64_t texture_uploads{};
  std::uint64_t texture_upload_bytes{};
  std::uint64_t texture_cache_bytes{};
  std::uint64_t readback_bytes{};
};

struct RenderFrameRequest {
  const compositor::Scene& scene;
  std::span<const std::uint64_t> stacking_order;
  const BufferMappingMap& mappings;
  const SurfaceAttachmentMap& attachments;
  std::span<const compositor::Rectangle> damage;
  const glasswyrm::output::SoftwareFrame* previous{};
  std::uint64_t commit_id{};
  std::uint64_t generation{};
  std::uint64_t ordinal{};
};

struct RenderFrameResult {
  RenderDisposition disposition{RenderDisposition::InvalidFrame};
  glasswyrm::output::SoftwareFrame frame;
  RendererMetrics metrics;
  std::string selected_renderer;
  std::string fallback_reason;
  std::string error;

  [[nodiscard]] bool complete() const noexcept {
    return disposition == RenderDisposition::Complete;
  }
};

class SceneRenderer {
public:
  virtual ~SceneRenderer() = default;
  [[nodiscard]] virtual RenderFrameResult
  render(const RenderFrameRequest& request) = 0;
  virtual void disconnect() noexcept = 0;
};

[[nodiscard]] const char* renderer_request_name(RendererRequest request) noexcept;

} // namespace gw::render
