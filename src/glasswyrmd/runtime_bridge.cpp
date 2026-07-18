#include "glasswyrmd/runtime_bridge.hpp"

#include <algorithm>
#include <array>
#include <limits>

namespace glasswyrm::server {
namespace {
constexpr std::array kRetryDelays = {
    std::chrono::milliseconds(50),  std::chrono::milliseconds(100),
    std::chrono::milliseconds(200), std::chrono::milliseconds(400),
    std::chrono::milliseconds(800), std::chrono::milliseconds(1000)};

void forget_cursor(CompositorSnapshotSubmission& submission) {
  std::vector<std::uint64_t> cursor_surfaces;
  for (const auto& surface : submission.surfaces)
    if (surface.presentation_flags == GWIPC_SURFACE_PRESENTATION_CURSOR)
      cursor_surfaces.push_back(surface.surface_id);
  const auto is_cursor = [&](const std::uint64_t surface_id) {
    return std::ranges::find(cursor_surfaces, surface_id) !=
           cursor_surfaces.end();
  };
  std::erase_if(submission.surfaces, [](const auto& surface) {
    return surface.presentation_flags == GWIPC_SURFACE_PRESENTATION_CURSOR;
  });
  std::erase_if(submission.buffers, [&](const auto& buffer) {
    return is_cursor(buffer.attach.surface_id);
  });
  std::erase_if(submission.damages, [&](const auto& damage) {
    return is_cursor(damage.surface_id);
  });
  std::erase_if(submission.surface_outputs, [&](const auto& state) {
    return is_cursor(state.state.surface_id);
  });
}

PolicySnapshotSubmission
initial_output_policy(const output::OutputLayout &layout) {
  PolicySnapshotSubmission submission;
  submission.commit_id = 1;
  submission.generation = 1;
  submission.outputs.reserve(layout.output_order.size());
  for (const auto id : layout.output_order) {
    const auto &state = layout.states.at(id);
    gwipc_policy_output_upsert record{};
    record.struct_size = sizeof(record);
    record.output_id = id.value;
    record.logical_x = state.logical_x;
    record.logical_y = state.logical_y;
    record.logical_width = state.logical_width;
    record.logical_height = state.logical_height;
    record.work_x = state.logical_x;
    record.work_y = state.logical_y;
    record.work_width = state.logical_width;
    record.work_height = state.logical_height;
    record.scale_numerator = state.scale.numerator;
    record.scale_denominator = state.scale.denominator;
    record.transform = static_cast<gwipc_transform>(state.transform);
    record.enabled = state.enabled;
    record.primary = state.primary;
    submission.outputs.push_back(record);
  }
  return submission;
}

gwipc_sdr_color_metadata color_record(const output::SdrMetadata& color) {
  return {static_cast<gwipc_sdr_color_space>(color.color_space),
          static_cast<gwipc_transfer_function>(color.transfer_function),
          static_cast<gwipc_color_primaries>(color.primaries),
          static_cast<std::uint8_t>(color.luminance_available),
          color.minimum_luminance_millinit,
          color.maximum_luminance_millinit,
          color.max_frame_average_luminance_millinit};
}

CompositorSnapshotSubmission
initial_compositor_scene(const output::OutputLayout& layout) {
  CompositorSnapshotSubmission submission;
  submission.commit_id = 1;
  submission.generation = layout.generation;
  submission.outputs.reserve(layout.output_order.size());
  for (const auto id : layout.output_order) {
    const auto& state = layout.states.at(id);
    gwipc_output_upsert record{};
    record.struct_size = sizeof(record);
    record.output_id = id.value;
    record.enabled = state.enabled;
    record.logical_x = state.logical_x;
    record.logical_y = state.logical_y;
    record.logical_width = state.logical_width;
    record.logical_height = state.logical_height;
    record.physical_pixel_width = state.physical_width;
    record.physical_pixel_height = state.physical_height;
    record.refresh_millihertz = state.refresh_millihertz;
    record.scale_numerator = state.scale.numerator;
    record.scale_denominator = state.scale.denominator;
    record.transform = static_cast<gwipc_transform>(state.transform);
    record.color = color_record(state.color);
    submission.outputs.push_back(record);
  }
  return submission;
}
}

RuntimeBridge::RuntimeBridge(std::string policy_path,
                             std::string compositor_path,
                             const gw::protocol::x11::ScreenModel screen,
                             const std::chrono::milliseconds deadline,
                             const bool software_content,
                             const bool session_state,
                             const bool cpu_buffer_synchronization,
                             const bool output_model)
    : policy_(std::move(policy_path), screen, true, output_model),
      compositor_(std::move(compositor_path), screen, software_content,
                  session_state, cpu_buffer_synchronization, output_model),
      deadline_duration_(deadline), output_model_(output_model) {}

void RuntimeBridge::start(const Clock::time_point now) noexcept {
  policy_.disconnect();
  compositor_.disconnect();
  stage_ = output_model_ ? Stage::Compositor : Stage::Policy;
  deadline_ = now + deadline_duration_;
  retry_at_ = now;
  retry_index_ = 0;
  transaction_stage_ = TransactionStage::None;
  resume_transaction_stage_ = TransactionStage::None;
  recovering_ = false;
  compositor_reset_ = false;
  output_scene_submitted_ = false;
}

void RuntimeBridge::schedule_retry(const Clock::time_point now) noexcept {
  const auto index =
      std::min<std::size_t>(retry_index_, kRetryDelays.size() - 1);
  retry_at_ = now + kRetryDelays[index];
  if (retry_index_ + 1 < kRetryDelays.size())
    ++retry_index_;
}

bool RuntimeBridge::service(const short policy_revents,
                            const short compositor_revents,
                            const Clock::time_point now, std::string &error) {
  if (stage_ == Stage::Failed)
    return false;
  if (stage_ == Stage::Ready) {
    std::string peer_error;
    const auto policy_outcome = policy_.process(policy_revents, peer_error);
    const auto compositor_outcome =
        compositor_.process(compositor_revents, peer_error);
    if (policy_outcome == PeerProcessOutcome::Fatal ||
        compositor_outcome == PeerProcessOutcome::Fatal) {
      stage_ = Stage::Failed;
      error = peer_error.empty() ? "peer protocol divergence" : peer_error;
      return false;
    }
    if (policy_outcome == PeerProcessOutcome::Disconnected ||
        compositor_outcome == PeerProcessOutcome::Disconnected) {
      const bool policy_disconnected =
          policy_outcome == PeerProcessOutcome::Disconnected;
      bool compositor_disconnected =
          compositor_outcome == PeerProcessOutcome::Disconnected;
      const bool policy_in_flight =
          transaction_stage_ == TransactionStage::Policy;
      const bool compositor_in_flight =
          transaction_stage_ == TransactionStage::Compositor ||
          transaction_stage_ == TransactionStage::Content ||
          transaction_stage_ == TransactionStage::Cursor ||
          transaction_stage_ == TransactionStage::Replay;
      resume_transaction_stage_ = transaction_stage_;
      recovering_ = true;
      if (policy_disconnected) policy_.disconnect();
      if (compositor_disconnected) compositor_.disconnect();
      if (compositor_disconnected) output_scene_submitted_ = false;

      // Preserve an unaffected peer across an idle peer restart.  An in-flight
      // transaction still uses the conservative paired reconnect so its reply
      // cannot race replay on the retained transport.
      if (policy_disconnected && compositor_in_flight) {
        compositor_.disconnect();
        compositor_disconnected = true;
      }
      if (compositor_disconnected && policy_in_flight) policy_.disconnect();
      compositor_reset_ = compositor_reset_ || compositor_disconnected;
      stage_ = output_model_ &&
                       compositor_.state() ==
                           PeerBootstrapState::Disconnected
                   ? Stage::Compositor
               : policy_.state() == PeerBootstrapState::Disconnected
                   ? Stage::Policy
                   : Stage::Compositor;
      deadline_ = now + deadline_duration_;
      retry_index_ = 0;
      schedule_retry(now);
      return true;
    }
    if (policy_outcome == PeerProcessOutcome::SemanticRejected)
      transaction_stage_ = TransactionStage::PolicyRejected;
    if (compositor_outcome == PeerProcessOutcome::SemanticRejected)
      transaction_stage_ = transaction_stage_ == TransactionStage::Content
                               ? TransactionStage::ContentRejected
                           : transaction_stage_ == TransactionStage::Cursor
                               ? TransactionStage::CursorRejected
                           : transaction_stage_ == TransactionStage::Replay
                               ? TransactionStage::ReplayRejected
                               : TransactionStage::CompositorRejected;
    if (transaction_stage_ == TransactionStage::Policy &&
        policy_.state() == PeerBootstrapState::Synchronized)
      transaction_stage_ = TransactionStage::PolicyReady;
    if (transaction_stage_ == TransactionStage::Compositor &&
        compositor_.state() == PeerBootstrapState::Synchronized)
      transaction_stage_ = TransactionStage::Complete;
    if (transaction_stage_ == TransactionStage::Content &&
        compositor_.state() == PeerBootstrapState::Synchronized)
      transaction_stage_ = TransactionStage::ContentComplete;
    if (transaction_stage_ == TransactionStage::Cursor &&
        compositor_.state() == PeerBootstrapState::Synchronized)
      transaction_stage_ = TransactionStage::CursorComplete;
    if (transaction_stage_ == TransactionStage::Replay &&
        compositor_.state() == PeerBootstrapState::Synchronized)
      transaction_stage_ = TransactionStage::ReplayComplete;
    return true;
  }
  if (stage_ != Stage::Ready && now >= deadline_) {
    stage_ = Stage::Failed;
    error = "integrated peer bootstrap deadline expired";
    return false;
  }
  if (stage_ == Stage::Policy) {
    if (policy_.state() == PeerBootstrapState::Disconnected &&
        now >= retry_at_) {
      std::string attempt_error;
      if (!policy_.connect(attempt_error))
        schedule_retry(now);
    }
    if (policy_.state() == PeerBootstrapState::Connecting ||
        policy_.state() == PeerBootstrapState::AwaitingReply) {
      const auto outcome = policy_.process(policy_revents, error);
      if (outcome == PeerProcessOutcome::Fatal ||
          outcome == PeerProcessOutcome::SemanticRejected) return false;
      if (outcome == PeerProcessOutcome::Disconnected) {
        policy_.disconnect();
        schedule_retry(now);
      }
    }
    if (output_model_ && policy_.ready_for_snapshot()) {
      const auto *layout = compositor_.output_layout();
      if (layout == nullptr) {
        stage_ = Stage::Failed;
        error = "output-model policy bootstrap has no compositor inventory";
        return false;
      }
      auto bootstrap = initial_output_policy(*layout);
      if (!policy_.submit(bootstrap, error)) {
        stage_ = Stage::Failed;
        if (error.empty()) error = "could not submit output policy bootstrap";
        return false;
      }
    } else if (policy_.state() == PeerBootstrapState::Synchronized) {
      stage_ = Stage::Compositor;
      retry_at_ = now;
      retry_index_ = 0;
    }
  }
  if (stage_ == Stage::Compositor) {
    if (compositor_.state() == PeerBootstrapState::Disconnected &&
        now >= retry_at_) {
      std::string attempt_error;
      if (!compositor_.connect(attempt_error))
        schedule_retry(now);
    }
    if (compositor_.state() == PeerBootstrapState::Connecting ||
        compositor_.state() == PeerBootstrapState::AwaitingReply) {
      const auto outcome = compositor_.process(compositor_revents, error);
      if (outcome == PeerProcessOutcome::Fatal) return false;
      if (outcome == PeerProcessOutcome::SemanticRejected) {
        if (error.empty()) error = "compositor bootstrap frame was rejected";
        return false;
      }
      if (outcome == PeerProcessOutcome::Disconnected) {
        compositor_.disconnect();
        schedule_retry(now);
      }
    }
    if (compositor_.state() == PeerBootstrapState::Synchronized) {
      if (output_model_ &&
          policy_.state() == PeerBootstrapState::Disconnected) {
        stage_ = Stage::Policy;
        retry_at_ = now;
        retry_index_ = 0;
        return true;
      }
      if (output_model_ && !output_scene_submitted_) {
        const auto* layout = compositor_.output_layout();
        if (layout == nullptr) {
          stage_ = Stage::Failed;
          error = "output-model scene bootstrap has no compositor inventory";
          return false;
        }
        const auto& replay = compositor_.replay_input();
        const auto submission = replay.commit_id != 0
                                    ? replay
                                    : initial_compositor_scene(*layout);
        if (!compositor_.submit(submission, error)) {
          stage_ = Stage::Failed;
          if (error.empty())
            error = "could not submit output-model scene bootstrap";
          return false;
        }
        output_scene_submitted_ = true;
        return true;
      }
      stage_ = Stage::Ready;
      if (recovering_) {
        transaction_stage_ = TransactionStage::None;
        recovering_ = false;
        std::string resume_error;
        if (resume_transaction_stage_ == TransactionStage::Policy &&
            !submit_policy(pending_policy_, resume_error)) {
          error = resume_error;
          return false;
        }
        if (resume_transaction_stage_ == TransactionStage::Compositor) {
          transaction_stage_ = TransactionStage::PolicyReady;
          if (!submit_compositor(pending_compositor_, resume_error)) {
            error = resume_error;
            return false;
          }
        }
        if (resume_transaction_stage_ == TransactionStage::Content &&
            !submit_content(pending_content_, resume_error)) {
          error = resume_error;
          return false;
        }
        if (resume_transaction_stage_ == TransactionStage::Cursor) {
          if (!compositor_.submit(pending_compositor_, resume_error)) {
            error = resume_error;
            return false;
          }
          transaction_stage_ = TransactionStage::Cursor;
        }
        if (resume_transaction_stage_ == TransactionStage::Replay &&
            !submit_replay(pending_compositor_, resume_error)) {
          error = resume_error;
          return false;
        }
        if (resume_transaction_stage_ == TransactionStage::PolicyReady ||
            resume_transaction_stage_ == TransactionStage::PolicyRejected ||
            resume_transaction_stage_ == TransactionStage::Complete ||
            resume_transaction_stage_ == TransactionStage::ContentComplete ||
            resume_transaction_stage_ == TransactionStage::CursorComplete ||
            resume_transaction_stage_ == TransactionStage::CompositorRejected ||
            resume_transaction_stage_ == TransactionStage::ContentRejected ||
            resume_transaction_stage_ == TransactionStage::CursorRejected ||
            resume_transaction_stage_ == TransactionStage::ReplayComplete ||
            resume_transaction_stage_ == TransactionStage::ReplayRejected)
          transaction_stage_ = resume_transaction_stage_;
      }
    }
  }
  return true;
}

bool RuntimeBridge::submit_policy(const PolicySnapshotSubmission& submission,
                                  std::string& error) {
  if (!ready() || transaction_stage_ != TransactionStage::None) {
    error = "policy submission attempted while transaction stage is busy";
    return false;
  }
  if (!policy_.submit(submission, error)) return false;
  pending_policy_ = submission;
  transaction_stage_ = TransactionStage::Policy;
  return true;
}

bool RuntimeBridge::policy_result_ready() const noexcept {
  return transaction_stage_ == TransactionStage::PolicyReady;
}
bool RuntimeBridge::policy_rejected_ready() const noexcept {
  return transaction_stage_ == TransactionStage::PolicyRejected;
}

bool RuntimeBridge::submit_compositor(
    const CompositorSnapshotSubmission& submission, std::string& error) {
  if (!ready() || transaction_stage_ != TransactionStage::PolicyReady) return false;
  if (!compositor_.submit(submission, error)) return false;
  pending_compositor_ = submission;
  transaction_stage_ = TransactionStage::Compositor;
  return true;
}

bool RuntimeBridge::submit_content(
    const CompositorContentSubmission& submission, std::string& error) {
  if (!ready() || transaction_stage_ != TransactionStage::None) {
    error = "content submission attempted while transaction stage is busy";
    return false;
  }
  if (!compositor_.submit_content(submission, error)) return false;
  pending_content_ = submission;
  transaction_stage_ = TransactionStage::Content;
  return true;
}
bool RuntimeBridge::submit_cursor(
    const CompositorCursorSubmission& submission,
    const std::uint64_t commit_id, const std::uint64_t generation,
    std::string& error) {
  if (!ready() || transaction_stage_ != TransactionStage::None) {
    error = "cursor submission attempted while transaction stage is busy";
    return false;
  }
  if (!compositor_.submit_cursor(submission, commit_id, generation, error))
    return false;
  pending_compositor_ = compositor_.replay_input();
  pending_compositor_.commit_id = commit_id;
  pending_compositor_.generation = generation;
  std::erase_if(pending_compositor_.surfaces, [](const auto& surface) {
    return surface.presentation_flags == GWIPC_SURFACE_PRESENTATION_CURSOR;
  });
  pending_compositor_.surfaces.push_back(submission.surface);
  pending_compositor_.buffers.clear();
  pending_compositor_.damages.clear();
  if (submission.buffer) pending_compositor_.buffers.push_back(*submission.buffer);
  if (submission.damage) pending_compositor_.damages.push_back(*submission.damage);
  transaction_stage_ = TransactionStage::Cursor;
  return true;
}
bool RuntimeBridge::submit_replay(
    const CompositorSnapshotSubmission& submission, std::string& error) {
  if (!ready() || transaction_stage_ != TransactionStage::None) {
    error = "compositor replay attempted while transaction stage is busy";
    return false;
  }
  if (!compositor_.submit(submission, error)) return false;
  pending_compositor_ = submission;
  transaction_stage_ = TransactionStage::Replay;
  return true;
}
bool RuntimeBridge::compositor_rejected_ready() const noexcept {
  return transaction_stage_ == TransactionStage::CompositorRejected;
}

bool RuntimeBridge::compositor_result_ready() const noexcept {
  return transaction_stage_ == TransactionStage::Complete;
}
bool RuntimeBridge::content_result_ready() const noexcept {
  return transaction_stage_ == TransactionStage::ContentComplete;
}
bool RuntimeBridge::content_rejected_ready() const noexcept {
  return transaction_stage_ == TransactionStage::ContentRejected;
}
bool RuntimeBridge::cursor_result_ready() const noexcept {
  return transaction_stage_ == TransactionStage::CursorComplete;
}
bool RuntimeBridge::cursor_rejected_ready() const noexcept {
  return transaction_stage_ == TransactionStage::CursorRejected;
}
bool RuntimeBridge::replay_result_ready() const noexcept {
  return transaction_stage_ == TransactionStage::ReplayComplete;
}
bool RuntimeBridge::replay_rejected_ready() const noexcept {
  return transaction_stage_ == TransactionStage::ReplayRejected;
}
bool RuntimeBridge::transaction_idle() const noexcept {
  return transaction_stage_ == TransactionStage::None;
}

void RuntimeBridge::forget_cursor_replay() noexcept {
  compositor_.forget_cursor_replay();
  forget_cursor(pending_compositor_);
  const auto cursor_stage = [](const TransactionStage stage) {
    return stage == TransactionStage::Cursor ||
           stage == TransactionStage::CursorComplete ||
           stage == TransactionStage::CursorRejected;
  };
  if (cursor_stage(transaction_stage_))
    transaction_stage_ = TransactionStage::None;
  if (cursor_stage(resume_transaction_stage_))
    resume_transaction_stage_ = TransactionStage::None;
}

bool RuntimeBridge::prepare_rollback() noexcept {
  if (!ready() ||
      (transaction_stage_ != TransactionStage::PolicyReady &&
       transaction_stage_ != TransactionStage::PolicyRejected &&
       transaction_stage_ != TransactionStage::Complete &&
       transaction_stage_ != TransactionStage::CompositorRejected))
    return false;
  transaction_stage_ = TransactionStage::None;
  return true;
}

void RuntimeBridge::clear_transaction_result() noexcept {
  transaction_stage_ = TransactionStage::None;
}

bool RuntimeBridge::ready() const noexcept { return stage_ == Stage::Ready; }

int RuntimeBridge::poll_timeout_ms(const Clock::time_point now) const noexcept {
  if (stage_ == Stage::Ready)
    return -1;
  const auto wake = std::min(deadline_, retry_at_);
  if (wake <= now)
    return 0;
  const auto count =
      std::chrono::duration_cast<std::chrono::milliseconds>(wake - now).count();
  return static_cast<int>(
      std::min<std::int64_t>(count, std::numeric_limits<int>::max()));
}

} // namespace glasswyrm::server
