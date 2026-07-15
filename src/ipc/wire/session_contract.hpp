#pragma once

#include "ipc/wire/types.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace gw::ipc::wire {

enum class SessionState : std::uint16_t {
  Inactive = 1,
  Active = 2,
};

enum class SessionStateResult : std::uint16_t {
  Accepted = 1,
  AlreadyApplied = 2,
  InputUnavailable = 3,
  Failed = 4,
};

struct SessionStateChange {
  std::uint64_t generation{};
  SessionState state{SessionState::Inactive};
  std::uint32_t flags{};
};

struct SessionStateAcknowledged {
  std::uint64_t generation{};
  SessionState state{SessionState::Inactive};
  SessionStateResult result{SessionStateResult::Accepted};
  std::uint32_t flags{};
};

std::vector<std::uint8_t> encode(const SessionStateChange &value);
CodecStatus decode(std::span<const std::uint8_t> bytes,
                   SessionStateChange &value);
std::vector<std::uint8_t> encode(const SessionStateAcknowledged &value);
CodecStatus decode(std::span<const std::uint8_t> bytes,
                   SessionStateAcknowledged &value);

} // namespace gw::ipc::wire
