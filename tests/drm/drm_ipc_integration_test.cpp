#include "tests/drm/drm_ipc_test_fixture.hpp"

#include "tests/helpers/test_support.hpp"

#include <poll.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace {

using glasswyrm::compositor::ContractDispatchResult;
using gw::compositor::PresentationCompletionKind;
using gw::test::drm_ipc::IpcHarness;
using gw::test::drm_ipc::PresenterHarness;
using gw::test::drm_ipc::ProducerKind;

ContractDispatchResult start_initial(PresenterHarness& rig, IpcHarness& ipc,
                                     const std::uint64_t buffer_id = 11) {
  ipc.send_snapshot(buffer_id, 0xff102030U, 1);
  const auto result = ipc.dispatch_until_frame(*rig.compositor);
  const auto events = ipc.drain_client();
  gw::test::require(result.accepted_frame && !result.pending_frame &&
                        gw::test::drm_ipc::has_ack(events, 1) &&
                        rig.compositor->accepted_frames() == 1 &&
                        rig.mirror_frame(1).filename().string().starts_with(
                            "frame-000001") &&
                        std::filesystem::exists(rig.mirror_frame(1)),
                    "first producer frame completes by blocking modeset");
  return result;
}

ContractDispatchResult start_pending(PresenterHarness& rig, IpcHarness& ipc,
                                     const std::uint64_t old_buffer = 11,
                                     const std::uint64_t new_buffer = 12) {
  start_initial(rig, ipc, old_buffer);
  ipc.send_replacement(new_buffer, 0xff405060U, 2);
  const auto result = ipc.dispatch_until_frame(*rig.compositor);
  gw::test::require(result.pending_frame && !result.accepted_frame &&
                        result.reply_sequence != 0 &&
                        rig.compositor->presentation_pending() &&
                        rig.compositor->accepted_frames() == 1 &&
                        rig.compositor->releases().empty() &&
                        ipc.drain_client().empty() &&
                        !std::filesystem::exists(rig.mirror_frame(2)) &&
                        rig.report_contents().find("\"ordinal\":2") ==
                            std::string::npos,
                    "second producer frame delays acknowledgement release and evidence");
  return result;
}

gw::compositor::PresentationCompletion complete_flip(
    PresenterHarness& rig, const std::uint32_t sequence) {
  rig.drm.queue_page_flip(0, 40, sequence);
  std::string error;
  const auto completion = rig.compositor->service_presentation(
      gw::test::drm_ipc::ready_presentation_events(*rig.compositor), error);
  gw::test::require(completion.kind == PresentationCompletionKind::Complete &&
                        error.empty(),
                    "matching fake DRM page flip completes");
  return completion;
}

void publish_completion(PresenterHarness& rig, IpcHarness& ipc,
                        const ContractDispatchResult& pending,
                        const std::uint32_t sequence) {
  const auto completion = complete_flip(rig, sequence);
  const auto result = glasswyrm::compositor::complete_contract_presentation(
      ipc.server(), pending.reply_sequence, completion, std::nullopt,
      *rig.compositor);
  gw::test::require(result.accepted_frame,
                    "completed presentation publishes producer result");
}

void require_clean_restore(PresenterHarness& rig) {
  std::string error;
  gw::test::require(rig.shutdown(error), "fake DRM session restores cleanly");
  const auto report = rig.report_contents();
  gw::test::require(
      report.find("\"record\":\"restore\"") != std::string::npos &&
          report.find("\"kms\":true") != std::string::npos &&
          report.find("\"framebuffer_cleanup\":true") != std::string::npos &&
          std::ranges::find(rig.kms.calls, "drop_master") !=
              rig.kms.calls.end() &&
          std::ranges::find(rig.vt.calls, "restore-kd") != rig.vt.calls.end(),
      "shutdown report and fake APIs prove KMS VT and scanout restoration");
}

