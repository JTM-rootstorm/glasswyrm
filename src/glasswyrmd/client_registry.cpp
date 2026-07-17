#include "glasswyrmd/server.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <sys/socket.h>
#include <unistd.h>

namespace glasswyrm::server {
namespace {

std::optional<std::uint32_t> unix_peer_uid(const int descriptor) noexcept {
  struct ucred credentials {};
  socklen_t size = sizeof(credentials);
  if (::getsockopt(descriptor, SOL_SOCKET, SO_PEERCRED, &credentials, &size) <
          0 ||
      size != sizeof(credentials))
    return std::nullopt;
  return static_cast<std::uint32_t>(credentials.uid);
}

}  // namespace

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
          expose_intent_handler_, trace_.get(), input_snapshot_provider_,
          protocol_event_handler_, &extensions_, options_.game_compat,
          unix_peer_uid(descriptor)));
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

} // namespace glasswyrm::server
