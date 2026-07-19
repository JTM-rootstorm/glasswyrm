#pragma once

#include "output/vrr/reasons.hpp"
#include "output/vrr/types.hpp"

#include <cstdint>
#include <optional>

namespace glasswyrm::output::vrr {

struct OutputFacts {
  bool enabled{};
  bool connected{};
  bool drm{};
  bool hardware_capable{};
  bool atomic_kms_available{};
  bool vrr_property_present{};
  bool atomic_test_passed{};
  bool kms_controllable{};
  bool simulated{};
};

struct DisplayFacts {
  bool session_active{};
  bool vt_suspended{};
  bool output_configuration_busy{};
};

struct PresenterFacts {
  bool accepted{};
  bool property_readback_matches{};
  bool timing_available{};
  bool hardware_behavior_confirmed{};
};

struct CandidateFacts {
  bool selected{};
  std::uint32_t window_id{};
  std::uint64_t surface_id{};
  bool window_present{};
  bool visible{};
  bool managed{};
  bool focused{};
  bool fullscreen{};
  bool borderless_fullscreen{};
  bool exclusive_output_membership{};
  WindowPreference preference{WindowPreference::Default};
  bool surface_present{};
  bool surface_metadata_only{};
  bool surface_visible{};
  bool surface_opaque{};
  bool surface_on_output{};
  bool surface_membership_valid{};
};

struct DecisionInput {
  PolicyMode mode{PolicyMode::Off};
  OutputFacts output;
  DisplayFacts display;
  PresenterFacts presenter;
  CandidateFacts candidate;
};

struct DecisionResult {
  bool desired_enabled{};
  Decision decision{Decision::Disabled};
  bool candidate_required{};
  std::uint32_t candidate_window_id{};
  std::uint64_t candidate_surface_id{};
  ReasonMask reasons{};
  std::optional<Reason> primary;
};

[[nodiscard]] DecisionResult evaluate(const DecisionInput &input) noexcept;

} // namespace glasswyrm::output::vrr
