#include "glasswyrmd/selection_store.hpp"

#include "helpers/test_support.hpp"

#include <cstdint>
#include <vector>

using glasswyrm::server::SelectionConversionKind;
using glasswyrm::server::SelectionOwnershipStatus;
using glasswyrm::server::SelectionOwner;
using glasswyrm::server::SelectionStore;
using gw::test::require;

int main() {
  constexpr std::uint32_t primary = 1;
  constexpr std::uint32_t clipboard = 100;
  SelectionStore store;

  auto change = store.set_owner(10, primary, 20, true, 0, 1000);
  require(change.status == SelectionOwnershipStatus::Applied &&
              change.effective_time == 1000 && !change.previous_owner &&
              store.owner(primary) == SelectionOwner{10, 20, 1000},
          "CurrentTime ownership uses server logical time");
  change = store.set_owner(11, primary, 21, true, 999, 1001);
  require(change.status == SelectionOwnershipStatus::IgnoredStaleTime &&
              store.owner(primary) == SelectionOwner{10, 20, 1000},
          "ownership changes older than the last change are ignored");
  change = store.set_owner(11, primary, 21, true, 1002, 1001);
  require(change.status == SelectionOwnershipStatus::IgnoredStaleTime,
          "ownership changes newer than server time are ignored");
  change = store.set_owner(11, primary, 21, true, 1001, 1001);
  require(change.status == SelectionOwnershipStatus::Applied &&
              change.previous_owner == SelectionOwner{10, 20, 1000} &&
              store.owner(primary) == SelectionOwner{11, 21, 1001},
          "replacement reports the previous owner for SelectionClear");

  require(store.set_owner(10, 0, 20, true, 0, 1002).status ==
              SelectionOwnershipStatus::InvalidSelection &&
              store.set_owner(10, clipboard, 99, false, 0, 1002).status ==
                  SelectionOwnershipStatus::InvalidOwnerWindow,
          "selection atoms and owner windows are validated");

  auto conversion = store.convert(12, 30, primary, 200, 201, 0, 1003);
  require(conversion.kind == SelectionConversionKind::ForwardToOwner &&
              conversion.owner == SelectionOwner{11, 21, 1001} &&
              conversion.requestor_client == 12 &&
              conversion.requestor_window == 30 && conversion.target == 200 &&
              conversion.property == 201 && conversion.time == 1003,
          "conversion forwards exact fields to the current owner");
  conversion = store.convert(12, 30, clipboard, 200, 201, 1004, 1004);
  require(conversion.kind == SelectionConversionKind::NotifyNoOwner &&
              !conversion.owner && conversion.property == 0,
          "absent ownership produces SelectionNotify with property None");

  require(store.set_owner(10, clipboard, 20, true, 0, 1005).status ==
              SelectionOwnershipStatus::Applied &&
              store.clear_window(20) ==
                  std::vector<std::uint32_t>{clipboard} &&
              !store.owner(clipboard) && store.owner(primary),
          "window destruction clears only selections owned by that window");
  require(store.set_owner(11, clipboard, 22, true, 0, 1006).status ==
              SelectionOwnershipStatus::Applied &&
              store.clear_client(11) ==
                  std::vector<std::uint32_t>{primary, clipboard} &&
              store.size() == 0,
          "client cleanup clears every selection owned by that client");

  constexpr std::uint32_t near_wrap = UINT32_MAX - 2U;
  require(store.set_owner(1, primary, 2, true, near_wrap, near_wrap).status ==
              SelectionOwnershipStatus::Applied &&
              store.set_owner(1, primary, 2, true, 1, 1).status ==
                  SelectionOwnershipStatus::Applied,
          "timestamp ordering follows X11 32-bit wrap semantics");
}
