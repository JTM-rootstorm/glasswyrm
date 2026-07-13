#pragma once

#include <glasswyrm/ipc.h>

#include <string>

namespace gw::compositor {

class Compositor;
struct PresentedFrame;
struct Scene;

class PresentationTransaction final {
public:
  [[nodiscard]] static PresentedFrame commit(Compositor& compositor,
                                             const gwipc_frame_commit& value,
                                             std::string& error);

private:
  static void release_retired_buffers(Compositor& compositor,
                                      const Scene& staged);
};

} // namespace gw::compositor
