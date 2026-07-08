#include <cassert>

#include <glasswyrm/core/build_config.hpp>
#include <glasswyrm/core/server.hpp>

int main() {
  const auto config = glasswyrm::core::build_config();
  assert(config.version == "0.1.0");
  assert(glasswyrm::core::project_name() == "Glasswyrm");
  assert(!glasswyrm::core::assembly_mode_name(config.assembly_mode).empty());

  const glasswyrm::core::Server server(config);
  assert(server.status() == glasswyrm::core::ServerStatus::ScaffoldOnly);
  assert(server.describe().find("server scaffold") != std::string::npos);

  return 0;
}
