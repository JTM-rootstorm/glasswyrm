#include "glasswyrmd/extensions/gw_vrr.hpp"
#include "glasswyrmd/vrr_window_state.hpp"
#include "protocol/x11/byte_cursor.hpp"
#include "tests/helpers/test_support.hpp"

namespace {

using namespace glasswyrm::server;
using namespace glasswyrm::server::extensions;
namespace x11 = gw::protocol::x11;
using gw::test::require;

std::uint32_t u32(const std::vector<std::uint8_t>& bytes,
                  const x11::ByteOrder order, const std::size_t offset) {
  x11::ByteReader reader(std::span<const std::uint8_t>(bytes).subspan(offset),
                         order);
  std::uint32_t value{};
  require(reader.read_u32(value), "decode GW_VRR u32");
  return value;
}

void test_store_and_changes() {
  VrrWindowStateStore store;
  require(store.find_window(7) == nullptr && store.find_output(9) == nullptr,
          "VRR store starts empty");
  auto& window = store.ensure_window(7);
  window.preference = WindowVrrPreference::Prefer;
  auto& output = store.ensure_output(9);
  output.policy = OutputVrrPolicyMode::Fullscreen;
  require(store.find_window(7)->preference == WindowVrrPreference::Prefer &&
              store.find_output(9)->policy == OutputVrrPolicyMode::Fullscreen,
          "VRR store retains independent window and output state");
  store.erase_window(7);
  require(store.find_window(7) == nullptr && store.find_output(9) != nullptr,
          "window destruction removes only ephemeral window VRR state");

  require(valid_vrr_preference(0) && valid_vrr_preference(3) &&
              !valid_vrr_preference(4),
          "VRR preference validation accepts only the frozen enum");
  WindowVrrState before;
  auto after = before;
  after.preference = WindowVrrPreference::Allow;
  after.policy_eligible = true;
  after.effective_output_enabled = true;
  require(vrr_change_mask(before, after) == kKnownVrrEventMask,
          "preference, eligibility, and effective changes are independent");
}

void test_notifications(const x11::ByteOrder order) {
  WindowVrrState before;
  before.event_selections.emplace(4, kVrrPreferenceChanged);
  before.event_selections.emplace(5, kVrrEffectiveStateChanged);
  auto after = before;
  after.preference = WindowVrrPreference::Prefer;
  after.effective_output_enabled = true;
  after.primary_output = 0x51;
  after.reason_flags = UINT64_C(0x0102030405060708);
  after.output_state_generation = UINT64_C(0x1112131415161718);
  const auto notifications = gw_vrr_notifications(
      order, 0x12345, 0x41, before, after, OutputVrrPolicyMode::AppRequested);
  require(notifications.size() == 2,
          "each subscriber receives only selected committed changes");
  for (const auto& notification : notifications) {
    const auto expected = notification.client == 4 ? kVrrPreferenceChanged
                                                   : kVrrEffectiveStateChanged;
    require(notification.bytes.size() == 32 &&
                notification.bytes[0] == kGwVrrEventBase &&
                (notification.bytes[1] & kKnownVrrEventMask) == expected &&
                (notification.bytes[1] & 0x80U) != 0 &&
                u32(notification.bytes, order, 4) == 0x41 &&
                u32(notification.bytes, order, 8) == 0x51 &&
                u32(notification.bytes, order, 16) == 0x01020304 &&
                u32(notification.bytes, order, 20) == 0x05060708 &&
                u32(notification.bytes, order, 24) == 0x11121314 &&
                u32(notification.bytes, order, 28) == 0x15161718,
            "VrrNotify has fixed size, stable fields, and effective detail bit");
  }
  require(gw_vrr_notifications(order, 1, 0x41, after, after,
                               OutputVrrPolicyMode::AppRequested)
              .empty(),
          "unchanged committed state emits no event");
}

}  // namespace

int main() {
  test_store_and_changes();
  test_notifications(x11::ByteOrder::LittleEndian);
  test_notifications(x11::ByteOrder::BigEndian);
  return 0;
}
