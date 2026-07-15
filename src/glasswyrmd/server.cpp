#include "glasswyrmd/server.hpp"

#include "glasswyrmd/event_router.hpp"
#include "glasswyrmd/protocol_event_router.hpp"

#include <algorithm>
#include <array>
#include <utility>

namespace glasswyrm::server {

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
        (void)router.route_viewable_subtree_expose(
            transition.committed->target, recipients);
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
  protocol_event_handler_ =
      [this](const std::vector<ProtocolEventIntent>& intents) {
        std::vector<ClientConnection*> recipients;
        recipients.reserve(clients_.size());
        for (const auto& client : clients_) recipients.push_back(client.get());
        ProtocolEventRouter router(state_.resources());
        for (const auto& intent : intents)
          (void)router.route(intent, recipients);
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

} // namespace glasswyrm::server
