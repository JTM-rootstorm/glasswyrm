#include "glasswyrmd/compatibility_trace.hpp"

#include <cstdlib>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <string>
#include <sys/resource.h>
#include <unistd.h>
#include <vector>

namespace {
void require(bool condition) {
  if (!condition) std::exit(1);
}
}

int main() {
  require(glasswyrm::server::x11_request_name(33) == "GrabKey" &&
          glasswyrm::server::x11_request_name(34) == "UngrabKey");
  using glasswyrm::server::x11_request_name;
  require(x11_request_name(9) == "MapSubwindows");
  require(x11_request_name(22) == "SetSelectionOwner");
  require(x11_request_name(25) == "SendEvent");
  require(x11_request_name(26) == "GrabPointer");
  require(x11_request_name(35) == "AllowEvents");
  require(x11_request_name(38) == "QueryPointer");
  require(x11_request_name(41) == "WarpPointer");
  require(x11_request_name(42) == "SetInputFocus");
  require(x11_request_name(44) == "QueryKeymap");
  require(x11_request_name(45) == "OpenFont");
  require(x11_request_name(65) == "PolyLine");
  require(x11_request_name(71) == "PolyFillArc");
  require(x11_request_name(76) == "ImageText8");
  require(x11_request_name(84) == "AllocColor");
  require(x11_request_name(94) == "CreateGlyphCursor");
  require(x11_request_name(98) == "QueryExtension");
  require(x11_request_name(101) == "GetKeyboardMapping");
  require(x11_request_name(103) == "GetKeyboardControl");
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
    const std::vector<std::uint8_t> query_extension{
        98, 0, 4, 0, 6, 0, 0, 0, 'R', 'E', 'N', 'D', 'E', 'R', 0, 0};
    trace->request(1, 43, 98, query_extension.size(), {}, query_extension);
    const std::vector<std::uint8_t> shm_put_image{
        129, 3, 10, 0, 0, 0, 0, 0};
    trace->request(1, 44, 129, 40, {}, shm_put_image);
    std::vector<std::uint8_t> motion(32);
    motion[0] = 6; motion[2] = 0x34; motion[3] = 0x12;
    motion[12] = 0x44; motion[13] = 0x33; motion[14] = 0x22; motion[15] = 0x11;
    trace->packet(1, 99, motion);
    std::vector<std::uint8_t> expose(32);
    expose[0] = 12; expose[2] = 0xab; expose[3] = 0xcd;
    expose[4] = 0x01; expose[5] = 0x02; expose[6] = 0x03; expose[7] = 0x04;
    trace->packet(2, 99, expose,
                  gw::protocol::x11::ByteOrder::BigEndian);
    trace->request(1, 43, 250, 8,
                   std::vector<std::uint8_t>{0, 1, 0, 0});
    trace->connection(1, "disconnected");
  }
  std::ifstream input(path);
  const std::string contents((std::istreambuf_iterator<char>(input)), {});
  require(contents ==
          "{\"direction\":\"connection\",\"client\":1,\"outcome\":\"accepted\"}\n"
          "{\"direction\":\"request\",\"client\":1,\"sequence\":42,\"opcode\":70,\"name\":\"PolyFillRectangle\",\"length\":36,\"outcome\":\"success\",\"error\":null}\n"
          "{\"direction\":\"reply\",\"client\":1,\"sequence\":42}\n"
          "{\"direction\":\"request\",\"client\":1,\"sequence\":43,\"opcode\":98,\"name\":\"QueryExtension\",\"extension\":\"RENDER\",\"length\":16,\"outcome\":\"success\",\"error\":null}\n"
          "{\"direction\":\"request\",\"client\":1,\"sequence\":44,\"opcode\":129,\"name\":\"Unknown\",\"extension\":\"MIT-SHM\",\"minor\":3,\"length\":40,\"outcome\":\"success\",\"error\":null}\n"
          "{\"direction\":\"event\",\"client\":1,\"sequence\":4660,\"event_type\":6,\"window\":287454020}\n"
          "{\"direction\":\"event\",\"client\":2,\"sequence\":43981,\"event_type\":12,\"window\":16909060}\n"
          "{\"direction\":\"request\",\"client\":1,\"sequence\":43,\"opcode\":250,\"name\":\"Unknown\",\"length\":8,\"outcome\":\"error\",\"error\":\"BadRequest\"}\n"
          "{\"direction\":\"connection\",\"client\":1,\"outcome\":\"disconnected\"}\n");
  require(contents.find("payload") == std::string::npos);

  auto duplicate = glasswyrm::server::CompatibilityTrace::create(path, error);
  require(!duplicate);
  const auto symlink = directory / "link";
  std::filesystem::create_symlink(path, symlink);
  auto followed = glasswyrm::server::CompatibilityTrace::create(symlink, error);
  require(!followed);
  const auto fault_path = directory / "write-fault.jsonl";
  auto fault_trace =
      glasswyrm::server::CompatibilityTrace::create(fault_path, error);
  require(fault_trace != nullptr);
  rlimit previous_limit{};
  require(::getrlimit(RLIMIT_FSIZE, &previous_limit) == 0);
  auto limited = previous_limit;
  limited.rlim_cur = 64;
  const auto previous_handler = std::signal(SIGXFSZ, SIG_IGN);
  require(previous_handler != SIG_ERR &&
          ::setrlimit(RLIMIT_FSIZE, &limited) == 0);
  fault_trace->connection(100, "accepted");
  fault_trace->connection(101, "accepted");
  require(!fault_trace->enabled());
  require(::setrlimit(RLIMIT_FSIZE, &previous_limit) == 0);
  require(std::signal(SIGXFSZ, previous_handler) != SIG_ERR);
  std::filesystem::remove_all(directory);
}
