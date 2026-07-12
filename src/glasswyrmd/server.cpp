#include "glasswyrmd/server.hpp"
#include "glasswyrmd/event_router.hpp"

#ifdef GW_SERVER_HAS_IPC
#include "glasswyrmd/content_presenter.hpp"
#include "glasswyrmd/lifecycle_coordinator.hpp"
#include "glasswyrmd/lifecycle_projection.hpp"
#include "glasswyrmd/runtime_bridge.hpp"
#include "glasswyrmd/synthetic_input_peer.hpp"
#include "input/input_router.hpp"
#include "input/input_state.hpp"
#include "protocol/x11/event_mask.hpp"
#include "protocol/x11/reply.hpp"
#endif

#include <algorithm>
#include <array>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <limits>
#include <map>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace glasswyrm::server {
namespace {

volatile std::sig_atomic_t stop_requested = 0;
volatile std::sig_atomic_t signal_write_descriptor = -1;

void request_stop(int) {
  const int saved_errno = errno;
  stop_requested = 1;
  if (signal_write_descriptor >= 0) {
    const std::uint8_t byte = 1;
    (void)::write(static_cast<int>(signal_write_descriptor), &byte,
                  sizeof(byte));
  }
  errno = saved_errno;
}

bool make_address(const std::string &path, sockaddr_un &address,
                  socklen_t &length) {
  if (path.size() >= sizeof(address.sun_path)) {
    std::fprintf(stderr, "glasswyrmd: socket path is too long: %s\n",
                 path.c_str());
    return false;
  }
  std::memset(&address, 0, sizeof(address));
  address.sun_family = AF_UNIX;
  std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
  length =
      static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + path.size() + 1);
  return true;
}

} // namespace

Server::Server(Options options) : options_(std::move(options)) {
  socket_path_ = options_.socket_dir + "/X" +
                 std::to_string(static_cast<unsigned int>(options_.display));
  structural_transition_handler_ = [this](
      const std::vector<StructuralTransition>& transitions) {
    std::vector<ClientConnection*> recipients;
    recipients.reserve(clients_.size());
    for (const auto& client : clients_) recipients.push_back(client.get());
    EventRouter router(state_.resources());
    for (const auto& transition : transitions)
    {
      (void)router.route_transition(transition.kind, transition.before,
                                    transition.committed, recipients);
      if (transition.kind == StructuralTransitionKind::Map && transition.before &&
          transition.committed && !transition.before->viewable &&
          transition.committed->viewable) {
        const std::array rectangles{glasswyrm::geometry::Rectangle{
            0, 0, transition.committed->width, transition.committed->height}};
        (void)router.route_expose(transition.committed->target, rectangles, recipients);
      } else if (transition.kind == StructuralTransitionKind::Configure &&
                 transition.before && transition.committed) {
        std::vector<glasswyrm::geometry::Rectangle> rectangles;
        if (transition.committed->width > transition.before->width)
          rectangles.push_back({static_cast<std::int32_t>(transition.before->width), 0,
              static_cast<std::uint32_t>(transition.committed->width - transition.before->width),
              transition.committed->height});
        if (transition.committed->height > transition.before->height)
          rectangles.push_back({0, static_cast<std::int32_t>(transition.before->height),
              std::min<std::uint16_t>(transition.before->width, transition.committed->width),
              static_cast<std::uint32_t>(transition.committed->height - transition.before->height)});
        (void)router.route_expose(transition.committed->target, rectangles, recipients);
      }
    }
  };
  expose_intent_handler_ = [this](const std::vector<ExposeIntent>& intents) {
    std::vector<ClientConnection*> recipients;
    recipients.reserve(clients_.size());
    for (const auto& client : clients_) recipients.push_back(client.get());
    EventRouter router(state_.resources());
    for (const auto& intent : intents) {
      const std::array rectangles{intent.rectangle};
      (void)router.route_expose(intent.window, rectangles, recipients);
    }
  };
}

Server::~Server() {
  for (const auto &client : clients_) {
    (void)state_.cleanup_client(client->identifier());
  }
  clients_.clear();
  close_listener();
  unlink_owned_socket();
}

bool Server::remove_stale_socket() {
  struct stat before{};
  if (::lstat(socket_path_.c_str(), &before) != 0) {
    if (errno == ENOENT) {
      return true;
    }
    std::fprintf(stderr, "glasswyrmd: cannot inspect %s: %s\n",
                 socket_path_.c_str(), std::strerror(errno));
    return false;
  }
  if (!S_ISSOCK(before.st_mode)) {
    std::fprintf(stderr, "glasswyrmd: refusing to replace non-socket path %s\n",
                 socket_path_.c_str());
    return false;
  }
  if (before.st_uid != ::geteuid()) {
    std::fprintf(stderr,
                 "glasswyrmd: refusing to remove socket owned by another "
                 "user: %s\n",
                 socket_path_.c_str());
    return false;
  }

  sockaddr_un address{};
  socklen_t address_length = 0;
  if (!make_address(socket_path_, address, address_length)) {
    return false;
  }
  const int probe =
      ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
  if (probe < 0) {
    std::fprintf(stderr, "glasswyrmd: cannot create socket probe: %s\n",
                 std::strerror(errno));
    return false;
  }
  const int result =
      ::connect(probe, reinterpret_cast<sockaddr *>(&address), address_length);
  const int connect_error = errno;
  ::close(probe);
  if (result == 0 ||
      (connect_error != ECONNREFUSED && connect_error != ENOENT)) {
    std::fprintf(stderr, "glasswyrmd: display socket is already active: %s\n",
                 socket_path_.c_str());
    return false;
  }
  if (connect_error == ENOENT) {
    return true;
  }

  struct stat after{};
  if (::lstat(socket_path_.c_str(), &after) != 0 ||
      before.st_dev != after.st_dev || before.st_ino != after.st_ino ||
      !S_ISSOCK(after.st_mode)) {
    std::fprintf(stderr,
                 "glasswyrmd: socket changed while checking staleness: %s\n",
                 socket_path_.c_str());
    return false;
  }
  if (::unlink(socket_path_.c_str()) != 0) {
    std::fprintf(stderr, "glasswyrmd: cannot remove stale socket %s: %s\n",
                 socket_path_.c_str(), std::strerror(errno));
    return false;
  }
  return true;
}