void m4_async_ipc_ordering_and_vt() {
  PresenterHarness rig;
  IpcHarness ipc(rig.root / "m4.sock", ProducerKind::M4);
  const auto pending = start_pending(rig, ipc);
  const auto front = rig.presenter->front_framebuffer();

  ipc.send_damage_commit(3);
  ipc.pump_transport();
  gw::test::require(ipc.drain_client().empty() &&
                        rig.presenter->front_framebuffer() == front,
                    "later producer frame remains queued behind pending scanout");

  publish_completion(rig, ipc, pending, 0);
  const auto completed = ipc.drain_client();
  const auto acknowledgement = std::ranges::find_if(
      completed, [](const auto& event) {
        return event.type == GWIPC_MESSAGE_FRAME_ACKNOWLEDGED &&
               event.commit_id == 2;
      });
  const auto release = std::ranges::find_if(completed, [](const auto& event) {
    return event.type == GWIPC_MESSAGE_BUFFER_RELEASE && event.buffer_id == 11;
  });
  gw::test::require(
      acknowledgement != completed.end() && release != completed.end() &&
          acknowledgement < release && rig.presenter->front_framebuffer() != front &&
          rig.compositor->accepted_frames() == 2 &&
          std::filesystem::exists(rig.mirror_frame(2)) &&
          rig.report_contents().find("\"ordinal\":2") != std::string::npos,
      "completion orders acknowledgement before retired-buffer release and evidence");

  const auto queued = ipc.dispatch_until_frame(*rig.compositor);
  gw::test::require(queued.pending_frame,
                    "queued third producer frame dispatches only after completion");
  publish_completion(rig, ipc, queued, 0);
  constexpr std::string_view zero_sequence = "\"page_flip_sequence\":0";
  const auto report = rig.report_contents();
  const auto first_zero = report.find(zero_sequence);
  const auto second_zero =
      first_zero == std::string::npos
          ? std::string::npos
          : report.find(zero_sequence, first_zero + zero_sequence.size());
  gw::test::require(gw::test::drm_ipc::has_ack(ipc.drain_client(), 3) &&
                        rig.compositor->accepted_frames() == 3 &&
                        first_zero != std::string::npos &&
                        second_zero != std::string::npos &&
                        report.find(zero_sequence,
                                    second_zero + zero_sequence.size()) ==
                            std::string::npos,
                    "consecutive zero-sequence flips rearm and acknowledge the "
                    "queued producer frame");

  std::string error;
  gw::test::require(rig.compositor->suspend_presentation(error) &&
                        rig.compositor->presentation_suspended() &&
                        rig.compositor->resume_presentation(error) &&
                        !rig.compositor->presentation_suspended() &&
                        rig.report_contents().find("\"transition\":\"release\"") !=
                            std::string::npos &&
                        rig.report_contents().find("\"transition\":\"acquire\"") !=
                            std::string::npos,
                    "VT release and acquire re-present committed M4 pixels");
  gw::test::require(rig.compositor->disconnect(error), error);
  require_clean_restore(rig);
}

void protocol_server_buffered_scene() {
  PresenterHarness rig({}, true);
  IpcHarness ipc(rig.root / "protocol.sock", ProducerKind::ProtocolServer);
  const auto pending = start_pending(rig, ipc, 21, 22);
  const auto pending_manifest = rig.scene_manifest_contents();
  gw::test::require(
      std::ranges::count(pending_manifest, '\n') == 1 &&
          pending_manifest.find("\"commit_id\":1") != std::string::npos &&
          pending_manifest.find("\"commit_id\":2") == std::string::npos,
      "pending ProtocolServer flip does not publish its scene manifest early");
  publish_completion(rig, ipc, pending, 91);
  const auto events = ipc.drain_client();
  const auto completed_manifest = rig.scene_manifest_contents();
  gw::test::require(gw::test::drm_ipc::has_ack(events, 2) &&
                        gw::test::drm_ipc::has_release(events, 21) &&
                        std::filesystem::exists(rig.mirror_frame(2)) &&
                        std::ranges::count(completed_manifest, '\n') == 2 &&
                        completed_manifest.find("\"commit_id\":2") !=
                            std::string::npos,
                    "buffered ProtocolServer scene and manifest use the asynchronous DRM path");
  std::string error;
  gw::test::require(
      rig.compositor->service_presentation(0, error).kind ==
              PresentationCompletionKind::None &&
          rig.scene_manifest_contents() == completed_manifest,
      "completed ProtocolServer flip appends its scene manifest exactly once");
  require_clean_restore(rig);
}

