#include "gwcomp/presentation_transaction.hpp"

#include <limits>
#include <poll.h>

namespace gw::compositor {

int PresentationTransaction::timeout_ms(const Compositor& compositor) {
  if (!compositor.pending_presentation_)
    return -1;
  const auto remaining = compositor.pending_presentation_->deadline_ -
                         compositor.timing_.now();
  if (remaining <= std::chrono::steady_clock::duration::zero())
    return 0;
  auto milliseconds =
      std::chrono::duration_cast<std::chrono::milliseconds>(remaining);
  if (milliseconds < remaining)
    ++milliseconds;
  if (milliseconds.count() > std::numeric_limits<int>::max())
    return std::numeric_limits<int>::max();
  return static_cast<int>(milliseconds.count());
}

void PresentationTransaction::abort(
    Compositor& compositor, const std::string_view reason) noexcept {
  if (!compositor.pending_presentation_)
    return;
  compositor.presenter_->abort_pending(
      compositor.pending_presentation_->token_, reason);
  compositor.pending_presentation_.reset();
}

PresentationCompletion PresentationTransaction::service(
    Compositor& compositor, const short revents, std::string& error) {
  PresentationCompletion completion;
  if (!compositor.pending_presentation_)
    return completion;
  const auto fatal = [&](std::string message) {
    completion.kind = PresentationCompletionKind::Fatal;
    completion.commit = compositor.pending_presentation_->commit_;
    completion.frame = compositor.pending_presentation_->presented_;
    completion.frame.disposition = PresentedFrame::Disposition::Fatal;
    abort(compositor, message);
    error = std::move(message);
    return completion;
  };
  if ((revents & (POLLERR | POLLHUP | POLLNVAL)) != 0)
    return fatal("presentation backend became unusable during a pending frame");
  if (revents != 0) {
    const auto event = compositor.presenter_->service(revents);
    if (event.kind == glasswyrm::output::BackendEventKind::Fatal)
      return fatal(event.error);
    if (event.kind == glasswyrm::output::BackendEventKind::Complete) {
      if (event.token != compositor.pending_presentation_->token_)
        return fatal("presentation completion token does not match pending frame");
      const auto canonical_hash =
          compositor.pending_presentation_->frame_set_
              ? compositor.pending_presentation_->frame_set_->aggregate_hash()
              : compositor.pending_presentation_->frame_.visible_hash();
      if (event.visible_hash != canonical_hash)
        return fatal(
            "completed presentation hash differs from canonical software frame");
      std::string finalize_error;
      if (!compositor.presenter_->finalize_pending(event.token,
                                                   finalize_error))
        return fatal(finalize_error.empty()
                         ? "could not finalize pending presentation diagnostics"
                         : finalize_error);
      if (!compositor.pending_presentation_->publish_manifest(compositor,
                                                              error))
        return fatal(error);
      auto pending = std::move(compositor.pending_presentation_);
      completion.kind = PresentationCompletionKind::Complete;
      completion.commit = pending->commit_;
      completion.frame = pending->promote(compositor, event.visible_hash);
      error.clear();
      return completion;
    }
  }
  if (compositor.timing_.now() >=
      compositor.pending_presentation_->deadline_)
    return fatal("pending presentation exceeded its completion timeout");
  return completion;
}

} // namespace gw::compositor
