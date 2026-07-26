#pragma once

#include "backends/drm/presenter.hpp"

namespace glasswyrm::drm {

struct DrmPresenter::PendingPresentation {
  std::uint64_t token{};
  std::uint64_t hash{};
  std::uint64_t ordinal{};
  std::uint64_t commit_id{};
  std::uint64_t generation{};
  std::uint32_t framebuffer_id{};
  std::size_t next_front_index{};
  std::shared_ptr<PageFlipCookie> cookie;
  headless::StagedFrameDump mirror;
  StagedDrmReport report;
  StagedDrmReport vrr_report;
  std::vector<gw::compositor::Rectangle> committed_pixel_rectangles;
  std::vector<std::uint32_t> committed_pixel_update;
  DamageCopyPlan damage_copy;
  std::optional<output::VrrPresentationRequest> vrr_request;
  PresenterVrrPlan vrr_plan;
  DrmPresentationEvidenceId evidence;
  std::optional<PresenterVrrState> completed_vrr_state;
  output::VrrPresentationFeedbackMap vrr_feedback;
  bool promote_back{true};
  bool report_damage_copy{true};
  bool completion_verified{};
};

}  // namespace glasswyrm::drm
