#include "glasswyrmd/options.hpp"
#include "glasswyrmd/server.hpp"

#include <iostream>

int main(int argc, char** argv) {
  glasswyrm::server::Options options;
  const auto result = glasswyrm::server::parse_options(
      argc, argv, options, std::cout, std::cerr);
  if (result == glasswyrm::server::ParseOptionsResult::ExitSuccess) {
    return 0;
  }
  if (result == glasswyrm::server::ParseOptionsResult::ExitFailure) {
    return 2;
  }
  return glasswyrm::server::Server(std::move(options)).run();
}
