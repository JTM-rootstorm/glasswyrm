#include "glasswyrmd/compatibility_trace.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>
#include <vector>

namespace {
void require(bool condition) {
  if (!condition) std::exit(1);
}
}

int main() {
  using glasswyrm::server::x11_request_name;
  require(x11_request_name(9) == "MapSubwindows");
  require(x11_request_name(38) == "QueryPointer");
  require(x11_request_name(45) == "OpenFont");
  require(x11_request_name(65) == "PolyLine");
  require(x11_request_name(71) == "PolyFillArc");
  require(x11_request_name(76) == "ImageText8");
  require(x11_request_name(84) == "AllocColor");
  require(x11_request_name(98) == "QueryExtension");
  require(x11_request_name(101) == "GetKeyboardMapping");
  require(x11_request_name(119) == "GetModifierMapping");
  require(x11_request_name(250) == "Unknown");
  const auto directory = std::filesystem::temp_directory_path() /
                         ("gw-m9-trace-" + std::to_string(::getpid()));
  std::filesystem::create_directory(directory);
  const auto path = directory / "trace.jsonl";
  std::string error;
  {
    auto trace = glasswyrm::server::CompatibilityTrace::create(path, error);
    require(trace != nullptr);
    trace->connection(1, "accepted");
    trace->request(1, 42, 70, 36, {});
    trace->packet(1, 42, std::vector<std::uint8_t>{1, 0, 0, 0});
    trace->request(1, 43, 250, 8,
                   std::vector<std::uint8_t>{0, 1, 0, 0});
  }
  std::ifstream input(path);
  const std::string contents((std::istreambuf_iterator<char>(input)), {});
  require(contents ==
          "{\"direction\":\"connection\",\"client\":1,\"outcome\":\"accepted\"}\n"
          "{\"direction\":\"request\",\"client\":1,\"sequence\":42,\"opcode\":70,\"name\":\"PolyFillRectangle\",\"length\":36,\"outcome\":\"success\",\"error\":null}\n"
          "{\"direction\":\"reply\",\"client\":1,\"sequence\":42}\n"
          "{\"direction\":\"request\",\"client\":1,\"sequence\":43,\"opcode\":250,\"name\":\"Unknown\",\"length\":8,\"outcome\":\"error\",\"error\":\"BadRequest\"}\n");
  require(contents.find("payload") == std::string::npos);

  auto duplicate = glasswyrm::server::CompatibilityTrace::create(path, error);
  require(!duplicate);
  const auto symlink = directory / "link";
  std::filesystem::create_symlink(path, symlink);
  auto followed = glasswyrm::server::CompatibilityTrace::create(symlink, error);
  require(!followed);
  std::filesystem::remove_all(directory);
}
