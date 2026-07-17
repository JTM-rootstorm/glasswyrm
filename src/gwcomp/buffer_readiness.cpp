#include "gwcomp/compositor.hpp"

#include "gwcomp/presentation_transaction.hpp"

#include <algorithm>
#include <chrono>
#include <limits>
#include <poll.h>
#include <set>
#include <utility>

namespace gw::compositor {
namespace {

int remaining_timeout_ms(
    const PresentationTiming& timing,
    const PresentationTiming::Clock::time_point deadline) {
  const auto remaining = deadline - timing.now();
  if (remaining <= PresentationTiming::Clock::duration::zero()) return 0;
  auto milliseconds =
      std::chrono::duration_cast<std::chrono::milliseconds>(remaining);
  if (milliseconds < remaining) ++milliseconds;
  if (milliseconds.count() > std::numeric_limits<int>::max())
    return std::numeric_limits<int>::max();
  return static_cast<int>(milliseconds.count());
}

template <class Pending>
PresentationCompletion fatal_readiness(Pending& pending, std::string message,
                                       std::string& error) {
  PresentationCompletion completion;
  completion.kind = PresentationCompletionKind::Fatal;
  completion.commit = pending.commit;
  completion.frame.disposition = PresentedFrame::Disposition::Fatal;
  completion.frame.result = GWIPC_FRAME_REJECTED_INVALID_BUFFER;
  error = std::move(message);
  return completion;
}

}  // namespace

PresentedFrame Compositor::commit(const gwipc_frame_commit& value,
                                  std::string& error) {
  if (presentation_pending()) {
    error = "frame commit cannot overtake a pending presentation";
    return {};
  }

  std::vector<Mapping> synchronized;
  std::set<std::uint64_t> selected_buffers;
  for (const auto surface_id : scene_.pending_damage_surface_ids()) {
    const auto attachment = pending_attachments_.find(surface_id);
    if (attachment == pending_attachments_.end() ||
        !selected_buffers.insert(attachment->second).second)
      continue;
    const auto mapping = mappings_.find(attachment->second);
    if (mapping != mappings_.end() &&
        mapping->second->synchronization() ==
            GWIPC_SYNCHRONIZATION_EVENTFD)
      synchronized.push_back(mapping->second);
  }
  for (std::size_t index = 0; index < synchronized.size(); ++index) {
    const auto readiness = synchronized[index]->consume_readiness(error);
    if (readiness == BufferReadiness::Fatal) {
      PresentedFrame result;
      result.disposition = PresentedFrame::Disposition::Fatal;
      result.result = GWIPC_FRAME_REJECTED_INVALID_BUFFER;
      return result;
    }
    if (readiness == BufferReadiness::WouldBlock) {
      pending_buffer_readiness_.emplace(PendingBufferReadiness{
          value, std::move(synchronized), index,
          timing_.now() + timing_.timeout});
      PresentedFrame result;
      result.disposition = PresentedFrame::Disposition::Pending;
      result.result = GWIPC_FRAME_ACCEPTED;
      result.generation = value.producer_generation;
      error.clear();
      return result;
    }
  }
  return PresentationTransaction::commit(*this, value, error);
}

bool Compositor::presentation_pending() const noexcept {
  return pending_buffer_readiness_.has_value() ||
         pending_presentation_ != nullptr;
}

int Compositor::presentation_poll_fd() const noexcept {
  if (pending_buffer_readiness_) {
    return pending_buffer_readiness_->mappings
        .at(pending_buffer_readiness_->next)
        ->readiness_fd();
  }
  return presenter_->poll_fd();
}

short Compositor::presentation_poll_events() const noexcept {
  return pending_buffer_readiness_ ? POLLIN : presenter_->poll_events();
}

int Compositor::presentation_timeout_ms() const {
  if (pending_buffer_readiness_)
    return remaining_timeout_ms(timing_, pending_buffer_readiness_->deadline);
  return PresentationTransaction::timeout_ms(*this);
}

PresentationCompletion Compositor::service_presentation(
    const short revents, std::string& error) {
  if (!pending_buffer_readiness_)
    return PresentationTransaction::service(*this, revents, error);

  if (presentation_timeout_ms() == 0) {
    auto completion = fatal_readiness(*pending_buffer_readiness_,
                                      "buffer readiness timed out", error);
    pending_buffer_readiness_.reset();
    return completion;
  }
  if ((revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
    auto completion = fatal_readiness(
        *pending_buffer_readiness_,
        "buffer readiness descriptor became unusable", error);
    pending_buffer_readiness_.reset();
    return completion;
  }

  auto& pending = *pending_buffer_readiness_;
  while (pending.next < pending.mappings.size()) {
    const auto readiness =
        pending.mappings[pending.next]->consume_readiness(error);
    if (readiness == BufferReadiness::WouldBlock) return {};
    if (readiness == BufferReadiness::Fatal) {
      const auto detail = error;
      auto completion = fatal_readiness(pending, detail, error);
      pending_buffer_readiness_.reset();
      return completion;
    }
    ++pending.next;
  }

  const auto commit_value = pending.commit;
  pending_buffer_readiness_.reset();
  const auto frame = PresentationTransaction::commit(*this, commit_value, error);
  if (frame.disposition == PresentedFrame::Disposition::Pending) return {};
  PresentationCompletion completion;
  completion.kind = frame.disposition == PresentedFrame::Disposition::Fatal
                        ? PresentationCompletionKind::Fatal
                        : PresentationCompletionKind::Complete;
  completion.commit = commit_value;
  completion.frame = frame;
  return completion;
}

}  // namespace gw::compositor