void protocol_server_cursor_scanout() {
  PresenterHarness rig({}, true);
  IpcHarness ipc(rig.root / "cursor.sock", ProducerKind::ProtocolServer);
  ipc.send_cursor_snapshot(31, UINT32_C(0xff0000ff), 32,
                           UINT32_C(0xffff0000), 1);
  const auto result = ipc.dispatch_until_frame(*rig.compositor);
  const auto events = ipc.drain_client();
  const auto frame_path = rig.mirror_frame(1);
  std::ifstream frame_stream(frame_path, std::ios::binary);
  const std::string frame{std::istreambuf_iterator<char>(frame_stream), {}};
  constexpr std::string_view header = "P6\n2 2\n255\n";
  const auto manifest = rig.scene_manifest_contents();
  gw::test::require(
      result.accepted_frame && gw::test::drm_ipc::has_ack(events, 1) &&
          frame.starts_with(header) && frame.size() >= header.size() + 3 &&
          static_cast<std::uint8_t>(frame[header.size()]) == 0xff &&
          static_cast<std::uint8_t>(frame[header.size() + 1]) == 0 &&
          static_cast<std::uint8_t>(frame[header.size() + 2]) == 0 &&
          manifest.find("\"surface_count\":1") != std::string::npos &&
          manifest.find("\"cursor_surface\":{") != std::string::npos,
      "software cursor is present in the canonical frame scanned out by fake DRM");
  require_clean_restore(rig);
}

void timeout_hup_and_disconnect_restore() {
  auto now = gw::compositor::PresentationTiming::Clock::time_point{};
  gw::compositor::PresentationTiming timing;
  timing.timeout = std::chrono::milliseconds(2000);
  timing.now = [&now] { return now; };
  PresenterHarness timeout_rig(std::move(timing));
  IpcHarness timeout_ipc(timeout_rig.root / "timeout.sock", ProducerKind::M4);
  start_pending(timeout_rig, timeout_ipc);
  timeout_ipc.send_damage_commit(3);
  timeout_ipc.pump_transport();
  now += std::chrono::milliseconds(2000);
  std::string error;
  gw::test::require(
      timeout_rig.compositor->service_presentation(0, error).kind ==
              PresentationCompletionKind::Fatal &&
          error.find("timeout") != std::string::npos &&
          timeout_ipc.drain_client().empty() &&
          timeout_rig.compositor->accepted_frames() == 1 &&
          !std::filesystem::exists(timeout_rig.mirror_frame(2)) &&
          !std::filesystem::exists(timeout_rig.mirror_frame(3)) &&
          timeout_rig.report_contents().find("completion timeout") !=
              std::string::npos,
      "page-flip timeout is fatal without acknowledging the pending or queued "
      "producer frame");
  require_clean_restore(timeout_rig);

  PresenterHarness hup_rig;
  IpcHarness hup_ipc(hup_rig.root / "hup.sock", ProducerKind::M4);
  start_pending(hup_rig, hup_ipc);
  gw::test::require(
      hup_rig.compositor->service_presentation(POLLHUP, error).kind ==
              PresentationCompletionKind::Fatal &&
          hup_ipc.drain_client().empty() &&
          hup_rig.report_contents().find("became unusable") !=
              std::string::npos,
      "DRM HUP is fatal and aborts the staged producer transaction");
  require_clean_restore(hup_rig);

  PresenterHarness disconnect_rig;
  IpcHarness disconnect_ipc(disconnect_rig.root / "disconnect.sock",
                            ProducerKind::M4);
  start_pending(disconnect_rig, disconnect_ipc);
  disconnect_ipc.disconnect_client();
  gw::test::require(disconnect_ipc.server_closed() &&
                        disconnect_rig.compositor->presentation_pending(),
                    "producer disconnect is detected while scanout is pending");
  gw::test::require(disconnect_rig.compositor->disconnect(error), error);
  gw::test::require(!std::filesystem::exists(disconnect_rig.mirror_frame(2)),
                    "producer disconnect aborts staged mirror evidence");
  require_clean_restore(disconnect_rig);
}

