#pragma once

#include "backends/drm/damage_copy.hpp"
#include "backends/drm/drm_vrr_report.hpp"
#include "backends/drm/vrr_timing.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace glasswyrm::drm {

struct DrmPresentationEvidenceId {
  std::uint64_t output_id{};
  std::uint64_t commit_id{};
  std::uint64_t generation{};
  std::uint64_t presentation_token{};
};

enum class ReportApiPath { Atomic, Legacy };
enum class VtTransition { Release, Acquire };

struct DiscoveryReport {
  std::string device_path;
  std::string driver_name;
  bool primary_node{};
  bool dumb_buffer_capable{};
  bool atomic_capable{};
};

struct SelectionReport {
  std::string connector_name;
  std::uint32_t connector_id{};
  std::uint32_t crtc_id{};
  std::uint32_t primary_plane_id{};
  std::string mode_name;
  std::uint32_t width{};
  std::uint32_t height{};
  std::uint32_t refresh_millihz{};
  ReportApiPath api{ReportApiPath::Atomic};
  std::string framebuffer_format;
  std::vector<std::uint32_t> pitches;
  std::vector<std::uint64_t> sizes;
  std::string vt_path;
  bool vt_owned{};
};

struct ModesetReport {
  std::uint64_t ordinal{};
  std::uint64_t commit_id{};
  std::uint64_t generation{};
  std::uint32_t front_buffer_index{};
  std::uint32_t framebuffer_id{};
  std::uint64_t canonical_hash{};
  std::uint64_t scanout_hash{};
  ReportApiPath api{ReportApiPath::Atomic};
};

struct FlipReport {
  std::uint64_t ordinal{};
  std::uint64_t commit_id{};
  std::uint64_t generation{};
  std::uint32_t front_buffer_index{};
  std::uint32_t framebuffer_id{};
  std::uint64_t canonical_hash{};
  std::uint64_t scanout_hash{};
  std::uint64_t page_flip_sequence{};
  ReportApiPath api{ReportApiPath::Atomic};
  CrtcSequenceSample crtc_sequence_sample;
};

struct VtReport {
  VtTransition transition{VtTransition::Release};
  bool master_owned{};
  bool full_modeset{};
  std::uint64_t committed_hash{};
};

struct DamageCopyReport {
  std::uint64_t generation{};
  std::uint32_t buffer_index{};
  std::uint32_t framebuffer_id{};
  std::uint64_t full_frame_bytes{};
  std::uint64_t copied_bytes{};
  std::uint64_t history_span{};
  std::uint64_t drm_copied_bytes{};
  std::uint64_t copy_nanoseconds{};
  std::uint64_t parity_verified_bytes{};
  std::uint64_t scanout_readback_bytes{};
  std::uint64_t parity_nanoseconds{};
  std::uint64_t cumulative_full_frame_bytes{};
  std::uint64_t cumulative_copied_bytes{};
  std::vector<gw::compositor::Rectangle> rectangles;
  FullCopyReason full_copy_reason{FullCopyReason::None};
};

inline constexpr std::uint32_t kEvidenceStreamDrmReport = UINT32_C(1) << 0U;
inline constexpr std::uint32_t kEvidenceStreamVrrReport = UINT32_C(1) << 1U;
inline constexpr std::uint32_t kEvidenceStreamMirror = UINT32_C(1) << 2U;
inline constexpr std::uint32_t kKnownEvidenceStreamMask =
    kEvidenceStreamDrmReport | kEvidenceStreamVrrReport |
    kEvidenceStreamMirror;

struct EvidenceStreamReport {
  DrmPresentationEvidenceId evidence;
  std::uint32_t stream{};
};

struct EvidenceSealReport {
  DrmPresentationEvidenceId evidence;
  std::uint32_t required_streams{};
  std::uint32_t committed_streams{};
  std::uint64_t mirror_frame{};
  std::uint64_t mirror_fnv1a64{};
  std::string mirror_file;
};

struct RestoreReport {
  bool kms_restore{};
  bool vt_restore{};
  bool master_drop{};
  bool framebuffer_cleanup{};
};

struct FatalReport {
  std::string stage;
  std::string reason;
  std::string connector_name;
  std::uint32_t crtc_id{};
  std::uint32_t framebuffer_id{};
  std::uint64_t commit_id{};
  std::uint64_t generation{};
};

using DrmReportRecord =
    std::variant<DiscoveryReport, SelectionReport, ModesetReport, FlipReport,
                 VtReport, DamageCopyReport, EvidenceStreamReport,
                 EvidenceSealReport, RestoreReport, FatalReport,
                 DrmVrrReportRecord>;

[[nodiscard]] std::string serialize_report_record(
    const DrmReportRecord& record);

class DrmReport;

class StagedDrmReport final {
public:
  StagedDrmReport() = default;
  ~StagedDrmReport();
  StagedDrmReport(const StagedDrmReport&) = delete;
  StagedDrmReport& operator=(const StagedDrmReport&) = delete;
  StagedDrmReport(StagedDrmReport&& other) noexcept;
  StagedDrmReport& operator=(StagedDrmReport&& other) noexcept;

  [[nodiscard]] bool active() const noexcept { return active_; }
  [[nodiscard]] const std::filesystem::path& temporary_path() const noexcept {
    return temporary_path_;
  }
  [[nodiscard]] const std::filesystem::path& final_path() const noexcept {
    return final_path_;
  }

private:
  friend class DrmReport;
  void discard() noexcept;

  std::filesystem::path temporary_path_;
  std::filesystem::path final_path_;
  std::string contents_;
  std::uint64_t base_generation_{};
  bool active_{};
};

class DrmReport final {
public:
  using BeforePublishHook = void (*)(void* context);

  explicit DrmReport(std::filesystem::path path) : path_(std::move(path)) {}

  [[nodiscard]] bool initialize(std::string& error);
  [[nodiscard]] bool stage(const DrmReportRecord& record,
                           StagedDrmReport& staged, std::string& error);
  [[nodiscard]] bool stage(std::span<const DrmReportRecord> records,
                           StagedDrmReport& staged, std::string& error);
  [[nodiscard]] bool commit(StagedDrmReport& staged, std::string& error);
  [[nodiscard]] bool flush(std::string& error);
  void abort(StagedDrmReport& staged) const noexcept;
  void set_before_publish_hook_for_testing(BeforePublishHook hook,
                                           void* context) noexcept {
    before_publish_hook_ = hook;
    before_publish_context_ = context;
  }

  [[nodiscard]] const std::filesystem::path& path() const noexcept {
    return path_;
  }
  [[nodiscard]] std::uint64_t generation() const noexcept {
    return generation_;
  }

private:
  struct Identity {
    std::uint64_t device{};
    std::uint64_t inode{};
  };

  [[nodiscard]] bool validate_parent(std::string& error) const;
  [[nodiscard]] bool validate_target(std::string& error) const;
  [[nodiscard]] bool capture_target_identity(std::string& error);

  std::filesystem::path path_;
  std::filesystem::path parent_;
  Identity parent_identity_{};
  Identity target_identity_{};
  std::uint64_t committed_size_{};
  std::uint64_t generation_{};
  bool initialized_{};
  bool dirty_{};
  bool poisoned_{};
  BeforePublishHook before_publish_hook_{};
  void* before_publish_context_{};
};

} // namespace glasswyrm::drm
