#pragma once

#include "backends/drm/kms_api.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace glasswyrm::drm {

struct PipelineIds {
  std::uint32_t connector{}, crtc{}, primary_plane{};
};

struct SavedKmsState {
  PipelineIds pipeline;
  std::uint32_t connector_crtc_id{};
  KmsCrtcState crtc;
  KmsPlaneState primary_plane;
  std::vector<std::uint32_t> connector_ids;
  AtomicPropertyCache properties;
  bool atomic{};
};

class ModeBlob {
public:
  ModeBlob() = default;
  ~ModeBlob();
  ModeBlob(const ModeBlob &) = delete;
  ModeBlob &operator=(const ModeBlob &) = delete;
  ModeBlob(ModeBlob &&) noexcept;
  ModeBlob &operator=(ModeBlob &&) noexcept;
  [[nodiscard]] bool create(KmsApi &, int fd, const KmsMode &,
                            std::string &error);
  void reset() noexcept;
  void abandon() noexcept;
  [[nodiscard]] std::uint32_t id() const noexcept { return id_; }

private:
  KmsApi *api_{};
  int fd_{-1};
  std::uint32_t id_{};
};

[[nodiscard]] bool
capture_saved_state(KmsApi &, int fd, PipelineIds,
                    std::span<const std::uint32_t> connector_ids, bool atomic,
                    SavedKmsState &, std::string &error);
[[nodiscard]] std::vector<AtomicPropertyValue>
atomic_initial_request(PipelineIds, const AtomicPropertyCache &,
                       std::uint32_t mode_blob, std::uint32_t framebuffer,
                       std::uint32_t width, std::uint32_t height,
                       bool include_vrr = true);
[[nodiscard]] std::vector<AtomicPropertyValue>
atomic_flip_request(PipelineIds, const AtomicPropertyCache &,
                    std::uint32_t framebuffer);
[[nodiscard]] bool restore_saved_state(KmsApi &, int fd, const SavedKmsState &,
                                       std::string &error);
[[nodiscard]] bool verify_saved_state(KmsApi &, int fd, const SavedKmsState &,
                                      std::string &error);

} // namespace glasswyrm::drm
