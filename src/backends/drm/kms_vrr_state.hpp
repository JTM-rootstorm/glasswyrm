#pragma once

#include "backends/drm/kms_state.hpp"
#include "backends/drm/resources.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace glasswyrm::drm {

enum class KmsVrrStatus {
  ConnectorPropertyAbsent,
  ConnectorNotCapable,
  AtomicUnavailable,
  CrtcPropertyAbsent,
  CrtcPropertyInvalid,
  TestOffRejected,
  TestOnRejected,
  Controllable,
};

struct KmsVrrState {
  KmsVrrStatus status{KmsVrrStatus::ConnectorPropertyAbsent};
  bool connector_property_present{};
  bool hardware_capable{};
  bool atomic_available{};
  bool crtc_property_present{};
  std::uint32_t crtc_id{};
  std::uint32_t crtc_property_id{};
  bool original_enabled{};
  bool test_off_passed{};
  bool test_on_passed{};
  bool controllable{};
  std::string diagnostic;
};

[[nodiscard]] KmsVrrState probe_kms_vrr_state(
    KmsApi &api, int fd, const Connector &connector, PipelineIds pipeline,
    const SavedKmsState &saved,
    std::span<const AtomicPropertyValue> selected_state);

[[nodiscard]] std::vector<AtomicPropertyValue> make_vrr_atomic_request(
    std::span<const AtomicPropertyValue> selected_state,
    const KmsVrrState &state, bool enabled);

[[nodiscard]] bool read_kms_vrr_enabled(KmsApi &api, int fd,
                                        PipelineIds pipeline,
                                        const KmsVrrState &state,
                                        bool &enabled, std::string &error);

[[nodiscard]] bool verify_kms_vrr_enabled(KmsApi &api, int fd,
                                          PipelineIds pipeline,
                                          const KmsVrrState &state,
                                          bool expected,
                                          std::string &error);

[[nodiscard]] const char *kms_vrr_status_name(KmsVrrStatus status) noexcept;

} // namespace glasswyrm::drm
