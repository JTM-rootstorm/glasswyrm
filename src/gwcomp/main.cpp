#include "gwcomp/options.hpp"
#include "gwcomp/runtime.hpp"

#include <iostream>

int main(int argc, char** argv) {
  glasswyrm::compositor::Options options;
  const auto result = glasswyrm::compositor::parse_options(
      argc, argv, options, std::cout, std::cerr);
  if (result == glasswyrm::compositor::ParseOptionsResult::ExitSuccess) return 0;
  if (result == glasswyrm::compositor::ParseOptionsResult::ExitFailure) return 2;
  return glasswyrm::compositor::run(options);
}
