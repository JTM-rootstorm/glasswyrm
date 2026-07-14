#pragma once

#include <glasswyrm/ipc.h>

#include "backends/output/software_frame.hpp"
#include "compositor/buffer.hpp"
#include "compositor/scene.hpp"
#include "gwcomp/compositor.hpp"

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>

namespace gw::compositor {

struct Scene;

class PresentationTransaction final {
public:
  [[nodiscard]] static PresentedFrame commit(Compositor& compositor,
                                             const gwipc_frame_commit& value,
                                             std::string& error);
  [[nodiscard]] static PresentationCompletion service(
      Compositor& compositor, short revents, std::string& error);
  [[nodiscard]] static int timeout_ms(const Compositor& compositor);
  static void abort(Compositor& compositor,
                    std::string_view reason = {}) noexcept;

private:
  using AttachmentMap = std::map<std::uint64_t, std::uint64_t>;
  using ReleaseMap =
      std::map<std::uint64_t, gwipc_buffer_release_reason>;

  PresentationTransaction(SceneModel candidate, AttachmentMap attachments,
                          ReleaseMap releases,
                          glasswyrm::output::SoftwareFrame frame,
                          gwipc_frame_commit commit, PresentedFrame presented,
                          std::uint64_t token,
                          std::chrono::steady_clock::time_point deadline);
  [[nodiscard]] static ReleaseMap calculate_retired_buffers(
      const Compositor& compositor, const Scene& staged);
  static void release_retired_buffers(Compositor& compositor,
                                      const Scene& staged);
  [[nodiscard]] PresentedFrame promote(Compositor& compositor,
                                       std::uint64_t visible_hash);

  SceneModel candidate_;
  AttachmentMap attachments_;
  ReleaseMap releases_;
  glasswyrm::output::SoftwareFrame frame_;
  gwipc_frame_commit commit_{};
  PresentedFrame presented_;
  std::uint64_t token_{};
  std::chrono::steady_clock::time_point deadline_;
};

} // namespace gw::compositor
