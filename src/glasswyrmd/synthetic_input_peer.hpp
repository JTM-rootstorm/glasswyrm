#pragma once

#include <glasswyrm/ipc.h>

#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <string>

namespace glasswyrm::server {

struct SyntheticInputRecord {
  enum class Kind { Motion, Button, Key, Barrier };
  Kind kind{Kind::Barrier};
  std::uint64_t sequence{};
  std::uint64_t input_id{};
  std::uint32_t time_ms{};
  std::int32_t root_x{};
  std::int32_t root_y{};
  std::uint8_t detail{};
  bool pressed{};
};

class SyntheticInputPeer {
 public:
  explicit SyntheticInputPeer(std::string path);
  ~SyntheticInputPeer();
  SyntheticInputPeer(const SyntheticInputPeer&) = delete;
  SyntheticInputPeer& operator=(const SyntheticInputPeer&) = delete;

  [[nodiscard]] bool start(std::string& error);
  [[nodiscard]] int listener_fd() const noexcept;
  [[nodiscard]] short listener_events() const noexcept;
  [[nodiscard]] int connection_fd() const noexcept;
  [[nodiscard]] short connection_events() const noexcept;
  void accept_provider();
  void service(short revents);
  [[nodiscard]] std::optional<SyntheticInputRecord> take_record();
  [[nodiscard]] bool acknowledge(
      const SyntheticInputRecord& record,
      const gwipc_synthetic_input_acknowledged& acknowledgement);
  [[nodiscard]] bool connected() const noexcept;
  [[nodiscard]] bool has_records() const noexcept { return !records_.empty(); }
  [[nodiscard]] bool consume_disconnect() noexcept;
  void disconnect() noexcept;

 private:
  struct ListenerDeleter {
    void operator()(gwipc_listener* value) const noexcept;
  };
  struct ConnectionDeleter {
    void operator()(gwipc_connection* value) const noexcept;
  };
  void drain();

  std::string path_;
  std::unique_ptr<gwipc_listener, ListenerDeleter> listener_;
  std::unique_ptr<gwipc_connection, ConnectionDeleter> connection_;
  std::deque<SyntheticInputRecord> records_;
  bool disconnected_{};
};

}  // namespace glasswyrm::server