bool Server::prepare_socket_path() {
  std::error_code error;
  std::filesystem::create_directories(options_.socket_dir, error);
  if (error) {
    std::fprintf(stderr, "glasswyrmd: cannot create socket directory %s: %s\n",
                 options_.socket_dir.c_str(), error.message().c_str());
    return false;
  }
  const auto status =
      std::filesystem::symlink_status(options_.socket_dir, error);
  if (error || !std::filesystem::is_directory(status)) {
    std::fprintf(stderr,
                 "glasswyrmd: socket directory is not a directory: %s\n",
                 options_.socket_dir.c_str());
    return false;
  }
  return remove_stale_socket();
}

bool Server::open_listener() {
  if (!prepare_socket_path()) {
    return false;
  }
  sockaddr_un address{};
  socklen_t address_length = 0;
  if (!make_address(socket_path_, address, address_length)) {
    return false;
  }
  listener_ = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
  if (listener_ < 0) {
    std::fprintf(stderr, "glasswyrmd: cannot create listener: %s\n",
                 std::strerror(errno));
    return false;
  }
  if (::bind(listener_, reinterpret_cast<sockaddr *>(&address),
             address_length) != 0) {
    std::fprintf(stderr, "glasswyrmd: cannot bind %s: %s\n",
                 socket_path_.c_str(), std::strerror(errno));
    close_listener();
    return false;
  }
  struct stat status{};
  if (::lstat(socket_path_.c_str(), &status) != 0 ||
      !S_ISSOCK(status.st_mode)) {
    std::fprintf(stderr, "glasswyrmd: cannot record bound socket identity\n");
    close_listener();
    return false;
  }
  socket_device_ = status.st_dev;
  socket_inode_ = status.st_ino;
  if (::listen(listener_, 32) != 0) {
    std::fprintf(stderr, "glasswyrmd: cannot listen on %s: %s\n",
                 socket_path_.c_str(), std::strerror(errno));
    close_listener();
    unlink_owned_socket();
    return false;
  }
  return true;
}

std::optional<std::uint32_t> Server::allocate_resource_base() const {
  constexpr std::uint64_t first_base = 0x00200000U;
  constexpr std::uint64_t last_base = 0xffe00000U;
  constexpr std::uint64_t stride = 0x00200000U;
  for (std::uint64_t candidate = first_base; candidate <= last_base;
       candidate += stride) {
    const auto base = static_cast<std::uint32_t>(candidate);
    const bool in_use = pending_resource_bases_.contains(base) ||
                        std::any_of(clients_.begin(), clients_.end(),
                                    [base](const auto &client) {
                                      return client->resource_id_base() == base;
                                    });
    if (!in_use) {
      return base;
    }
  }
  return std::nullopt;
}

void Server::accept_clients() {
  for (;;) {
    const int descriptor =
        ::accept4(listener_, nullptr, nullptr, SOCK_CLOEXEC | SOCK_NONBLOCK);
    if (descriptor >= 0) {
      const auto resource_base = allocate_resource_base();
      if (!resource_base) {
        std::fprintf(stderr,
                     "glasswyrmd: client resource-ID space exhausted\n");
        ::close(descriptor);
        continue;
      }
      clients_.push_back(std::make_unique<ClientConnection>(
          descriptor, next_client_identifier_++, *resource_base, state_,
          options_.integrated(), deferred_lifecycle_handler_,
          structural_transition_handler_, drawable_damage_handler_,
          expose_intent_handler_));
      std::fprintf(
          stderr, "glasswyrmd: accepted client %llu\n",
          static_cast<unsigned long long>(next_client_identifier_ - 1));
      continue;
    }
    if (errno == EINTR) {
      continue;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      return;
    }
    std::fprintf(stderr, "glasswyrmd: accept failed: %s\n",
                 std::strerror(errno));
    return;
  }
}

void Server::remove_closed_clients() {
  std::erase_if(clients_, [this](const auto &client) {
    if (client->state() != ClientConnection::State::Closing) return false;
    if (cancel_lifecycle_handler_) {
      pending_resource_bases_.insert(client->resource_id_base());
      cancel_lifecycle_handler_(client->identifier(),
                                client->resource_id_base());
      return true;
    }
    const auto cleanup = state_.cleanup_client(client->identifier());
    if (cleanup.resources_destroyed != 0 ||
        cleanup.property_bytes_released != 0) {
      std::fprintf(stderr,
                   "glasswyrmd: client %llu: cleanup resources=%zu "
                   "property_bytes=%zu\n",
                   static_cast<unsigned long long>(client->identifier()),
                   cleanup.resources_destroyed,
                   cleanup.property_bytes_released);
    }
    return true;
  });
}

void Server::close_listener() {
  if (listener_ >= 0) {
    ::close(listener_);
    listener_ = -1;
  }
}

void Server::unlink_owned_socket() {
  if (socket_inode_ == 0) {
    return;
  }
  struct stat status{};
  if (::lstat(socket_path_.c_str(), &status) == 0 &&
      status.st_dev == socket_device_ && status.st_ino == socket_inode_ &&
      S_ISSOCK(status.st_mode)) {
    if (::unlink(socket_path_.c_str()) != 0) {
      std::fprintf(stderr, "glasswyrmd: cannot remove socket %s: %s\n",
                   socket_path_.c_str(), std::strerror(errno));
    }
  }
  socket_device_ = 0;
  socket_inode_ = 0;
}

