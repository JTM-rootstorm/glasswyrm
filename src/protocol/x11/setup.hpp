#pragma once

#include "protocol/x11/byte_order.hpp"
#include "protocol/x11/screen_model.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace gw::protocol::x11 {

inline constexpr std::uint16_t kProtocolMajor = 11;
inline constexpr std::uint16_t kProtocolMinor = 0;
inline constexpr std::size_t kSetupRequestHeaderSize = 12;
inline constexpr std::size_t kDefaultMaximumSetupSize = 4096;

struct SetupRequest {
  ByteOrder byte_order{ByteOrder::LittleEndian};
  std::uint16_t protocol_major{0};
  std::uint16_t protocol_minor{0};
  std::vector<std::uint8_t> authorization_name;
  std::vector<std::uint8_t> authorization_data;
};

enum class ParseStatus {
  NeedMore,
  Complete,
  InvalidByteOrder,
  MessageTooLarge,
  LengthOverflow,
  TruncatedInput,
};

struct ParseResult {
  ParseStatus status{ParseStatus::NeedMore};
  std::size_t consumed{0};
};

class SetupParser {
public:
  explicit SetupParser(
      std::size_t maximum_size = kDefaultMaximumSetupSize) noexcept;

  [[nodiscard]] ParseResult consume(std::span<const std::uint8_t> input);
  [[nodiscard]] ParseStatus eof() const noexcept;
  [[nodiscard]] const SetupRequest &request() const noexcept {
    return request_;
  }
  [[nodiscard]] std::size_t expected_size() const noexcept {
    return expected_size_;
  }

private:
  [[nodiscard]] ParseStatus inspect_header();
  void finish_request();

  std::size_t maximum_size_;
  std::size_t expected_size_{kSetupRequestHeaderSize};
  std::size_t authorization_name_size_{0};
  std::size_t authorization_name_padded_size_{0};
  std::size_t authorization_data_size_{0};
  ParseStatus status_{ParseStatus::NeedMore};
  SetupRequest request_;
  std::vector<std::uint8_t> bytes_;
};

enum class SetupDecision {
  Accepted,
  UnsupportedVersion,
  UnsupportedAuthorization,
};

[[nodiscard]] SetupDecision
evaluate_setup_request(const SetupRequest &request) noexcept;

struct SetupReplyConfig {
  std::uint32_t resource_id_base{0x00400000};
  std::uint32_t resource_id_mask{kScreenModel.resource_id_mask};
  bool game_compat{false};
};

[[nodiscard]] std::vector<std::uint8_t>
encode_setup_success(ByteOrder order, const SetupReplyConfig &config = {});
[[nodiscard]] std::vector<std::uint8_t>
encode_setup_failure(ByteOrder order, std::string_view reason);

} // namespace gw::protocol::x11
