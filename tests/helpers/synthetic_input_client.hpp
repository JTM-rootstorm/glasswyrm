#pragma once

#include <glasswyrm/ipc.h>

#include <cstdint>
#include <memory>
#include <string>

namespace gw::test {

class SyntheticInputClient {
 public:
  explicit SyntheticInputClient(const std::string& socket_path);
  ~SyntheticInputClient();
  SyntheticInputClient(const SyntheticInputClient&) = delete;
  SyntheticInputClient& operator=(const SyntheticInputClient&) = delete;
  SyntheticInputClient(SyntheticInputClient&&) noexcept;
  SyntheticInputClient& operator=(SyntheticInputClient&&) noexcept;

  [[nodiscard]] gwipc_synthetic_input_acknowledged motion(
      std::uint64_t input_id, std::uint32_t time_ms, std::int32_t x,
      std::int32_t y);
  [[nodiscard]] gwipc_synthetic_input_acknowledged button(
      std::uint64_t input_id, std::uint32_t time_ms, std::uint8_t button,
      bool pressed);
  [[nodiscard]] gwipc_synthetic_input_acknowledged key(
      std::uint64_t input_id, std::uint32_t time_ms, std::uint8_t keycode,
      bool pressed);
  [[nodiscard]] gwipc_synthetic_input_acknowledged barrier(
      std::uint64_t input_id);

 private:
  struct ConnectionDeleter {
    void operator()(gwipc_connection*) const;
  };
  std::unique_ptr<gwipc_connection, ConnectionDeleter> connection_;

  [[nodiscard]] gwipc_synthetic_input_acknowledged receive_ack(
      std::uint64_t expected_input_id);
};

}  // namespace gw::test

