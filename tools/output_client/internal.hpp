#pragma once

#include "output_client/output_client.hpp"

namespace glasswyrm::tools::output_client {

class SnapshotDecoder final {
public:
  SnapshotDecoder(std::uint64_t request_id, std::uint32_t query_flags)
      : request_id_(request_id), query_flags_(query_flags) {}

  [[nodiscard]] bool consume(const gwipc_message *message, std::string &error);
  [[nodiscard]] bool complete() const noexcept { return complete_; }
  [[nodiscard]] Snapshot take() { return std::move(snapshot_); }

private:
  [[nodiscard]] bool consume_control(const gwipc_message *message,
                                     std::string &error);
  [[nodiscard]] bool consume_contract(const gwipc_message *message,
                                      std::string &error);
  std::uint64_t request_id_{};
  std::uint32_t query_flags_{};
  Snapshot snapshot_;
  std::uint64_t snapshot_id_{};
  std::uint32_t expected_items_{};
  std::uint32_t actual_items_{};
  bool reading_{};
  bool ended_{};
  bool acknowledged_{};
  bool complete_{};
};

} // namespace glasswyrm::tools::output_client