int Server::run() {
  stop_requested = 0;
  int signal_pipe[2] = {-1, -1};
  if (::pipe2(signal_pipe, O_CLOEXEC | O_NONBLOCK) != 0) {
    std::fprintf(stderr, "glasswyrmd: cannot create signal wakeup pipe: %s\n",
                 std::strerror(errno));
    return 1;
  }
  signal_write_descriptor = signal_pipe[1];
  const auto close_signal_pipe = [&signal_pipe] {
    signal_write_descriptor = -1;
    ::close(signal_pipe[0]);
    ::close(signal_pipe[1]);
    signal_pipe[0] = -1;
    signal_pipe[1] = -1;
  };
  struct sigaction action{};
  action.sa_handler = request_stop;
  ::sigemptyset(&action.sa_mask);
  if (::sigaction(SIGINT, &action, nullptr) != 0 ||
      ::sigaction(SIGTERM, &action, nullptr) != 0) {
    std::fprintf(stderr, "glasswyrmd: cannot install signal handlers: %s\n",
                 std::strerror(errno));
    close_signal_pipe();
    return 1;
  }
  struct sigaction ignore_pipe{};
  ignore_pipe.sa_handler = SIG_IGN;
  ::sigemptyset(&ignore_pipe.sa_mask);
  (void)::sigaction(SIGPIPE, &ignore_pipe, nullptr);

#ifdef GW_SERVER_HAS_IPC
  std::unique_ptr<RuntimeBridge> bridge;
  std::unique_ptr<LifecycleCoordinator> lifecycle;
  std::unique_ptr<ContentPresenter> content_presenter;
  std::unique_ptr<SyntheticInputPeer> input_peer;
  glasswyrm::input::InputState input_state;
  std::uint64_t expected_input_id = 1;
  struct PendingFocusInput {
    SyntheticInputRecord record;
    std::uint32_t old_focus{};
    std::size_t delivered{};
  };
  std::optional<PendingFocusInput> pending_focus_input;
  std::uint64_t next_lifecycle_token = 1;
  // Runtime bootstrap owns commit/generation 1.
  std::uint64_t next_policy_commit = 2;
  std::uint64_t next_policy_generation = 2;
  std::uint64_t next_compositor_commit = 2;
  std::uint64_t next_compositor_generation = 2;
  std::uint64_t policy_commit = 0;
  std::uint64_t policy_generation = 0;
  std::optional<StructuralEventState> transition_before;
  bool content_replay_attempted = false;
  struct PendingMutation {
    std::optional<WindowCreateSpec> create;
    ClientId owner{};
    std::uint32_t resource_base{}, resource_mask{};
    std::uint64_t creation_serial{};
    std::optional<WindowDestroyPlan> destroy;
    std::optional<ClientCleanupPlan> cleanup;
  };
  std::map<std::uint64_t, PendingMutation> pending_mutations;
  if (options_.integrated()) {
    bridge = std::make_unique<RuntimeBridge>(
        *options_.wm_socket, *options_.compositor_socket, state_.screen(),
        std::chrono::seconds(10), options_.software_content);
    if (options_.software_content) {
      content_presenter = std::make_unique<ContentPresenter>();
      drawable_damage_handler_ = [&](const std::vector<DrawableDamage>& damage) {
        for (const auto& item : damage)
          content_presenter->damage(item.window, item.rectangle);
      };
    }
    bridge->start();
    while (!stop_requested && !bridge->ready()) {
      pollfd bootstrap[3] = {
          {signal_pipe[0], POLLIN, 0},
          {bridge->policy_fd(), bridge->policy_events(), 0},
          {bridge->compositor_fd(), bridge->compositor_events(), 0}};
      const int count = ::poll(
          bootstrap, 3, bridge->poll_timeout_ms(RuntimeBridge::Clock::now()));
      if (count < 0 && errno == EINTR)
        continue;
      if (count < 0) {
        std::fprintf(stderr,
                     "glasswyrmd: integrated bootstrap poll failed: %s\n",
                     std::strerror(errno));
        close_signal_pipe();
        return 1;
      }
      if ((bootstrap[0].revents & POLLIN) != 0) {
        stop_requested = 1;
        break;
      }
      std::string error;
      if (!bridge->service(bootstrap[1].revents, bootstrap[2].revents,
                           RuntimeBridge::Clock::now(), error)) {
        std::fprintf(stderr, "glasswyrmd: integrated bootstrap failed: %s\n",
                     error.c_str());
        close_signal_pipe();
        return 1;
      }
    }
    if (stop_requested) {
      close_signal_pipe();
      return 0;
    }

    LifecycleCallbacks callbacks;
    callbacks.send_policy = [&](const LifecycleSnapshot& snapshot) {
      std::string error;
      policy_commit = next_policy_commit++;
      policy_generation = next_policy_generation++;
      const bool sent = bridge->submit_policy(
          project_policy(snapshot, policy_commit, policy_generation),
          error);
      if (!sent)
        std::fprintf(stderr, "glasswyrmd: policy submission failed: %s\n",
                     error.c_str());
      return sent;
    };
    callbacks.send_compositor = [&](const LifecycleSnapshot& snapshot) {
      std::string error;
      auto submission = project_compositor(
          snapshot, next_compositor_commit++, next_compositor_generation++,
          options_.software_content);
      if (content_presenter && !content_presenter->prepare_lifecycle(
                                   snapshot, state_.resources(), submission))
        return false;
      if (bridge->submit_compositor(submission, error)) return true;
      if (content_presenter) content_presenter->reject_lifecycle();
      return false;
    };
    callbacks.commit = [&](const LifecycleSnapshot& snapshot) {
      transition_before.reset();
      const auto* active = lifecycle ? lifecycle->active() : nullptr;
      if (active) {
        EventRouter router(state_.resources());
        transition_before = router.capture(active->window);
      }
      const auto mutation = active ? pending_mutations.find(active->token)
                                   : pending_mutations.end();
      bool committed = false;
      if (active && mutation != pending_mutations.end() &&
          mutation->second.create) {
        const auto& pending = mutation->second;
        committed = state_.commit_create_lifecycle(
            pending.owner, pending.resource_base, pending.resource_mask,
            *pending.create, pending.creation_serial, snapshot);
      } else if (active && mutation != pending_mutations.end() &&
                 mutation->second.destroy) {
        auto staged = state_;
        committed = staged.resources().commit_destroy_plan(
                        *mutation->second.destroy) ==
                    DestroyWindowStatus::Success;
        committed = committed && staged.commit_lifecycle(snapshot);
        if (committed) state_ = std::move(staged);
      } else if (active && mutation != pending_mutations.end() &&
                 mutation->second.cleanup) {
        auto staged = state_;
        (void)staged.resources().commit_client_cleanup(
            *mutation->second.cleanup);
        committed = staged.commit_lifecycle(snapshot);
        if (committed) state_ = std::move(staged);
      } else {
        committed = state_.commit_lifecycle(snapshot);
      }
      if (!committed) {
        if (content_presenter) content_presenter->reject_lifecycle();
        return false;
      }
      if (content_presenter)
        content_presenter->accept_lifecycle(snapshot, state_.resources());
      return true;
    };
    callbacks.complete = [&](const std::uint64_t token, const bool success) {
      // Tokens are globally unique, so inspect every blocked connection rather
      // than coupling token values to client identifiers.
      ClientConnection* requester = nullptr;
      for (const auto& client : clients_)
        if (client->clear_dispatch_blocked(token)) requester = client.get();
      const auto* operation = lifecycle ? lifecycle->active() : nullptr;
      const auto mutation = pending_mutations.find(token);
      const auto cleanup_base =
          mutation != pending_mutations.end() && mutation->second.cleanup
              ? std::optional<std::uint32_t>(mutation->second.resource_base)
              : std::nullopt;
      if (success && operation) {
        std::vector<ClientConnection*> recipients;
        recipients.reserve(clients_.size());
        for (const auto& client : clients_) recipients.push_back(client.get());
        EventRouter router(state_.resources());
        if (operation->kind == LifecycleOperationKind::Destroy &&
            mutation != pending_mutations.end() && mutation->second.destroy) {
          for (const auto& item : mutation->second.destroy->postorder) {
            StructuralEventState before{};
            before.target = item.xid;
            before.parent = item.parent;
            before.structure_recipients = item.structure_recipients;
            before.substructure_recipients = item.substructure_recipients;
            (void)router.route_transition(StructuralTransitionKind::Destroy,
                                          before, std::nullopt, recipients);
          }
        } else if (operation->kind == LifecycleOperationKind::ClientCleanup &&
            mutation != pending_mutations.end() && mutation->second.cleanup) {
          for (const auto& item : mutation->second.cleanup->postorder) {
            StructuralEventState before{};
            before.target = item.xid;
            before.parent = item.parent;
            before.structure_recipients = item.structure_recipients;
            before.substructure_recipients = item.substructure_recipients;
            (void)router.route_transition(StructuralTransitionKind::Destroy,
                                          before, std::nullopt, recipients);
          }
        } else {
        const auto committed = router.capture(operation->window);
        if (operation->kind == LifecycleOperationKind::Create) {
          // M6 does not expose CreateNotify.
        } else {
        const auto kind = operation->kind == LifecycleOperationKind::Map
                              ? StructuralTransitionKind::Map
                          : operation->kind == LifecycleOperationKind::Unmap
                              ? StructuralTransitionKind::Unmap
                          : operation->kind == LifecycleOperationKind::Destroy
                              ? StructuralTransitionKind::Destroy
                              : StructuralTransitionKind::Configure;
        (void)router.route_transition(kind, transition_before, committed,
                                      recipients);
        if (committed && operation->kind == LifecycleOperationKind::Map &&
            (!transition_before || !transition_before->viewable) &&
            committed->viewable) {
          const std::array rectangles{geometry::Rectangle{
              0, 0, committed->width, committed->height}};
          (void)router.route_expose(operation->window, rectangles, recipients);
        } else if (committed && transition_before &&
                   operation->kind == LifecycleOperationKind::Configure) {
          std::vector<geometry::Rectangle> rectangles;
          if (committed->width > transition_before->width)
            rectangles.push_back(
                {static_cast<std::int32_t>(transition_before->width), 0,
                 static_cast<std::uint32_t>(committed->width -
                                            transition_before->width),
                 committed->height});
          if (committed->height > transition_before->height)
            rectangles.push_back(
                {0, static_cast<std::int32_t>(transition_before->height),
                 std::min<std::uint16_t>(transition_before->width,
                                         committed->width),
                 static_cast<std::uint32_t>(committed->height -
                                            transition_before->height)});
          (void)router.route_expose(operation->window, rectangles, recipients);
        }
        }
        }
      }
      if (operation && operation->kind == LifecycleOperationKind::Focus &&
          pending_focus_input) {
        const auto new_focus = state_.focused_window();
        if (success) {
          std::vector<ClientConnection*> recipients;
          recipients.reserve(clients_.size());
          for (const auto& client : clients_) recipients.push_back(client.get());
          EventRouter router(state_.resources());
          pending_focus_input->delivered += router.route_focus(
              pending_focus_input->old_focus, new_focus, recipients);
        }
        if (input_peer && input_peer->connected()) {
          gwipc_synthetic_input_acknowledged acknowledgement{};
          acknowledgement.struct_size = sizeof(acknowledgement);
          acknowledgement.input_id = pending_focus_input->record.input_id;
          acknowledgement.time_ms = input_state.time();
          acknowledgement.result = success
              ? GWIPC_SYNTHETIC_INPUT_ACCEPTED
              : GWIPC_SYNTHETIC_INPUT_FOCUS_REJECTED;
          acknowledgement.root_x = input_state.pointer_x();
          acknowledgement.root_y = input_state.pointer_y();
          acknowledgement.pointer_window = input_state.pointer_target();
          acknowledgement.focus_window = new_focus;
          acknowledgement.state = input_state.mask();
          acknowledgement.delivered_event_count =
              static_cast<std::uint32_t>(pending_focus_input->delivered);
          if (!input_peer->acknowledge(pending_focus_input->record,
                                       acknowledgement))
            input_peer->disconnect();
        }
        pending_focus_input.reset();
      }
      (void)requester;
      transition_before.reset();
      pending_mutations.erase(token);
      if (cleanup_base) pending_resource_bases_.erase(*cleanup_base);
      bridge->clear_transaction_result();
    };
    callbacks.fatal = [&] { stop_requested = 1; };
    callbacks.rebase = rebase_lifecycle_operation;
    callbacks.prepare_rollback = [&] { return bridge->prepare_rollback(); };
    lifecycle = std::make_unique<LifecycleCoordinator>(
        state_.lifecycle_snapshot(), 1024, std::move(callbacks));
    deferred_lifecycle_handler_ = [&](ClientConnection& client,
                                      const DispatchResult& result) {
      auto proposed = lifecycle->committed();
      const auto serial = state_.next_lifecycle_serial();
      if (!serial) return false;
      LifecycleOperation operation;
      operation.token = next_lifecycle_token++;
      operation.client_id = client.identifier();
      operation.request_sequence = client.last_request_sequence();
      operation.window = result.deferred_window;
      PendingMutation mutation;
      if (result.deferred_create) {
        operation.kind = LifecycleOperationKind::Create;
        auto create = state_.propose_create_lifecycle(
            client.identifier(), client.resource_id_base(),
            state_.screen().resource_id_mask, *result.deferred_create, *serial);
        if (!create) return false;
        proposed = std::move(*create);
        mutation.create = result.deferred_create;
        mutation.owner = client.identifier();
        mutation.resource_base = client.resource_id_base();
        mutation.resource_mask = state_.screen().resource_id_mask;
        mutation.creation_serial = *serial;
      } else if (result.deferred_destroy) {
        operation.kind = LifecycleOperationKind::Destroy;
        auto destroy = state_.propose_destroy_lifecycle(result.deferred_window);
        auto plan = state_.resources().capture_destroy_plan(
            result.deferred_window);
        if (!destroy || !plan) return false;
        proposed = std::move(*destroy);
        mutation.destroy = std::move(*plan);
      } else {
      auto found = proposed.windows.find(result.deferred_window);
      if (found == proposed.windows.end()) return false;
      if (result.deferred_override_redirect) {
        operation.kind = LifecycleOperationKind::OverrideChange;
        found->second.override_redirect = *result.deferred_override_redirect;
      } else if (result.deferred_configure) {
        operation.kind = LifecycleOperationKind::Configure;
        const auto& request = *result.deferred_configure;
        if (request.x) found->second.requested_x = *request.x;
        if (request.y) found->second.requested_y = *request.y;
        if (request.width) found->second.requested_width = *request.width;
        if (request.height) found->second.requested_height = *request.height;
        if (request.border_width)
          found->second.requested_border_width = *request.border_width;
        found->second.geometry_serial = *serial;
        found->second.stack_sibling = request.sibling.value_or(0);
        found->second.stack_mode =
            request.stack_mode == gw::protocol::x11::CoreStackMode::Above
                ? LifecycleStackMode::Above
                : request.stack_mode == gw::protocol::x11::CoreStackMode::Below
                      ? LifecycleStackMode::Below
                      : LifecycleStackMode::None;
        if (request.stack_mode) found->second.stack_serial = *serial;
      } else {
        operation.kind = result.deferred_map ? LifecycleOperationKind::Map
                                             : LifecycleOperationKind::Unmap;
        found->second.map_requested = result.deferred_map;
        found->second.map_serial = *serial;
      }
      }
      operation.proposed = std::move(proposed);
      const auto token = operation.token;
      pending_mutations.emplace(token, std::move(mutation));
      const auto status = content_presenter &&
                                  content_presenter->frame_in_flight()
                              ? lifecycle->enqueue_paused(std::move(operation))
                              : lifecycle->enqueue(std::move(operation));
      if (status == EnqueueStatus::Queued) {
        client.set_dispatch_blocked(token);
        return true;
      }
      pending_mutations.erase(token);
      if (status == EnqueueStatus::Full) {
        return client.enqueue_server_packet(gw::protocol::x11::encode_core_error(
            client.byte_order(),
            {gw::protocol::x11::CoreErrorCode::BadAlloc,
             client.last_request_sequence(), 0,
             static_cast<std::uint8_t>(result.deferred_configure
                                           ? gw::protocol::x11::CoreOpcode::ConfigureWindow
                                           : result.deferred_override_redirect
                                                 ? gw::protocol::x11::CoreOpcode::ChangeWindowAttributes
                                           : result.deferred_map
                                                 ? gw::protocol::x11::CoreOpcode::MapWindow
                                                 : gw::protocol::x11::CoreOpcode::UnmapWindow),
             0}));
      }
      std::fprintf(stderr,
                   "glasswyrmd: lifecycle enqueue failed token=%llu status=%u phase=%u\n",
                   static_cast<unsigned long long>(token),
                   static_cast<unsigned>(status),
                   static_cast<unsigned>(lifecycle->phase()));
      return false;
    };
    cancel_lifecycle_handler_ = [&](const std::uint64_t client,
                                    const std::uint32_t resource_base) {
      lifecycle->cancel_client(client);
      auto plan = state_.resources().prepare_client_cleanup(client);
      if (!plan.affects_policy) {
        (void)state_.resources().commit_client_cleanup(plan);
        pending_resource_bases_.erase(resource_base);
        return;
      }
      auto proposed = lifecycle->committed();
      for (const auto& item : plan.postorder) {
        proposed.windows.erase(item.xid);
        std::erase(proposed.root_order, item.xid);
        if (proposed.focused_window == item.xid)
          proposed.focused_window = proposed.root_window;
      }
      LifecycleOperation operation;
      operation.token = next_lifecycle_token++;
      operation.client_id = client;
      operation.kind = LifecycleOperationKind::ClientCleanup;
      operation.window = plan.roots.empty() ? 0 : plan.roots.front();
      operation.proposed = std::move(proposed);
      const auto token = operation.token;
      PendingMutation mutation;
      mutation.resource_base = resource_base;
      mutation.cleanup = std::move(plan);
      pending_mutations.emplace(token, std::move(mutation));
      const auto cleanup_status =
          content_presenter && content_presenter->frame_in_flight()
              ? lifecycle->enqueue_priority_paused(std::move(operation))
              : lifecycle->enqueue_priority(std::move(operation));
      if (cleanup_status != EnqueueStatus::Queued) {
        std::fprintf(stderr,
                     "glasswyrmd: could not queue coordinated client cleanup\n");
        stop_requested = 1;
      }
    };
    if (options_.synthetic_input_socket) {
      input_peer = std::make_unique<SyntheticInputPeer>(
          *options_.synthetic_input_socket);
      std::string error;
      if (!input_peer->start(error)) {
        std::fprintf(stderr, "glasswyrmd: %s\n", error.c_str());
        close_signal_pipe();
        return 1;
      }
    }
  }
#else
  if (options_.integrated()) {
    std::fprintf(stderr,
                 "glasswyrmd: integrated mode requires libgwipc support\n");
    close_signal_pipe();
    return 1;
  }
#endif

  if (!open_listener()) {
    close_signal_pipe();
    return 1;
  }
  std::fprintf(stderr, "glasswyrmd: listening on %s\n", socket_path_.c_str());

  while (!stop_requested) {
    std::vector<pollfd> descriptors;
    descriptors.reserve(clients_.size() + 6);
    short listener_events = POLLIN;
#ifdef GW_SERVER_HAS_IPC
    if (bridge && !bridge->ready())
      listener_events = 0;
#endif
    descriptors.push_back(pollfd{listener_, listener_events, 0});
    descriptors.push_back(pollfd{signal_pipe[0], POLLIN, 0});
#ifdef GW_SERVER_HAS_IPC
    std::optional<std::size_t> input_listener_index;
    std::optional<std::size_t> input_connection_index;
    if (bridge) {
      descriptors.push_back(
          pollfd{bridge->policy_fd(), bridge->policy_events(), 0});
      descriptors.push_back(
          pollfd{bridge->compositor_fd(), bridge->compositor_events(), 0});
    }
    if (input_peer) {
      input_listener_index = descriptors.size();
      descriptors.push_back(pollfd{input_peer->listener_fd(),
                                   input_peer->listener_events(), 0});
      input_connection_index = descriptors.size();
      descriptors.push_back(pollfd{input_peer->connection_fd(),
                                   input_peer->connection_events(), 0});
    }
#endif
    const std::size_t client_offset = descriptors.size();
    for (const auto &client : clients_) {
      descriptors.push_back(
          pollfd{client->descriptor(), client->poll_events(), 0});
    }
    const bool pending_work =
        std::any_of(clients_.begin(), clients_.end(),
                    [](const auto &client) { return client->needs_service(); });
    int poll_timeout = pending_work ? 0 : -1;
#ifdef GW_SERVER_HAS_IPC
    if (bridge && !bridge->ready())
      poll_timeout = bridge->poll_timeout_ms(RuntimeBridge::Clock::now());
#endif
    const int result =
        ::poll(descriptors.data(), descriptors.size(), poll_timeout);
    if (result < 0) {
      if (errno == EINTR) {
        continue;
      }
      std::fprintf(stderr, "glasswyrmd: poll failed: %s\n",
                   std::strerror(errno));
      close_signal_pipe();
      return 1;
    }
    const std::size_t polled_client_count = clients_.size();
    for (std::size_t index = 0; index < polled_client_count; ++index) {
      clients_[index]->handle_events(
          descriptors[index + client_offset].revents);
    }
#ifdef GW_SERVER_HAS_IPC
    if (bridge) {
      std::string error;
      if (!bridge->service(descriptors[2].revents, descriptors[3].revents,
                           RuntimeBridge::Clock::now(), error)) {
        std::fprintf(stderr, "glasswyrmd: integrated runtime failed: %s\n",
                     error.c_str());
        close_signal_pipe();
        return 1;
      }
      std::vector<CompositorBufferRelease> compositor_releases;
      if (content_presenter) {
        if (bridge->take_compositor_reset())
          content_presenter->peer_disconnected();
        compositor_releases = bridge->take_buffer_releases();
        if (bridge->content_rejected_ready()) {
          content_presenter->reject_content();
          bridge->clear_transaction_result();
          if (content_replay_attempted) {
            std::fprintf(stderr,
                         "glasswyrmd: repeated compositor content rejection\n");
            close_signal_pipe();
            return 1;
          }
          content_replay_attempted = true;
          auto replay = project_compositor(
              lifecycle->committed(), next_compositor_commit++,
              next_compositor_generation++, true);
          if (!content_presenter->prepare_replay(
                  lifecycle->committed(), state_.resources(), replay) ||
              !bridge->submit_replay(replay, error)) {
            content_presenter->reject_lifecycle();
            std::fprintf(stderr,
                         "glasswyrmd: compositor content replay failed: %s\n",
                         error.c_str());
            close_signal_pipe();
            return 1;
          }
        } else if (bridge->content_result_ready()) {
          content_presenter->accept_content();
          content_replay_attempted = false;
          bridge->clear_transaction_result();
          if (lifecycle && lifecycle->pending_count() != 0 &&
              !lifecycle->resume()) {
            std::fprintf(stderr,
                         "glasswyrmd: could not resume deferred lifecycle\n");
            close_signal_pipe();
            return 1;
          }
        }
        if (bridge->replay_rejected_ready()) {
          content_presenter->reject_lifecycle();
          std::fprintf(stderr,
                       "glasswyrmd: compositor rejected full content replay\n");
          close_signal_pipe();
          return 1;
        }
        if (bridge->replay_result_ready()) {
          content_presenter->accept_lifecycle(lifecycle->committed(),
                                              state_.resources());
          bridge->clear_transaction_result();
          content_replay_attempted = false;
          if (lifecycle->pending_count() != 0 && !lifecycle->resume()) {
            std::fprintf(stderr,
                         "glasswyrmd: could not resume lifecycle after replay\n");
            close_signal_pipe();
            return 1;
          }
        }
      }
      if (lifecycle && bridge->policy_result_ready()) {
        const auto* active = lifecycle->active();
        auto evaluated = active
                             ? apply_policy_result(active->proposed,
                                                   bridge->policy_result())
                             : std::nullopt;
        if (!evaluated) {
          bridge->clear_transaction_result();
          if (!lifecycle->policy_rejected()) {
            std::fprintf(stderr,
                         "glasswyrmd: invalid policy lifecycle result\n");
            close_signal_pipe();
            return 1;
          }
        } else if (!lifecycle->policy_accepted(std::move(*evaluated))) {
          std::fprintf(stderr,
                       "glasswyrmd: policy lifecycle transition failed\n");
          close_signal_pipe();
          return 1;
        }
      }
      if (lifecycle && bridge->policy_rejected_ready() &&
          !lifecycle->policy_rejected()) {
        std::fprintf(stderr,
                     "glasswyrmd: policy rejection transition failed\n");
        close_signal_pipe();
        return 1;
      }
      if (lifecycle && bridge->compositor_rejected_ready()) {
        if (content_presenter) content_presenter->reject_lifecycle();
        if (!lifecycle->compositor_rejected()) {
          std::fprintf(stderr,
                       "glasswyrmd: compositor rollback transition failed\n");
          close_signal_pipe();
          return 1;
        }
      }
      if (lifecycle && bridge->compositor_result_ready()) {
        const bool rollback = lifecycle->phase() ==
                              CoordinatorPhase::RollingBackCompositor;
        if (!lifecycle->compositor_accepted()) {
          std::fprintf(stderr,
                       "glasswyrmd: compositor lifecycle transition failed\n");
          close_signal_pipe();
          return 1;
        }
        if (rollback && content_presenter)
          content_presenter->accept_lifecycle(lifecycle->committed(),
                                              state_.resources());
      }
      if (content_presenter) {
        for (const auto& release : compositor_releases) {
          if (!content_presenter->release(release.buffer_id, release.reason)) {
            std::fprintf(stderr,
                         "glasswyrmd: invalid compositor buffer release id=%llu reason=%u\n",
                         static_cast<unsigned long long>(release.buffer_id),
                         static_cast<unsigned>(release.reason));
            close_signal_pipe();
            return 1;
          }
          std::fprintf(stderr,
                       "glasswyrmd: published buffer released buffer=%llu reason=%u\n",
                       static_cast<unsigned long long>(release.buffer_id),
                       static_cast<unsigned>(release.reason));
        }
      }
      if (content_presenter && lifecycle &&
          lifecycle->phase() == CoordinatorPhase::Idle &&
          bridge->transaction_idle() &&
          !content_presenter->frame_in_flight() &&
          content_presenter->has_pending_damage()) {
        CompositorContentSubmission content;
        if (content_presenter->prepare_content(
                lifecycle->committed(), state_.resources(),
                next_compositor_commit++, next_compositor_generation++,
                content)) {
          if (!bridge->submit_content(content, error)) {
            content_presenter->reject_content();
            std::fprintf(stderr,
                         "glasswyrmd: content submission failed: %s\n",
                         error.c_str());
          }
        }
      }
    }
    if (input_peer) {
      if (input_listener_index &&
          (descriptors[*input_listener_index].revents & POLLIN) != 0)
        input_peer->accept_provider();
      if (input_connection_index)
        input_peer->service(descriptors[*input_connection_index].revents);
      if (input_peer->consume_disconnect()) {
        input_state.reset_provider_state();
        expected_input_id = 1;
        std::fprintf(stderr,
                     "glasswyrmd: input provider state cleared\n");
      }
      const auto acknowledge = [&](const SyntheticInputRecord& record,
                                   const gwipc_synthetic_input_result result,
                                   const std::size_t delivered) {
        gwipc_synthetic_input_acknowledged value{};
        value.struct_size = sizeof(value);
        value.input_id = record.input_id;
        value.time_ms = input_state.time();
        value.result = result;
        value.root_x = input_state.pointer_x();
        value.root_y = input_state.pointer_y();
        value.pointer_window = input_state.pointer_target();
        value.focus_window = state_.focused_window();
        value.state = input_state.mask();
        value.delivered_event_count = static_cast<std::uint32_t>(delivered);
        if (!input_peer->acknowledge(record, value)) input_peer->disconnect();
      };
      std::size_t processed = 0;
      while (!pending_focus_input && processed++ < 64) {
        const auto next = input_peer->take_record();
        if (!next) break;
        const auto record = *next;
        if (record.input_id != expected_input_id) {
          input_peer->disconnect();
          break;
        }
        ++expected_input_id;
        std::vector<ClientConnection*> recipients;
        recipients.reserve(clients_.size());
        for (const auto& client : clients_) recipients.push_back(client.get());
        EventRouter router(state_.resources());
        std::size_t delivered = 0;
        if (record.kind == SyntheticInputRecord::Kind::Barrier) {
          acknowledge(record, GWIPC_SYNTHETIC_INPUT_ACCEPTED, 0);
          continue;
        }
        if (record.time_ms == 0 || record.time_ms < input_state.time()) {
          input_peer->disconnect();
          break;
        }
        if (record.kind == SyntheticInputRecord::Kind::Motion) {
          const auto [x, y] = glasswyrm::input::clamp_pointer(
              record.root_x, record.root_y, state_.screen().width_pixels,
              state_.screen().height_pixels);
          const auto old_target = input_state.pointer_target();
          const auto new_target = glasswyrm::input::hit_test_top_level(
              state_.resources(), x, y);
          (void)input_state.accept_time(record.time_ms);
          input_state.set_pointer(x, y, new_target);
          delivered += router.route_crossing(old_target, new_target,
                                              state_.focused_window(),
                                              input_state, recipients);
          delivered += router.route_input(
              gw::protocol::x11::CoreEventType::MotionNotify, 0,
              input_state.time(), new_target, input_state.mask(),
              glasswyrm::input::motion_delivery_mask(input_state), x, y,
              new_target, recipients);
          acknowledge(record,
                      x != record.root_x || y != record.root_y
                          ? GWIPC_SYNTHETIC_INPUT_CLAMPED
                          : GWIPC_SYNTHETIC_INPUT_ACCEPTED,
                      delivered);
          continue;
        }
        auto staged = input_state;
        const auto pre_state = input_state.mask();
        if (record.kind == SyntheticInputRecord::Kind::Button) {
          if (staged.transition_button(record.detail, record.pressed) !=
              glasswyrm::input::TransitionStatus::Accepted) {
            acknowledge(record, GWIPC_SYNTHETIC_INPUT_INVALID_TRANSITION, 0);
            continue;
          }
          (void)staged.accept_time(record.time_ms);
          delivered += router.route_input(
              record.pressed ? gw::protocol::x11::CoreEventType::ButtonPress
                             : gw::protocol::x11::CoreEventType::ButtonRelease,
              record.detail, record.time_ms, input_state.pointer_target(),
              pre_state,
              record.pressed ? gw::protocol::x11::event_mask::ButtonPress
                             : gw::protocol::x11::event_mask::ButtonRelease,
              input_state.pointer_x(), input_state.pointer_y(),
              input_state.pointer_target(), recipients);
          input_state = staged;
          const auto target = input_state.pointer_target();
          const auto* window = state_.resources().find_window(target);
          const bool requests_focus =
              record.pressed && record.detail == 1 && window &&
              state_.resources().is_policy_candidate(target) &&
              window->map_state == MapState::Viewable &&
              window->policy_visible && !window->cleanup_pending &&
              !window->attributes.override_redirect &&
              state_.focused_window() != target;
          if (requests_focus) {
            const auto serial = state_.next_lifecycle_serial();
            auto proposed = lifecycle->committed();
            auto found = proposed.windows.find(target);
            if (!serial || found == proposed.windows.end()) {
              acknowledge(record, GWIPC_SYNTHETIC_INPUT_FOCUS_UNCHANGED,
                          delivered);
              continue;
            }
            found->second.focus_serial = *serial;
            LifecycleOperation operation;
            operation.token = next_lifecycle_token++;
            operation.kind = LifecycleOperationKind::Focus;
            operation.window = target;
            operation.proposed = std::move(proposed);
            pending_focus_input = PendingFocusInput{
                record, state_.focused_window(), delivered};
            if (lifecycle->enqueue(std::move(operation)) !=
                EnqueueStatus::Queued) {
              pending_focus_input.reset();
              acknowledge(record, GWIPC_SYNTHETIC_INPUT_FOCUS_REJECTED,
                          delivered);
            }
            continue;
          }
          acknowledge(record,
                      record.pressed && record.detail == 1
                          ? GWIPC_SYNTHETIC_INPUT_FOCUS_UNCHANGED
                          : GWIPC_SYNTHETIC_INPUT_ACCEPTED,
                      delivered);
          continue;
        }
        if (staged.transition_key(record.detail, record.pressed) !=
            glasswyrm::input::TransitionStatus::Accepted) {
          acknowledge(record, GWIPC_SYNTHETIC_INPUT_INVALID_TRANSITION, 0);
          continue;
        }
        (void)staged.accept_time(record.time_ms);
        const auto focus = state_.resources().find_window(state_.focused_window())
                               ? state_.focused_window()
                               : state_.screen().root_window;
        delivered += router.route_input(
            record.pressed ? gw::protocol::x11::CoreEventType::KeyPress
                           : gw::protocol::x11::CoreEventType::KeyRelease,
            record.detail, record.time_ms, focus, pre_state,
            record.pressed ? gw::protocol::x11::event_mask::KeyPress
                           : gw::protocol::x11::event_mask::KeyRelease,
            input_state.pointer_x(), input_state.pointer_y(),
            input_state.pointer_target(), recipients);
        input_state = staged;
        acknowledge(record, GWIPC_SYNTHETIC_INPUT_ACCEPTED, delivered);
      }
    }
#endif
    remove_closed_clients();
    bool accepts_ready = true;
#ifdef GW_SERVER_HAS_IPC
    accepts_ready = !bridge || bridge->ready();
#endif
    if (accepts_ready && (descriptors.front().revents & POLLIN) != 0) {
      accept_clients();
    }
  }

  clients_.clear();
  deferred_lifecycle_handler_ = {};
  cancel_lifecycle_handler_ = {};
  close_signal_pipe();
  close_listener();
  unlink_owned_socket();
  std::fprintf(stderr, "glasswyrmd: stopped\n");
  return 0;
}

} // namespace glasswyrm::server
