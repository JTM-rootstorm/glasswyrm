#include "backends/drm/drm_report.hpp"
#include "tests/helpers/test_support.hpp"

#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <unistd.h>

namespace {

using glasswyrm::drm::DiscoveryReport;
using glasswyrm::drm::DrmReport;
using glasswyrm::drm::DrmReportRecord;
using glasswyrm::drm::FatalReport;
using glasswyrm::drm::FlipReport;
using glasswyrm::drm::ReportApiPath;
using glasswyrm::drm::SelectionReport;
using glasswyrm::drm::StagedDrmReport;

struct TemporaryDirectory {
  TemporaryDirectory() {
    char pattern[] = "/tmp/glasswyrm-drm-report-XXXXXX";
    gw::test::require(::mkdtemp(pattern) != nullptr,
                      "create DRM report test directory");
    path = pattern;
  }
  ~TemporaryDirectory() {
    std::error_code ignored;
    std::filesystem::remove_all(path, ignored);
  }
  std::filesystem::path path;
};

std::string read(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

void write(const std::filesystem::path& path, const std::string& contents) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output << contents;
  gw::test::require(static_cast<bool>(output), "write test file");
}

DiscoveryReport discovery() {
  return {"/dev/dri/card0", "virtio_gpu", true, true, true};
}

SelectionReport selection() {
  SelectionReport value;
  value.connector_name = "Virtual-1";
  value.connector_id = 31;
  value.crtc_id = 42;
  value.primary_plane_id = 7;
  value.mode_name = "1024x768";
  value.width = 1024;
  value.height = 768;
  value.refresh_millihz = 60000;
  value.api = ReportApiPath::Atomic;
  value.framebuffer_format = "XRGB8888";
  value.pitches = {4096, 4096};
  value.sizes = {3145728, 3145728};
  value.vt_path = "/dev/tty2";
  value.vt_owned = true;
  return value;
}

FlipReport flip() {
  return {1, 9, 11, 1, 56, 0x1234, 0x1234, 77,
          ReportApiPath::Atomic};
}

struct ReplacementRace {
  std::filesystem::path report;
  std::filesystem::path displaced;
};

void replace_report_during_commit(void* raw_context) {
  auto& context = *static_cast<ReplacementRace*>(raw_context);
  std::filesystem::rename(context.report, context.displaced);
  write(context.report, "raced replacement");
}

void stage_commit_abort() {
  TemporaryDirectory directory;
  const auto path = directory.path / "report.jsonl";
  DrmReport report(path);
  std::string error;
  gw::test::require(report.initialize(error), "initialize absent report path");

  const std::array<DrmReportRecord, 2> initialization{discovery(), selection()};
  StagedDrmReport staged;
  gw::test::require(report.stage(initialization, staged, error),
                    "stage initialization records");
  gw::test::require(staged.active() &&
                        std::filesystem::exists(staged.temporary_path()) &&
                        !std::filesystem::exists(path),
                    "staging exposes only private temporary report");
  const auto initial_contents = read(staged.temporary_path());
  gw::test::require(report.commit(staged, error) && report.generation() == 1 &&
                        read(path) == initial_contents,
                    "atomically publish initialization report");

  const auto frame = DrmReportRecord{flip()};
  gw::test::require(report.stage(frame, staged, error), "stage completed flip");
  gw::test::require(read(path) == initial_contents &&
                        read(path).find("\"record\":\"flip\"") ==
                            std::string::npos,
                    "final frame record invisible before completion commit");
  const auto temporary = staged.temporary_path();
  report.abort(staged);
  gw::test::require(!std::filesystem::exists(temporary) &&
                        read(path) == initial_contents,
                    "aborted flip removes staging and preserves report");

  gw::test::require(report.stage(frame, staged, error) &&
                        report.commit(staged, error) &&
                        read(path).find("\"record\":\"flip\"") !=
                            std::string::npos,
                    "publish flip only on explicit completion commit");

  std::filesystem::path abandoned;
  {
    StagedDrmReport automatic_abort;
    const DrmReportRecord fatal =
        FatalReport{"page_flip", "timeout", "Virtual-1", 42, 56, 9, 11};
    gw::test::require(report.stage(fatal, automatic_abort, error),
                      "stage fatal evidence");
    abandoned = automatic_abort.temporary_path();
  }
  gw::test::require(!std::filesystem::exists(abandoned),
                    "staged report destructor removes temporary file");
}

void unsafe_paths_are_rejected() {
  TemporaryDirectory directory;
  std::string error;

  const auto existing = directory.path / "existing.jsonl";
  write(existing, "occupied");
  DrmReport existing_report(existing);
  gw::test::require(!existing_report.initialize(error),
                    "reject pre-existing report target");

  const auto target = directory.path / "target";
  write(target, "target");
  const auto link = directory.path / "report-link.jsonl";
  gw::test::require(::symlink(target.c_str(), link.c_str()) == 0,
                    "create report symlink");
  DrmReport linked_report(link);
  gw::test::require(!linked_report.initialize(error), "reject report symlink");

  const auto real_parent = directory.path / "real";
  std::filesystem::create_directory(real_parent);
  const auto parent_link = directory.path / "parent-link";
  gw::test::require(::symlink(real_parent.c_str(), parent_link.c_str()) == 0,
                    "create parent symlink");
  DrmReport parent_linked(parent_link / "report.jsonl");
  gw::test::require(!parent_linked.initialize(error),
                    "reject symlinked immediate parent");
}

void replacement_targets_are_not_overwritten() {
  TemporaryDirectory directory;
  const auto path = directory.path / "report.jsonl";
  const auto original = directory.path / "original.jsonl";
  DrmReport report(path);
  std::string error;
  StagedDrmReport staged;
  gw::test::require(report.initialize(error) &&
                        report.stage(DrmReportRecord{discovery()}, staged,
                                     error) &&
                        report.commit(staged, error),
                    "publish report before replacement test");
  gw::test::require(report.stage(DrmReportRecord{flip()}, staged, error),
                    "stage report update before replacement");
  const auto temporary = staged.temporary_path();
  std::filesystem::rename(path, original);
  write(path, "replacement");
  gw::test::require(!report.commit(staged, error) && read(path) == "replacement" &&
                        !std::filesystem::exists(temporary),
                    "reject and preserve replacement target");

  const auto raced_path = directory.path / "raced.jsonl";
  DrmReport raced(raced_path);
  gw::test::require(raced.initialize(error) &&
                        raced.stage(DrmReportRecord{discovery()}, staged,
                                    error),
                    "stage first report before target race");
  write(raced_path, "appeared");
  gw::test::require(!raced.commit(staged, error) &&
                        read(raced_path) == "appeared",
                    "first publication never replaces appeared target");

  const auto exchange_path = directory.path / "exchange.jsonl";
  const auto displaced_path = directory.path / "exchange-original.jsonl";
  DrmReport exchange_report(exchange_path);
  gw::test::require(exchange_report.initialize(error) &&
                        exchange_report.stage(DrmReportRecord{discovery()},
                                              staged, error) &&
                        exchange_report.commit(staged, error) &&
                        exchange_report.stage(DrmReportRecord{flip()}, staged,
                                              error),
                    "prepare deterministic exchange race");
  ReplacementRace race{exchange_path, displaced_path};
  exchange_report.set_before_publish_hook_for_testing(
      replace_report_during_commit, &race);
  gw::test::require(!exchange_report.commit(staged, error) &&
                        read(exchange_path) == "raced replacement" &&
                        std::filesystem::exists(displaced_path),
                    "exchange rollback preserves raced replacement target");
}

void invalid_records_are_rejected() {
  TemporaryDirectory directory;
  DrmReport report(directory.path / "report.jsonl");
  std::string error;
  StagedDrmReport staged;
  gw::test::require(report.initialize(error), "initialize validation report");
  auto mismatched = flip();
  mismatched.scanout_hash ^= 1;
  gw::test::require(!report.stage(DrmReportRecord{mismatched}, staged, error) &&
                        !std::filesystem::exists(report.path()),
                    "reject canonical/scanout hash mismatch");
  auto one_buffer = selection();
  one_buffer.pitches.pop_back();
  one_buffer.sizes.pop_back();
  gw::test::require(
      !report.stage(DrmReportRecord{one_buffer}, staged, error),
      "M10 selection diagnostics require exactly two dumb buffers");
  auto implicit_primary = selection();
  implicit_primary.api = ReportApiPath::Legacy;
  implicit_primary.primary_plane_id = 0;
  gw::test::require(
      report.stage(DrmReportRecord{implicit_primary}, staged, error),
      "legacy diagnostics may represent the implicit primary plane");
  report.abort(staged);
}

} // namespace

int main() {
  stage_commit_abort();
  unsafe_paths_are_rejected();
  replacement_targets_are_not_overwritten();
  invalid_records_are_rejected();
  return 0;
}
