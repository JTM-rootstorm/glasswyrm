#include "session/launcher.hpp"

#include <csignal>
#include <iostream>

namespace {

volatile std::sig_atomic_t pending_signal = 0;

extern "C" void handle_signal(int signal) { pending_signal = signal; }

bool install_handler(int signal) {
  struct sigaction action{};
  action.sa_handler = handle_signal;
  ::sigemptyset(&action.sa_mask);
  return ::sigaction(signal, &action, nullptr) == 0;
}

} // namespace

int main(int argc, char **argv) {
  glasswyrm::session::Options options;
  const auto result = glasswyrm::session::parse_options(argc, argv, options,
                                                        std::cout, std::cerr);
  if (result == glasswyrm::session::ParseOptionsResult::ExitSuccess)
    return 0;
  if (result == glasswyrm::session::ParseOptionsResult::ExitFailure)
    return 2;
  if (!install_handler(SIGINT) || !install_handler(SIGTERM)) {
    std::cerr << "glasswyrm-session: cannot install signal handlers\n";
    return 2;
  }
  return glasswyrm::session::run_launcher(options, std::cerr, &pending_signal);
}
