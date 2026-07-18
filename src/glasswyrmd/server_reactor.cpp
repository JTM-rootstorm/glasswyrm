#include "glasswyrmd/server_runtime.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <poll.h>

namespace glasswyrm::server {

int Server::run() { return ServerRuntime(*this).run(); }

bool ServerRuntime::initialize_trace() {
  if (!server_.options_.x11_trace) return true;
  std::string error;
  server_.trace_ = CompatibilityTrace::create(*server_.options_.x11_trace,
                                               error);
  if (!server_.trace_) {
    std::fprintf(stderr, "glasswyrmd: cannot initialize X11 trace %s: %s\n",
                 server_.options_.x11_trace->c_str(), error.c_str());
    return false;
  }
  return true;
}

int ServerRuntime::run() {
  if (!initialize_trace()) return 1;
  SignalRuntime signals;
  if (!signals.start()) return 1;
  const int integrated_status = initialize_integrated(signals);
  if (integrated_status != 0) {
    signals.close();
    return integrated_status == 2 ? 0 : 1;
  }
  if (!server_.open_listener()) {
    signals.close();
    return 1;
  }
  std::fprintf(stderr, "glasswyrmd: listening on %s\n",
               server_.socket_path_.c_str());
  const int result = event_loop(signals);
  if (result != 0) {
    signals.close();
    return result;
  }
  shutdown(signals);
  return 0;
}

int ServerRuntime::event_loop(SignalRuntime& signals) {
  while (!SignalRuntime::stop_requested()) {
    std::vector<pollfd> descriptors;
    descriptors.reserve(server_.clients_.size() + 8);
    short listener_events = POLLIN;
#ifdef GW_SERVER_HAS_IPC
    if (bridge_ && !bridge_->ready()) listener_events = 0;
#endif
    descriptors.push_back(pollfd{server_.listener_, listener_events, 0});
    descriptors.push_back(pollfd{signals.read_descriptor(), POLLIN, 0});
#ifdef GW_SERVER_HAS_IPC
    std::optional<std::size_t> policy_index;
    std::optional<std::size_t> compositor_index;
    std::optional<std::size_t> input_listener_index;
    std::optional<std::size_t> input_connection_index;
    std::vector<OutputControlPollDescriptor> output_control_descriptors;
    std::size_t output_control_offset{};
#if GW_HAS_LIBINPUT_BACKEND
    std::optional<std::size_t> real_input_index;
    std::optional<std::size_t> repeat_index;
#endif
    if (bridge_) {
      policy_index = descriptors.size();
      descriptors.push_back(
          pollfd{bridge_->policy_fd(), bridge_->policy_events(), 0});
      compositor_index = descriptors.size();
      descriptors.push_back(
          pollfd{bridge_->compositor_fd(), bridge_->compositor_events(), 0});
    }
    if (input_peer_) {
      input_listener_index = descriptors.size();
      descriptors.push_back(pollfd{
          input_peer_->listener_fd(),
          static_cast<short>((!bridge_ || bridge_->ready())
                                 ? input_peer_->listener_events()
                                 : 0),
          0});
      input_connection_index = descriptors.size();
      descriptors.push_back(pollfd{input_peer_->connection_fd(),
                                   input_peer_->connection_events(), 0});
    }
    if (output_control_peer_) {
      output_control_descriptors = output_control_peer_->poll_descriptors();
      output_control_offset = descriptors.size();
      for (const auto &entry : output_control_descriptors)
        descriptors.push_back(pollfd{entry.descriptor, entry.events, 0});
    }
#if GW_HAS_LIBINPUT_BACKEND
    if (real_input_) {
      real_input_index = descriptors.size();
      descriptors.push_back(
          pollfd{real_input_->input_fd(),
                 static_cast<short>(real_input_->active() ? POLLIN : 0), 0});
      repeat_index = descriptors.size();
      descriptors.push_back(pollfd{real_input_->repeat_fd(), POLLIN, 0});
    }
#endif
#endif
    const std::size_t client_offset = descriptors.size();
    for (const auto& client : server_.clients_) {
      descriptors.push_back(
          pollfd{client->descriptor(), client->poll_events(), 0});
    }
    bool pending_work = std::any_of(
        server_.clients_.begin(), server_.clients_.end(),
        [](const auto& client) { return client->needs_service(); });
#ifdef GW_SERVER_HAS_IPC
    pending_work = pending_work ||
                   (input_peer_ && input_peer_->has_records() &&
                    !pending_focus_input_);
#if GW_HAS_LIBINPUT_BACKEND
    pending_work = pending_work ||
                   (real_input_ &&
                    (real_input_->has_events() ||
                     real_input_->backend_work_pending()) &&
                    !pending_real_focus_) ||
                   (cursor_presenter_ && cursor_dirty_);
#endif
#endif
    int poll_timeout = pending_work ? 0 : -1;
#ifdef GW_SERVER_HAS_IPC
    if (bridge_ && !bridge_->ready())
      poll_timeout = bridge_->poll_timeout_ms(RuntimeBridge::Clock::now());
#endif
    const int result =
        ::poll(descriptors.data(), descriptors.size(), poll_timeout);
    if (result < 0) {
      if (errno == EINTR) continue;
      std::fprintf(stderr, "glasswyrmd: poll failed: %s\n",
                   std::strerror(errno));
      return 1;
    }
    const std::size_t polled_client_count = server_.clients_.size();
    for (std::size_t index = 0; index < polled_client_count; ++index) {
#if GW_HAS_LIBINPUT_BACKEND
      const auto request_sequence =
          server_.clients_[index]->last_request_sequence();
#endif
      server_.clients_[index]->handle_events(
          descriptors[index + client_offset].revents);
#if GW_HAS_LIBINPUT_BACKEND
      if (cursor_presenter_ &&
          server_.clients_[index]->last_request_sequence() != request_sequence)
        mark_cursor_dirty();
#endif
    }
#ifdef GW_SERVER_HAS_IPC
    if (output_control_peer_) {
      for (std::size_t index = 0; index < output_control_descriptors.size();
           ++index)
        output_control_descriptors[index].revents =
            descriptors[output_control_offset + index].revents;
      output_control_peer_->service(output_control_descriptors);
    }
    if (bridge_ &&
        !service_integrated(descriptors[*policy_index].revents,
                            descriptors[*compositor_index].revents))
      return 1;
    if (input_peer_)
      service_input(descriptors[*input_listener_index].revents,
                    descriptors[*input_connection_index].revents);
#if GW_HAS_LIBINPUT_BACKEND
    if (real_input_ &&
        !service_real_input(descriptors[*real_input_index].revents,
                            descriptors[*repeat_index].revents))
      return 1;
#endif
#endif
#if GW_HAS_LIBINPUT_BACKEND
    const auto client_count = server_.clients_.size();
#endif
    server_.remove_closed_clients();
#if GW_HAS_LIBINPUT_BACKEND
    if (cursor_presenter_ && server_.clients_.size() != client_count)
      mark_cursor_dirty();
#endif
    bool accepts_ready = true;
#ifdef GW_SERVER_HAS_IPC
    accepts_ready = !bridge_ || bridge_->ready();
#endif
    if (accepts_ready && (descriptors.front().revents & POLLIN) != 0)
      server_.accept_clients();
  }
  return 0;
}

}  // namespace glasswyrm::server
