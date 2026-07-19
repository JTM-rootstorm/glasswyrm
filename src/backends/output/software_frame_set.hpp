#pragma once

#include "backends/output/software_frame.hpp"
#include "output/model/types.hpp"
#include "output/vrr/reasons.hpp"
#include "output/vrr/types.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace glasswyrm::output {

// Desired variable-refresh state is presentation metadata. It deliberately
// lives beside, rather than inside, SoftwareFrame so renderer and pixel hashes
// remain independent of display cadence policy.
struct VrrPresentationRequest {
  bool valid{};
  vrr::PolicyMode requested_mode{vrr::PolicyMode::Off};
  vrr::Decision decision{vrr::Decision::Disabled};
  bool desired_enabled{};
  std::uint32_t candidate_window_id{};
  std::uint64_t candidate_surface_id{};
  vrr::ReasonMask reason_flags{};
  std::uint64_t state_generation{};
  std::uint64_t transition_serial{};
  std::uint64_t target_interval_nanoseconds{};
};

struct OutputFrameResult {
  OutputSpec output;
  LogicalRectangle logical;
  RationalScale scale;
  OutputTransform transform{OutputTransform::Normal};
  SoftwareFrame frame;
  std::vector<gw::compositor::Rectangle> damage;
  std::uint64_t visible_hash{};
  VrrPresentationRequest vrr;
};

[[nodiscard]] std::uint64_t calculate_frame_set_aggregate_hash(
    const std::map<std::uint64_t, OutputFrameResult>& outputs,
    std::uint64_t layout_generation,
    std::uint64_t primary_output_id) noexcept;

struct SoftwareFrameSetView {
  const std::map<std::uint64_t, OutputFrameResult> *outputs{};
  std::uint64_t aggregate_hash{};
  std::uint64_t layout_generation{};
  std::uint64_t primary_output_id{};
  std::uint64_t commit_id{};
  std::uint64_t generation{};
  std::uint64_t ordinal{};

  [[nodiscard]] bool valid() const noexcept {
    return outputs != nullptr && !outputs->empty() && layout_generation != 0 &&
           primary_output_id != 0 && commit_id != 0 && generation != 0 &&
           ordinal != 0;
  }
};

// Owns the complete canonical software result for one output-layout commit.
// Frames are keyed by stable output ID, so iteration and aggregate hashing are
// deterministic regardless of discovery or render order.
class SoftwareFrameSet {
public:
  static constexpr std::size_t kMaximumOutputs =
      glasswyrm::output::kMaximumOutputs;
  static constexpr std::uint64_t kMaximumTotalPixels =
      kMaximumTotalOutputPixels;

  [[nodiscard]] bool append(OutputFrameResult output, std::string &error);
  [[nodiscard]] bool finalize(std::uint64_t layout_generation,
                              std::uint64_t primary_output_id,
                              std::uint64_t commit_id,
                              std::uint64_t generation,
                              std::uint64_t ordinal, std::string &error);
  [[nodiscard]] bool set_vrr_requests(
      const std::map<std::uint64_t, VrrPresentationRequest>& requests,
      std::string& error);

  [[nodiscard]] const std::map<std::uint64_t, OutputFrameResult> &
  outputs() const noexcept {
    return outputs_;
  }
  [[nodiscard]] std::uint64_t aggregate_hash() const noexcept {
    return aggregate_hash_;
  }
  [[nodiscard]] std::uint64_t layout_generation() const noexcept {
    return layout_generation_;
  }
  [[nodiscard]] std::uint64_t primary_output_id() const noexcept {
    return primary_output_id_;
  }
  [[nodiscard]] std::uint64_t commit_id() const noexcept { return commit_id_; }
  [[nodiscard]] std::uint64_t generation() const noexcept {
    return generation_;
  }
  [[nodiscard]] std::uint64_t ordinal() const noexcept { return ordinal_; }
  [[nodiscard]] bool finalized() const noexcept { return finalized_; }
  [[nodiscard]] SoftwareFrameSetView view() const noexcept;

private:
  std::map<std::uint64_t, OutputFrameResult> outputs_;
  std::uint64_t total_pixels_{};
  std::uint64_t aggregate_hash_{};
  std::uint64_t layout_generation_{};
  std::uint64_t primary_output_id_{};
  std::uint64_t commit_id_{};
  std::uint64_t generation_{};
  std::uint64_t ordinal_{};
  bool finalized_{};
};

} // namespace glasswyrm::output
