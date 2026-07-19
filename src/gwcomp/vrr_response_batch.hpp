#pragma once

#include "gwcomp/vrr_runtime.hpp"

#include <glasswyrm/ipc.h>

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace gw::compositor {

struct VrrResponseMessage {
  std::uint16_t type{};
  std::uint16_t flags{};
  std::vector<std::uint8_t> payload;
};

class VrrResponseBatch final {
public:
  using ReleaseMap =
      std::map<std::uint64_t, gwipc_buffer_release_reason>;

  [[nodiscard]] static std::optional<VrrResponseBatch> preflight(
      const PreparedVrrFrame& prepared, const gwipc_frame_commit& commit,
      gwipc_frame_result result, const ReleaseMap& releases,
      std::string& error);

  [[nodiscard]] bool finalize(const CompletedVrrFrame& completed,
                              std::string& error);

  [[nodiscard]] bool ready() const noexcept { return ready_; }
  [[nodiscard]] std::size_t reserved_messages() const noexcept {
    return reserved_messages_;
  }
  [[nodiscard]] std::size_t reserved_payload_bytes() const noexcept {
    return reserved_payload_bytes_;
  }
  [[nodiscard]] const std::vector<VrrResponseMessage>& messages() const noexcept {
    return messages_;
  }

private:
  gwipc_frame_commit commit_{};
  gwipc_frame_result result_{GWIPC_FRAME_REJECTED_INCOMPLETE_METADATA};
  ReleaseMap releases_;
  std::size_t reserved_messages_{};
  std::size_t reserved_payload_bytes_{};
  std::vector<VrrResponseMessage> messages_;
  bool ready_{};
};

} // namespace gw::compositor
