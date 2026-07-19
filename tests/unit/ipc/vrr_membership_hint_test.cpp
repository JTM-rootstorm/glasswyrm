#include "ipc/vrr_membership_hint.hpp"

#include "tests/helpers/test_support.hpp"

#include <array>
#include <vector>

using glasswyrm::ipc::internal::decode_vrr_membership_hint;
using glasswyrm::ipc::internal::encode_vrr_membership_hint;
using glasswyrm::ipc::internal::kVrrMembershipHintTag;
using gw::test::require;

namespace {

void exact_bitmap_round_trip() {
  const std::array outputs{UINT64_C(10), UINT64_C(20), UINT64_C(30)};
  const std::array membership{UINT64_C(10), UINT64_C(30)};
  const auto encoded = encode_vrr_membership_hint(outputs, membership);
  require(encoded && *encoded == (kVrrMembershipHintTag | UINT64_C(0x05)),
          "M14 membership uses ascending output positions");
  require(decode_vrr_membership_hint(outputs, *encoded) ==
              std::vector<std::uint64_t>({10, 30}),
          "M14 membership bitmap restores exact IDs");

  const std::array eight_outputs{UINT64_C(1), UINT64_C(2), UINT64_C(3),
                                 UINT64_C(4), UINT64_C(5), UINT64_C(6),
                                 UINT64_C(7), UINT64_C(8)};
  const std::array last{UINT64_C(8)};
  const auto highest = encode_vrr_membership_hint(eight_outputs, last);
  require(highest && (*highest & UINT64_C(0xff)) == UINT64_C(0x80) &&
              decode_vrr_membership_hint(eight_outputs, *highest) ==
                  std::vector<std::uint64_t>({8}),
          "all eight canonical output membership bits are usable");
}

void empty_and_malformed_inputs() {
  const std::array outputs{UINT64_C(10), UINT64_C(20)};
  const std::array<std::uint64_t, 0> empty{};
  const auto encoded_empty = encode_vrr_membership_hint(outputs, empty);
  require(encoded_empty &&
              decode_vrr_membership_hint(outputs, *encoded_empty) ==
                  std::vector<std::uint64_t>{},
          "an exact empty membership has an explicit bitmap");
  require(!decode_vrr_membership_hint(outputs, UINT64_C(20)),
          "an ordinary historical preferred output is not an M14 bitmap");
  require(!decode_vrr_membership_hint(
              outputs, kVrrMembershipHintTag | UINT64_C(0x04)),
          "bits beyond the canonical output set are rejected");
  require(!decode_vrr_membership_hint(
              outputs, kVrrMembershipHintTag | UINT64_C(0x100)),
          "stray tagged payload bits are rejected");
}

void canonical_order_and_reference_validation() {
  const std::array reversed{UINT64_C(20), UINT64_C(10)};
  const std::array one{UINT64_C(10)};
  require(!encode_vrr_membership_hint(reversed, one),
          "noncanonical output ordering is rejected");

  const std::array outputs{UINT64_C(10), UINT64_C(20)};
  const std::array unsorted{UINT64_C(20), UINT64_C(10)};
  const std::array unknown{UINT64_C(30)};
  require(!encode_vrr_membership_hint(outputs, unsorted) &&
              !encode_vrr_membership_hint(outputs, unknown),
          "membership must be sorted, unique, and known");

  const std::array too_many{UINT64_C(1), UINT64_C(2), UINT64_C(3),
                            UINT64_C(4), UINT64_C(5), UINT64_C(6),
                            UINT64_C(7), UINT64_C(8), UINT64_C(9)};
  require(!encode_vrr_membership_hint(too_many, one),
          "the internal convention refuses more than eight outputs");
}

}  // namespace

int main() {
  exact_bitmap_round_trip();
  empty_and_malformed_inputs();
  canonical_order_and_reference_validation();
  return 0;
}
