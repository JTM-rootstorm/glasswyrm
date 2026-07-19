#include "gwcomp/runtime_reactor.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <memory>
#include <poll.h>

namespace glasswyrm::compositor {
namespace {

constexpr std::uint64_t kOutputModelCapabilities =
    GWIPC_CAP_OUTPUT_MANAGEMENT | GWIPC_CAP_SURFACE_OUTPUT_MEMBERSHIP |
    GWIPC_CAP_SCALE_METADATA;

constexpr std::size_t kMaximumMessagesPerTurn = 64;
constexpr std::size_t kMaximumPayloadBytesPerTurn = 512U * 1024U;

struct MessageDeleter {
  void operator()(gwipc_message* value) const { gwipc_message_destroy(value); }
};

int minimum_timeout(const int left, const int right) {
  if (left < 0) return right;
  if (right < 0) return left;
  return std::min(left, right);
}

}  // namespace

RuntimeReactor::RuntimeReactor(const Options& options, gwipc_listener* listener,
                               SignalRuntime& signals,
                               gw::compositor::Compositor& compositor,
                               const output::OutputLayout& output_layout)
    : options_(options),
      listener_(listener),
      signals_(signals),
      compositor_(compositor),
      output_inventory_(output_layout) {}

void RuntimeReactor::ConnectionDeleter::operator()(
    gwipc_connection* value) const {
  gwipc_connection_destroy(value);
}

int RuntimeReactor::poll(PollEvents& events) {
  pollfd descriptors[4] = {
      {gwipc_listener_fd(listener_),
       static_cast<short>(producer_ ? 0 : POLLIN), 0},
      {producer_ ? gwipc_connection_fd(producer_.get()) : -1,
       static_cast<short>(producer_
                              ? gwipc_connection_wanted_poll_events(
                                    producer_.get())
                              : 0),
       0},
      {signals_.poll_fd(), POLLIN, 0},
      {compositor_.presentation_poll_fd(),
       compositor_.presentation_poll_events(), 0}};
  const int timeout =
      minimum_timeout(compositor_.presentation_timeout_ms(),
                      session_state_.timeout_ms());
  const int count =
      ::poll(descriptors, 4, buffered_work_pending_ ? 0 : timeout);
  buffered_work_pending_ = false;
  if (count < 0) {
    if (errno == EINTR) return 0;
    std::perror("gwcomp: poll");
    exit_status_ = 1;
    stopping_ = true;
    return -1;
  }
  events = {descriptors[0].revents, descriptors[1].revents,
            descriptors[2].revents, descriptors[3].revents};
  return 1;
}

void RuntimeReactor::service_signals(const short revents) {
  if ((revents & POLLIN) == 0) return;
  const auto events = signals_.drain();
  if (events.stop) stopping_ = true;
  if (!stopping_ && events.virtual_terminal_release)
    vt_release_requested_ = true;
  if (!stopping_ && events.virtual_terminal_acquire)
    vt_acquire_requested_ = true;
}

void RuntimeReactor::apply_dispatch(const ContractDispatchResult& dispatch) {
  accepted_any_frame_ = accepted_any_frame_ || dispatch.accepted_frame;
  stop_after_flush_ = stop_after_flush_ || dispatch.stop_after_flush;
  if (dispatch.pending_frame)
    pending_reply_sequence_ = dispatch.reply_sequence;
  if (dispatch.fatal) {
    stopping_ = true;
    exit_status_ = 1;
  }
}

void RuntimeReactor::fail_output_inventory(const char* reason,
                                           const gwipc_status status) {
  std::fprintf(stderr, "gwcomp: output inventory failed: %s: %s\n", reason,
               gwipc_status_string(status));
  session_state_.peer_disconnected();
  compositor_.disconnect();
  producer_.reset();
  peer_validated_ = false;
  peer_role_ = GWIPC_ROLE_UNKNOWN;
  peer_profile_.reset();
  stopping_ = true;
  exit_status_ = 1;
}

void RuntimeReactor::reject_output_inventory_peer(
    const char* reason, const gwipc_status status) {
  std::fprintf(stderr, "gwcomp: rejected output inventory peer: %s: %s\n",
               reason, gwipc_status_string(status));
  session_state_.peer_disconnected();
  compositor_.disconnect();
  producer_.reset();
  peer_validated_ = false;
  peer_role_ = GWIPC_ROLE_UNKNOWN;
  peer_profile_.reset();
}

OutputInventoryDisposition
RuntimeReactor::service_output_inventory_query(const gwipc_message& message) {
  if (!producer_ || !peer_validated_)
    return OutputInventoryDisposition::NotHandled;
  const auto result =
      output_inventory_.service(*producer_, peer_role_, message);
  if (result.disposition == OutputInventoryDisposition::RejectPeer)
    reject_output_inventory_peer(result.reason.c_str(), result.status);
  else if (result.disposition == OutputInventoryDisposition::Fatal)
    fail_output_inventory(result.reason.c_str(), result.status);
  return result.disposition;
}

void RuntimeReactor::service_presentation(const short revents) {
  if (!compositor_.presentation_pending() ||
      (revents == 0 && compositor_.presentation_timeout_ms() != 0))
    return;
  std::string error;
  const auto completion = compositor_.service_presentation(revents, error);
  if (completion.kind == gw::compositor::PresentationCompletionKind::Fatal) {
    std::fprintf(stderr, "gwcomp: presentation failed: %s\n", error.c_str());
    stopping_ = true;
    exit_status_ = 1;
    return;
  }
  if (completion.kind !=
      gw::compositor::PresentationCompletionKind::Complete)
    return;
  if (!producer_ || !pending_reply_sequence_) {
    std::fprintf(
        stderr,
        "gwcomp: presentation completed without a producer reply target\n");
    stopping_ = true;
    exit_status_ = 1;
    return;
  }
  const auto dispatch = complete_contract_presentation(
      producer_.get(), *pending_reply_sequence_, completion,
      options_.max_frames, compositor_);
  pending_reply_sequence_.reset();
  apply_dispatch(dispatch);
}

void RuntimeReactor::service_virtual_terminal() {
  if (vt_release_requested_ && !compositor_.presentation_pending() &&
      !compositor_.presentation_suspended()) {
    std::string error;
    if (session_state_.enabled() &&
        session_state_.state() == CoordinatedSessionState::Active) {
      GwipcSessionStateRequestSink sink(producer_.get());
      if (!producer_ || !session_state_.request_inactive(sink, error)) {
        std::fprintf(stderr,
                     "gwcomp: VT release coordination failed: %s\n",
                     error.c_str());
        stopping_ = true;
        exit_status_ = 1;
      }
    } else if (!session_state_.waiting() &&
               (!session_state_.enabled() ||
                session_state_.state() == CoordinatedSessionState::Inactive) &&
               !compositor_.suspend_presentation(error)) {
      std::fprintf(stderr, "gwcomp: VT release failed: %s\n", error.c_str());
      stopping_ = true;
      exit_status_ = 1;
    } else if (!session_state_.waiting() &&
               (!session_state_.enabled() ||
                session_state_.state() ==
                    CoordinatedSessionState::Inactive)) {
      vt_release_requested_ = false;
    }
  }
  if (stopping_ || !vt_acquire_requested_ ||
      !compositor_.presentation_suspended())
    return;

  std::string error;
  if (session_state_.enabled() &&
      session_state_.state() == CoordinatedSessionState::Inactive) {
    if (!compositor_.resume_presentation(error)) {
      std::fprintf(stderr, "gwcomp: VT acquire failed: %s\n", error.c_str());
      stopping_ = true;
      exit_status_ = 1;
    } else {
      GwipcSessionStateRequestSink sink(producer_.get());
      if (!producer_ || !session_state_.request_active(sink, error)) {
        std::fprintf(stderr,
                     "gwcomp: VT acquire coordination failed: %s\n",
                     error.c_str());
        stopping_ = true;
        exit_status_ = 1;
      }
    }
  } else if (!session_state_.waiting() &&
             (!session_state_.enabled() ||
              session_state_.state() == CoordinatedSessionState::Active) &&
             !compositor_.resume_presentation(error)) {
    std::fprintf(stderr, "gwcomp: VT acquire failed: %s\n", error.c_str());
    stopping_ = true;
    exit_status_ = 1;
  } else if (!session_state_.waiting() &&
             (!session_state_.enabled() ||
              session_state_.state() == CoordinatedSessionState::Active)) {
    vt_acquire_requested_ = false;
  }
}

void RuntimeReactor::accept_producer(const short revents) {
  if (producer_ || (revents & POLLIN) == 0) return;
  gwipc_connection* accepted = nullptr;
  if (gwipc_listener_accept(listener_, &accepted) != GWIPC_STATUS_OK) return;
  producer_.reset(accepted);
  const auto peer = gwipc_connection_peer_info(producer_.get());
  std::fprintf(stderr, "gwcomp: producer connected pid=%d uid=%u\n", peer.pid,
               peer.uid);
  peer_validated_ = false;
  peer_role_ = GWIPC_ROLE_UNKNOWN;
  peer_profile_.reset();
}

void RuntimeReactor::service_producer_transport(const short revents) {
  if (producer_ && revents != 0)
    (void)gwipc_connection_process_poll_events(producer_.get(), revents);
}

void RuntimeReactor::validate_producer() {
  if (!producer_ || peer_validated_ ||
      gwipc_connection_get_state(producer_.get()) !=
          GWIPC_CONNECTION_ESTABLISHED)
    return;
  const auto info = gwipc_connection_peer_info(producer_.get());
  peer_role_ = info.role;
  const auto negotiated_output_model =
      info.capabilities & kOutputModelCapabilities;
  peer_profile_ =
      gw::compositor::select_peer_profile(peer_role_, info.capabilities);
  if (peer_profile_ && !options_.headless_outputs.empty() &&
      negotiated_output_model != kOutputModelCapabilities)
    peer_profile_.reset();
  if (!peer_profile_) {
    std::fprintf(
        stderr,
        "gwcomp: peer role=%u has invalid role capability profile\n",
        static_cast<unsigned>(peer_role_));
    compositor_.disconnect();
    producer_.reset();
    peer_role_ = GWIPC_ROLE_UNKNOWN;
    return;
  }
  const auto scene_profile =
      negotiated_output_model == kOutputModelCapabilities
          ? gw::compositor::SceneProfile::OutputModel
          : gw::compositor::SceneProfile::Historical;
  const auto primary_output_id =
      scene_profile == gw::compositor::SceneProfile::OutputModel
          ? output_inventory_.layout().primary_output_id.value
          : 0;
  const auto output_layout_generation =
      scene_profile == gw::compositor::SceneProfile::OutputModel
          ? output_inventory_.layout().generation
          : 0;
  if (!compositor_.configure_scene_profile(
          scene_profile, primary_output_id, output_layout_generation)) {
    std::fprintf(stderr,
                 "gwcomp: could not configure negotiated scene profile\n");
    compositor_.disconnect();
    producer_.reset();
    peer_role_ = GWIPC_ROLE_UNKNOWN;
    peer_profile_.reset();
    return;
  }
  compositor_.set_peer_profile(*peer_profile_);
  compositor_.set_cpu_buffer_synchronization(
      (info.capabilities & GWIPC_CAP_CPU_BUFFER_SYNCHRONIZATION) != 0);
  peer_validated_ = true;
  session_state_.configure(
      peer_role_ == GWIPC_ROLE_PROTOCOL_SERVER &&
      (info.capabilities & GWIPC_CAP_SESSION_STATE) != 0);
}

void RuntimeReactor::service_session_messages() {
  if (!producer_ || !peer_validated_ || !session_state_.waiting()) return;
  std::size_t messages = 0;
  std::size_t payload_bytes = 0;
  while (!stopping_ && producer_ && peer_validated_ &&
         session_state_.waiting() && messages < kMaximumMessagesPerTurn &&
         payload_bytes < kMaximumPayloadBytesPerTurn) {
    gwipc_message* raw_message = nullptr;
    if (gwipc_connection_receive(producer_.get(), &raw_message) !=
        GWIPC_STATUS_OK)
      break;
    std::unique_ptr<gwipc_message, MessageDeleter> message(raw_message);
    std::size_t size = 0;
    (void)gwipc_message_payload(message.get(), &size);
    payload_bytes += size;
    ++messages;
    const auto output_query =
        service_output_inventory_query(*message);
    if (output_query == OutputInventoryDisposition::RejectPeer ||
        output_query == OutputInventoryDisposition::Fatal) {
      break;
    } else if (output_query == OutputInventoryDisposition::Handled) {
      continue;
    } else if (gwipc_message_type(message.get()) ==
        GWIPC_MESSAGE_SESSION_STATE_ACKNOWLEDGED) {
      gwipc_session_state_acknowledged acknowledged{};
      std::uint64_t reply_to = 0;
      std::string error;
      if (!decode_session_state_acknowledgement(
              message.get(), acknowledged, reply_to, error) ||
          !session_state_.acknowledge(reply_to, acknowledged, error)) {
        std::fprintf(stderr, "gwcomp: session coordination failed: %s\n",
                     error.c_str());
        stopping_ = true;
        exit_status_ = 1;
      } else if (vt_acquire_requested_ &&
                 session_state_.state() == CoordinatedSessionState::Active) {
        vt_acquire_requested_ = false;
      }
    } else {
      apply_dispatch(dispatch_contract_message(
          producer_.get(), message.get(), peer_role_, *peer_profile_,
          options_.max_frames, compositor_));
    }
  }
  if (session_state_.waiting() &&
      (messages == kMaximumMessagesPerTurn ||
       payload_bytes >= kMaximumPayloadBytesPerTurn))
    buffered_work_pending_ = true;
}

void RuntimeReactor::service_contract_messages() {
  if (!producer_ || !peer_validated_ || compositor_.presentation_pending() ||
      compositor_.presentation_suspended() || vt_release_requested_ ||
      session_state_.waiting())
    return;
  std::size_t messages = 0;
  std::size_t payload_bytes = 0;
  while (producer_ && peer_validated_ && !compositor_.presentation_pending() &&
         !compositor_.presentation_suspended() && !vt_release_requested_ &&
         !session_state_.waiting() && messages < kMaximumMessagesPerTurn &&
         payload_bytes < kMaximumPayloadBytesPerTurn) {
    gwipc_message* raw_message = nullptr;
    if (gwipc_connection_receive(producer_.get(), &raw_message) !=
        GWIPC_STATUS_OK)
      break;
    std::unique_ptr<gwipc_message, MessageDeleter> message(raw_message);
    std::size_t size = 0;
    (void)gwipc_message_payload(message.get(), &size);
    payload_bytes += size;
    ++messages;
    const auto output_query =
        service_output_inventory_query(*message);
    if (output_query == OutputInventoryDisposition::RejectPeer ||
        output_query == OutputInventoryDisposition::Fatal)
      break;
    if (output_query == OutputInventoryDisposition::Handled) continue;
    apply_dispatch(dispatch_contract_message(
        producer_.get(), message.get(), peer_role_, *peer_profile_,
        options_.max_frames, compositor_));
    if (stopping_) break;
  }
  if (producer_ && peer_validated_ && !compositor_.presentation_pending() &&
      !compositor_.presentation_suspended() && !vt_release_requested_ &&
      !session_state_.waiting() &&
      (messages == kMaximumMessagesPerTurn ||
       payload_bytes >= kMaximumPayloadBytesPerTurn))
    buffered_work_pending_ = true;
}

void RuntimeReactor::service_disconnect() {
  if (!producer_ || gwipc_connection_get_state(producer_.get()) !=
                        GWIPC_CONNECTION_CLOSED)
    return;
  if (compositor_.presentation_pending()) {
    std::fprintf(
        stderr,
        "gwcomp: producer disconnected during a pending presentation\n");
    stopping_ = true;
    exit_status_ = 1;
    return;
  }
  std::fprintf(
      stderr,
      "gwcomp: producer disconnected, cleared surfaces=0 buffers=0\n");
  const bool coordination_was_pending = session_state_.waiting();
  session_state_.peer_disconnected();
  if (coordination_was_pending) {
    std::fprintf(
        stderr,
        "gwcomp: producer disconnected during session coordination\n");
    stopping_ = true;
    exit_status_ = 1;
  }
  compositor_.disconnect();
  producer_.reset();
  peer_validated_ = false;
  peer_role_ = GWIPC_ROLE_UNKNOWN;
  peer_profile_.reset();
  if ((options_.once && accepted_any_frame_) || stop_after_flush_)
    stopping_ = true;
}

int RuntimeReactor::run() {
  while (!stopping_) {
    PollEvents events;
    const auto poll_result = poll(events);
    if (poll_result < 0) break;
    if (poll_result == 0) continue;
    service_signals(events.signals);
    if (stopping_) continue;
    service_presentation(events.presentation);
    if (stopping_) continue;
    service_virtual_terminal();
    if (stopping_) continue;
    accept_producer(events.listener);
    service_producer_transport(events.producer);
    validate_producer();
    service_session_messages();
    std::string session_error;
    if (!stopping_ && !session_state_.check_timeout(session_error)) {
      std::fprintf(stderr, "gwcomp: session coordination failed: %s\n",
                   session_error.c_str());
      stopping_ = true;
      exit_status_ = 1;
    }
    if (stopping_) continue;
    service_contract_messages();
    service_disconnect();
  }
  return exit_status_;
}

}  // namespace glasswyrm::compositor
