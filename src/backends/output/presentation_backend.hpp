#pragma once

#include "backends/output/software_frame.hpp"
#include "backends/output/software_frame_set.hpp"
#include "output/vrr/reasons.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace glasswyrm::output {

struct VrrPresentationCapability {
  bool output_enabled{};
  bool connected{};
  bool drm{};
  bool connector_property_present{};
  bool hardware_capable{};
  bool atomic_kms_available{};
  bool atomic_test_passed{};
  bool kms_controllable{};
  bool simulated{};
  bool range_available{};
  bool atomic_required{};
  bool session_active{true};
  bool suspended{};
  bool timing_available{};
  std::uint32_t minimum_refresh_millihertz{};
  std::uint32_t maximum_refresh_millihertz{};
  vrr::ReasonMask reason_flags{};
};

struct VrrPresentationFeedback {
  std::uint64_t output_id{};
  bool effective_enabled{};
  bool property_readback_valid{};
  bool session_active{};
  std::uint32_t flip_sequence{};
  std::uint32_t flags{};
  std::uint64_t kernel_timestamp_nanoseconds{};
  std::uint64_t interval_nanoseconds{};
  bool timestamp_available{};
};

inline constexpr std::uint32_t kVrrPresentationFeedbackSimulated =
    UINT32_C(1) << 0U;

using VrrPresentationFeedbackMap =
    std::map<std::uint64_t, VrrPresentationFeedback>;

enum class PresentDisposition { Complete, Pending, Rejected, Fatal };

struct PresentResult {
  PresentDisposition disposition{PresentDisposition::Rejected};
  std::uint64_t token{};
  std::uint64_t visible_hash{};
  std::string error;
  VrrPresentationFeedbackMap vrr_feedback;

  PresentResult() = default;
  PresentResult(PresentDisposition disposition_value, std::uint64_t token_value,
                std::uint64_t visible_hash_value, std::string error_value,
                VrrPresentationFeedbackMap feedback = {})
      : disposition(disposition_value), token(token_value),
        visible_hash(visible_hash_value), error(std::move(error_value)),
        vrr_feedback(std::move(feedback)) {}
};

enum class BackendEventKind { None, Complete, Fatal };

struct BackendEvent {
  BackendEventKind kind{BackendEventKind::None};
  std::uint64_t token{};
  std::uint64_t visible_hash{};
  std::string error;
  VrrPresentationFeedbackMap vrr_feedback;

  BackendEvent() = default;
  BackendEvent(BackendEventKind kind_value, std::uint64_t token_value,
               std::uint64_t visible_hash_value, std::string error_value,
               VrrPresentationFeedbackMap feedback = {})
      : kind(kind_value), token(token_value),
        visible_hash(visible_hash_value), error(std::move(error_value)),
        vrr_feedback(std::move(feedback)) {}
};

enum class BackendStateResult { Complete, Fatal };

class PresentationBackend {
 public:
  virtual ~PresentationBackend() = default;

  // Optional presentation contracts are activated only after peer
  // negotiation. Historical peers must not cause optional KMS discovery.
  [[nodiscard]] virtual bool configure_vrr_contract(bool enabled,
                                                    std::string& error) {
    static_cast<void>(enabled);
    error.clear();
    return true;
  }

  // A resumed backend remains session-inactive until the coordinating peer
  // acknowledges the Active transition.
  [[nodiscard]] virtual bool activate_session(std::string& error) {
    error.clear();
    return true;
  }

  [[nodiscard]] virtual std::optional<VrrPresentationCapability>
  vrr_capability(std::uint64_t output_id) const noexcept {
    static_cast<void>(output_id);
    return std::nullopt;
  }

  [[nodiscard]] virtual PresentResult present(const SoftwareFrameView& frame) = 0;
  // Internal owners may submit the finalized frame set directly. Its stored
  // per-output and aggregate hashes are trusted because SoftwareFrameSet has
  // no public pixel-mutation path after finalize(). External/view callers
  // retain the validating entry point below.
  [[nodiscard]] virtual PresentResult present(const SoftwareFrameSet& frames) {
    if (!frames.finalized())
      return {PresentDisposition::Rejected, 0, 0,
              "presentation requires a finalized output frame set"};
    return present(frames.view());
  }
  // The view boundary remains available for untrusted adapters and tests.
  // Backends must validate any hashes and metadata supplied through it.
  [[nodiscard]] virtual PresentResult present(
      const SoftwareFrameSetView&) {
    return {PresentDisposition::Rejected, 0, 0,
            "presentation backend does not support output frame sets"};
  }
  [[nodiscard]] virtual int poll_fd() const noexcept = 0;
  [[nodiscard]] virtual short poll_events() const noexcept = 0;
  // Pending diagnostics remain staged through service(); the coordinator calls
  // finalize_pending() only after validating the completion token and hash.
  [[nodiscard]] virtual BackendEvent service(short revents) = 0;
  [[nodiscard]] virtual bool finalize_pending(std::uint64_t token,
                                              std::string& error) {
    static_cast<void>(token);
    error.clear();
    return true;
  }
  virtual void abort_pending(std::uint64_t token,
                             std::string_view reason = {}) noexcept {
    static_cast<void>(token);
    static_cast<void>(reason);
  }
  [[nodiscard]] virtual BackendStateResult suspend(std::string& error) = 0;
  [[nodiscard]] virtual PresentResult resume(
      const SoftwareFrameView& committed) = 0;
  [[nodiscard]] virtual PresentResult resume(
      const SoftwareFrameSetView&) {
    return {PresentDisposition::Rejected, 0, 0,
            "presentation backend cannot resume output frame sets"};
  }
  [[nodiscard]] virtual BackendStateResult shutdown(
      std::string& error) noexcept = 0;
};

}  // namespace glasswyrm::output
