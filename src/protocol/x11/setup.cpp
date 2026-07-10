#include "protocol/x11/setup.hpp"

#include "core/checked_math.hpp"
#include "protocol/x11/byte_cursor.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <optional>
#include <stdexcept>

namespace gw::protocol::x11 {
namespace {

constexpr std::array<std::uint8_t, 21> kVendor{
    'G', 'l', 'a', 's', 's', 'w', 'y', 'r', 'm', ' ', 'M',
    'i', 'l', 'e', 's', 't', 'o', 'n', 'e', ' ', '1'};

constexpr std::uint32_t kRootWindow = 1;
constexpr std::uint32_t kDefaultColormap = 2;
constexpr std::uint32_t kRootVisual = 3;

[[nodiscard]] std::size_t padding_for(const std::size_t size) noexcept {
  return (4 - (size & 3U)) & 3U;
}

} // namespace

SetupParser::SetupParser(const std::size_t maximum_size) noexcept
    : maximum_size_(maximum_size) {
  bytes_.reserve(std::min(maximum_size_, kDefaultMaximumSetupSize));
  if (maximum_size_ < kSetupRequestHeaderSize) {
    status_ = ParseStatus::MessageTooLarge;
  }
}

ParseResult SetupParser::consume(const std::span<const std::uint8_t> input) {
  if (status_ != ParseStatus::NeedMore) {
    return {status_, 0};
  }

  std::size_t consumed = 0;
  while (consumed < input.size() && status_ == ParseStatus::NeedMore) {
    if (bytes_.empty()) {
      bytes_.push_back(input[consumed++]);
      const auto order = byte_order_from_marker(bytes_.front());
      if (!order) {
        status_ = ParseStatus::InvalidByteOrder;
        break;
      }
      request_.byte_order = *order;
    }

    const std::size_t needed = expected_size_ - bytes_.size();
    const std::size_t available = input.size() - consumed;
    const std::size_t copy_size = std::min(needed, available);
    bytes_.insert(
        bytes_.end(), input.begin() + static_cast<std::ptrdiff_t>(consumed),
        input.begin() + static_cast<std::ptrdiff_t>(consumed + copy_size));
    consumed += copy_size;

    if (bytes_.size() == kSetupRequestHeaderSize &&
        expected_size_ == kSetupRequestHeaderSize) {
      status_ = inspect_header();
      if (status_ != ParseStatus::NeedMore) {
        break;
      }
    }

    if (bytes_.size() == expected_size_) {
      finish_request();
      status_ = ParseStatus::Complete;
    }
  }

  return {status_, consumed};
}

ParseStatus SetupParser::eof() const noexcept {
  return status_ == ParseStatus::NeedMore ? ParseStatus::TruncatedInput
                                          : status_;
}

ParseStatus SetupParser::inspect_header() {
  ByteReader reader(bytes_, request_.byte_order);
  std::uint16_t authorization_name_size = 0;
  std::uint16_t authorization_data_size = 0;
  if (!reader.skip(2) || !reader.read_u16(request_.protocol_major) ||
      !reader.read_u16(request_.protocol_minor) ||
      !reader.read_u16(authorization_name_size) ||
      !reader.read_u16(authorization_data_size) || !reader.skip(2)) {
    return ParseStatus::TruncatedInput;
  }

  authorization_name_size_ = authorization_name_size;
  authorization_data_size_ = authorization_data_size;
  const auto padded_name =
      core::checked_align_up(authorization_name_size_, std::size_t{4});
  const auto padded_data =
      core::checked_align_up(authorization_data_size_, std::size_t{4});
  if (!padded_name || !padded_data) {
    return ParseStatus::LengthOverflow;
  }
  authorization_name_padded_size_ = *padded_name;
  const auto after_name =
      core::checked_add(kSetupRequestHeaderSize, *padded_name);
  const auto total_size =
      after_name ? core::checked_add(*after_name, *padded_data) : std::nullopt;
  if (!total_size) {
    return ParseStatus::LengthOverflow;
  }
  if (*total_size > maximum_size_) {
    return ParseStatus::MessageTooLarge;
  }
  expected_size_ = *total_size;
  return ParseStatus::NeedMore;
}

void SetupParser::finish_request() {
  const std::size_t name_offset = kSetupRequestHeaderSize;
  const std::size_t data_offset = name_offset + authorization_name_padded_size_;
  request_.authorization_name.assign(
      bytes_.begin() + static_cast<std::ptrdiff_t>(name_offset),
      bytes_.begin() +
          static_cast<std::ptrdiff_t>(name_offset + authorization_name_size_));
  request_.authorization_data.assign(
      bytes_.begin() + static_cast<std::ptrdiff_t>(data_offset),
      bytes_.begin() +
          static_cast<std::ptrdiff_t>(data_offset + authorization_data_size_));
}

SetupDecision evaluate_setup_request(const SetupRequest &request) noexcept {
  if (request.protocol_major != kProtocolMajor ||
      request.protocol_minor != kProtocolMinor) {
    return SetupDecision::UnsupportedVersion;
  }
  if (!request.authorization_name.empty() ||
      !request.authorization_data.empty()) {
    return SetupDecision::UnsupportedAuthorization;
  }
  return SetupDecision::Accepted;
}

std::vector<std::uint8_t> encode_setup_success(const ByteOrder order,
                                               const SetupReplyConfig &config) {
  ByteWriter body(order);
  body.write_u32(1); // release number
  body.write_u32(config.resource_id_base);
  body.write_u32(config.resource_id_mask);
  body.write_u32(0); // motion buffer size
  body.write_u16(static_cast<std::uint16_t>(kVendor.size()));
  body.write_u16(std::numeric_limits<std::uint16_t>::max());
  body.write_u8(1);                         // screens
  body.write_u8(1);                         // pixmap formats
  body.write_u8(0);                         // image byte order: LSBFirst
  body.write_u8(0);                         // bitmap bit order: LSBFirst
  body.write_u8(32);                        // bitmap scanline unit
  body.write_u8(32);                        // bitmap scanline pad
  body.write_u8(8);                         // minimum keycode
  body.write_u8(255);                       // maximum keycode
  body.write_padding(4);

  body.write_bytes(kVendor);
  body.write_padding(padding_for(kVendor.size()));

  body.write_u8(24); // pixmap depth
  body.write_u8(32); // bits per pixel
  body.write_u8(32); // scanline pad
  body.write_padding(5);

  body.write_u32(kRootWindow);
  body.write_u32(kDefaultColormap);
  body.write_u32(0x00ffffff); // white pixel
  body.write_u32(0);          // black pixel
  body.write_u32(0);          // current input masks
  body.write_u16(1024);
  body.write_u16(768);
  body.write_u16(270);
  body.write_u16(203);
  body.write_u16(1); // installed colormaps min
  body.write_u16(1); // installed colormaps max
  body.write_u32(kRootVisual);
  body.write_u8(0);  // backing store: Never
  body.write_u8(0);  // save unders
  body.write_u8(24); // root depth
  body.write_u8(1);  // allowed depths

  body.write_u8(24);
  body.write_padding(1);
  body.write_u16(1); // visuals
  body.write_padding(4);

  body.write_u32(kRootVisual);
  body.write_u8(4); // TrueColor
  body.write_u8(8); // bits per RGB value
  body.write_u16(256);
  body.write_u32(0x00ff0000);
  body.write_u32(0x0000ff00);
  body.write_u32(0x000000ff);
  body.write_padding(4);

  const std::size_t body_size = body.size();
  if ((body_size & 3U) != 0 ||
      body_size / 4 > std::numeric_limits<std::uint16_t>::max()) {
    throw std::length_error("X11 setup success reply is too large");
  }

  ByteWriter reply(order);
  reply.write_u8(1);
  reply.write_u8(0);
  reply.write_u16(kProtocolMajor);
  reply.write_u16(kProtocolMinor);
  reply.write_u16(static_cast<std::uint16_t>(body_size / 4));
  const auto encoded_body = std::move(body).take();
  reply.write_bytes(encoded_body);
  return std::move(reply).take();
}

std::vector<std::uint8_t> encode_setup_failure(const ByteOrder order,
                                               const std::string_view reason) {
  if (reason.size() > std::numeric_limits<std::uint8_t>::max()) {
    throw std::length_error("X11 setup failure reason is too large");
  }

  const std::size_t padding = padding_for(reason.size());
  const std::size_t body_size = reason.size() + padding;
  ByteWriter reply(order);
  reply.write_u8(0);
  reply.write_u8(static_cast<std::uint8_t>(reason.size()));
  reply.write_u16(kProtocolMajor);
  reply.write_u16(kProtocolMinor);
  reply.write_u16(static_cast<std::uint16_t>(body_size / 4));
  reply.write_bytes(std::span<const std::uint8_t>(
      reinterpret_cast<const std::uint8_t *>(reason.data()), reason.size()));
  reply.write_padding(padding);
  return std::move(reply).take();
}

} // namespace gw::protocol::x11
