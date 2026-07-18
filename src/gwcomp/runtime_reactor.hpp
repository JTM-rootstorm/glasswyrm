#pragma once

#include "gwcomp/compositor.hpp"
#include "gwcomp/contract_dispatch.hpp"
#include "gwcomp/options.hpp"
#include "gwcomp/session_state_coordinator.hpp"
#include "gwcomp/signal_runtime.hpp"

#include <glasswyrm/ipc.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace glasswyrm::compositor {

class RuntimeReactor final {
 public:
  RuntimeReactor(const Options& options, gwipc_listener* listener,
                 SignalRuntime& signals,
                 gw::compositor::Compositor& compositor);

  [[nodiscard]] int run();

 private:
  struct ConnectionDeleter {
    void operator()(gwipc_connection* value) const;
  };

  using Connection = std::unique_ptr<gwipc_connection, ConnectionDeleter>;

  struct PollEvents {
    short listener{};
    short producer{};
    short signals{};
    short presentation{};
  };

  // Returns 1 for ready descriptors, 0 for EINTR, and -1 for a fatal poll.
  [[nodiscard]] int poll(PollEvents& events);
  void service_signals(short revents);
  void service_presentation(short revents);
  void service_virtual_terminal();
  void accept_producer(short revents);
  void service_producer_transport(short revents);
  void validate_producer();
  void service_session_messages();
  void service_contract_messages();
  void service_disconnect();
  void apply_dispatch(const ContractDispatchResult& dispatch);

  const Options& options_;
  gwipc_listener* listener_{};
  SignalRuntime& signals_;
  gw::compositor::Compositor& compositor_;
  Connection producer_;
  bool peer_validated_{};
  gwipc_role peer_role_{GWIPC_ROLE_UNKNOWN};
  std::optional<gw::compositor::PeerProfile> peer_profile_;
  bool accepted_any_frame_{};
  bool stop_after_flush_{};
  bool stopping_{};
  bool vt_release_requested_{};
  bool vt_acquire_requested_{};
  bool buffered_work_pending_{};
  SessionStateCoordinator session_state_;
  int exit_status_{};
  std::optional<std::uint64_t> pending_reply_sequence_;
};

}  // namespace glasswyrm::compositor
