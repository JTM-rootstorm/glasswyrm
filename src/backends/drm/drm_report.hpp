#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace glasswyrm::drm {

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
};

struct VtReport {
  VtTransition transition{VtTransition::Release};
  bool master_owned{};
  bool full_modeset{};
  std::uint64_t committed_hash{};
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
                 VtReport, RestoreReport, FatalReport>;

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
  std::string committed_contents_;
  std::uint64_t generation_{};
  bool initialized_{};
  BeforePublishHook before_publish_hook_{};
  void* before_publish_context_{};
};

} // namespace glasswyrm::drm
