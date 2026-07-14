#include "gwm/options.hpp"
#include "gwm/runtime.hpp"

#include <iostream>

int main(int argc, char** argv) {
  glasswyrm::wm::Options options;
  const auto result = glasswyrm::wm::parse_options(argc, argv, options,
                                                   std::cout, std::cerr);
  if (result == glasswyrm::wm::ParseOptionsResult::ExitSuccess) return 0;
  if (result == glasswyrm::wm::ParseOptionsResult::ExitFailure) return 2;
  return glasswyrm::wm::runtime::run(options);
}