void replace_report_target(void* context) {
  const auto& path = *static_cast<std::filesystem::path*>(context);
  const auto replacement = path.string() + ".foreign";
  std::ofstream(replacement) << "foreign\n";
  std::filesystem::rename(replacement, path);
}

void diagnostic_failures_do_not_acknowledge() {
  PresenterHarness staging_rig;
  IpcHarness staging_ipc(staging_rig.root / "staging.sock", ProducerKind::M4);
  start_initial(staging_rig, staging_ipc);
  std::filesystem::remove_all(staging_rig.root / "mirror");
  std::ofstream(staging_rig.root / "mirror") << "obstacle\n";
  staging_ipc.send_replacement(12, 0xffa0b0c0U, 2);
  const auto rejected =
      staging_ipc.dispatch_until_frame(*staging_rig.compositor);
  const auto rejected_events = staging_ipc.drain_client();
  gw::test::require(
      !rejected.pending_frame && !rejected.accepted_frame &&
          gw::test::drm_ipc::has_ack(
              rejected_events, 2,
              GWIPC_FRAME_REJECTED_INCOMPLETE_METADATA) &&
          staging_rig.compositor->accepted_frames() == 1 &&
          staging_rig.kms.atomic_commits.size() == 2,
      "pre-submit mirror staging failure rejects without a KMS flip");
  std::filesystem::remove(staging_rig.root / "mirror");
  std::filesystem::create_directory(staging_rig.root / "mirror");
  require_clean_restore(staging_rig);

  PresenterHarness publish_rig;
  IpcHarness publish_ipc(publish_rig.root / "publish.sock", ProducerKind::M4);
  const auto pending = start_pending(publish_rig, publish_ipc);
  publish_rig.drm.queue_page_flip(0, 40, 101);
  auto report_path = publish_rig.report.path();
  publish_rig.report.set_before_publish_hook_for_testing(replace_report_target,
                                                         &report_path);
  std::string error;
  const auto completion = publish_rig.compositor->service_presentation(
      gw::test::drm_ipc::ready_presentation_events(*publish_rig.compositor),
      error);
  gw::test::require(
      completion.kind == PresentationCompletionKind::Fatal &&
          publish_ipc.drain_client().empty() &&
          !std::filesystem::exists(publish_rig.mirror_frame(2)) &&
          publish_rig.compositor->accepted_frames() == 1 &&
          pending.reply_sequence != 0,
      "report publication failure leaves mirror scene and acknowledgement uncommitted");
  (void)publish_rig.shutdown(error);
  gw::test::require(
      std::ranges::find_if(publish_rig.kms.calls, [](const std::string& call) {
        return call.starts_with("rmfb:");
      }) != publish_rig.kms.calls.end() &&
          std::ranges::find(publish_rig.kms.calls, "drop_master") !=
              publish_rig.kms.calls.end(),
      "diagnostic publication failure still restores and releases DRM state");
}

}  // namespace

int main() {
  m4_async_ipc_ordering_and_vt();
  protocol_server_buffered_scene();
  protocol_server_cursor_scanout();
  timeout_hup_and_disconnect_restore();
  diagnostic_failures_do_not_acknowledge();
  return 0;
}
