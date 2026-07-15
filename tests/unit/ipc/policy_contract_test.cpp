#include "ipc/wire/policy_contract.hpp"
#include "tests/helpers/test_support.hpp"

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace {
using namespace gw::ipc::wire;
using gw::test::require;

std::vector<std::uint8_t> hex(std::string_view text) {
  auto digit = [](char c) {
    return static_cast<std::uint8_t>(c <= '9' ? c - '0' : c - 'a' + 10);
  };
  std::vector<std::uint8_t> result;
  for (std::size_t i = 0; i < text.size(); i += 2)
    result.push_back(
        static_cast<std::uint8_t>((digit(text[i]) << 4U) | digit(text[i + 1])));
  return result;
}

template <class T>
void malformed_prefixes(const T &value, const char *message) {
  const auto bytes = encode(value);
  T decoded;
  for (std::size_t n = 0; n < bytes.size(); ++n)
    require(decode(std::span(bytes).first(n), decoded) ==
                CodecStatus::Truncated,
            message);
  auto trailing = bytes;
  trailing.push_back(0);
  require(decode(trailing, decoded) == CodecStatus::TrailingData,
          "policy codec rejects trailing bytes");
}

void exact_goldens() {
  PolicyContextUpsert context{1, 2, 3, -4, 5, 640, 480, 0};
  PolicyWindowUpsert window{10,
                            1,
                            0,
                            2,
                            -3,
                            4,
                            100,
                            80,
                            1,
                            PolicyWindowType::Dialog,
                            PolicyMapIntent::WantsMap,
                            false,
                            2,
                            true,
                            false,
                            false,
                            true,
                            11,
                            12,
                            13,
                            0};
  PolicyWindowRemove remove{10};
  PolicyCommit commit{20, 2, 0};
  PolicyWindowState state{10,
                          0,
                          2,
                          3,
                          -4,
                          5,
                          100,
                          80,
                          0,
                          PolicyWindowType::Normal,
                          PolicyAppliedState::Normal,
                          true,
                          true,
                          true,
                          true,
                          false,
                          true,
                          2,
                          0,
                          0};
  PolicyAcknowledged ack{
      20, 2, 3, 0x0102030405060708ULL, 1, PolicyResult::Accepted};
  PolicyBindingsUpsert bindings{8, 8, 8, 1, 3, 0xffc1, 96, 64, true, true};
  require(encode(context) == hex("01000000020000000300000000000000fcffffff05000"
                                 "00080020000e00100000000000000000000"),
          "context exact golden");
  require(encode(window) ==
              hex("0a000000010000000000000002000000fdffffff04000000640000005000"
                  "0000010000000200010000020100000100000b000000000000000c000000"
                  "000000000d000000000000000000000000000000"),
          "window exact golden");
  require(encode(remove) == hex("0a00000000000000"), "remove exact golden");
  require(encode(commit) ==
              hex("140000000000000002000000000000000000000000000000"),
          "commit exact golden");
  require(encode(state) == hex("0a000000000000000200000000000000030000000000000"
                               "0fcffffff05000000640000005000000000000000010001"
                               "0001010101000102000000000000000000"),
          "state exact golden");
  require(encode(ack) == hex("1400000000000000020000000000000003000000000000000"
                             "8070605040302010100000001000000"),
          "ack exact golden");
  require(encode(bindings) ==
              hex("080008000800000001030000c1ff00006000000040000000"
                  "0101000000000000"),
          "interactive bindings exact golden");
  malformed_prefixes(context, "truncated context");
  malformed_prefixes(window, "truncated window");
  malformed_prefixes(remove, "truncated remove");
  malformed_prefixes(commit, "truncated commit");
  malformed_prefixes(state, "truncated state");
  malformed_prefixes(ack, "truncated ack");
  malformed_prefixes(bindings, "truncated bindings");
}

void invalid_values() {
  PolicyContextUpsert context{1, 2, 3, 0, 0, 1, 1, 0};
  PolicyContextUpsert dc;
  context.work_width = 0;
  require(decode(encode(context), dc) == CodecStatus::InvalidValue,
          "zero work width rejected");
  PolicyWindowUpsert window{10,
                            1,
                            0,
                            2,
                            0,
                            0,
                            1,
                            1,
                            0,
                            PolicyWindowType::Normal,
                            PolicyMapIntent::WantsMap,
                            false,
                            0,
                            false,
                            false,
                            false,
                            false,
                            1,
                            2,
                            0,
                            0};
  PolicyWindowUpsert dw;
  window.override_redirect =
      static_cast<bool>(2); // bool canonicalizes; mutate encoded byte instead.
  auto wb = encode(window);
  wb[44] = 2;
  require(decode(wb, dw) == CodecStatus::InvalidValue,
          "invalid window boolean rejected");
  wb = encode(window);
  wb[36] = 0xff;
  require(decode(wb, dw) == CodecStatus::InvalidValue,
          "invalid window enum rejected");
  window.map_intent = PolicyMapIntent::Unmapped;
  require(decode(encode(window), dw) == CodecStatus::Ok &&
              dw.map_serial == window.map_serial,
          "unmapped window retains its ordering serial");
  PolicyWindowState state{10,
                          0,
                          2,
                          3,
                          0,
                          0,
                          1,
                          1,
                          0,
                          PolicyWindowType::Normal,
                          PolicyAppliedState::Normal,
                          true,
                          false,
                          true,
                          true,
                          false,
                          false,
                          0,
                          0,
                          0};
  PolicyWindowState ds;
  auto sb = encode(state);
  sb[52] = 2;
  require(decode(sb, ds) == CodecStatus::InvalidValue,
          "invalid state boolean rejected");
  PolicyAcknowledged ack{1, 1, 1, 0, 0, static_cast<PolicyResult>(8)};
  PolicyAcknowledged da;
  require(decode(encode(ack), da) == CodecStatus::InvalidValue,
          "invalid result rejected");
  PolicyBindingsUpsert bindings{8, 8, 8, 1, 3, 0xffc1, 96, 64, true, true};
  PolicyBindingsUpsert decoded_bindings;
  auto bb = encode(bindings);
  bb[6] = 1;
  require(decode(bb, decoded_bindings) == CodecStatus::InvalidValue,
          "bindings reserved fields are rejected");
  bindings.move_modifiers = UINT16_C(0x0100);
  require(decode(encode(bindings), decoded_bindings) ==
              CodecStatus::InvalidValue,
          "bindings reject unknown core modifier bits");
  bindings.move_modifiers = 8;
  bindings.resize_button = 10;
  require(decode(encode(bindings), decoded_bindings) ==
              CodecStatus::InvalidValue,
          "bindings reject buttons outside the core policy range");
  bindings.resize_button = 3;
  bindings.minimum_width = 16385;
  require(decode(encode(bindings), decoded_bindings) ==
              CodecStatus::InvalidValue,
          "bindings reject minimum dimensions beyond policy bounds");
}
} // namespace
int main() {
  exact_goldens();
  invalid_values();
  return 0;
}
