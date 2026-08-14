use std::collections::HashMap;

const MAXIMUM_XFIXES_SUBSCRIPTIONS: usize = 4096;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct SelectionOwner {
    pub client: u64,
    pub window: u32,
    pub last_change_time: u32,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct XFixesSelectionSubscription {
    pub client: u64,
    pub window: u32,
    pub selection: u32,
    pub mask: u32,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct XFixesSelectionNotification {
    pub client: u64,
    pub subtype: u8,
    pub window: u32,
    pub owner: u32,
    pub selection: u32,
    pub timestamp: u32,
    pub selection_timestamp: u32,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum SelectionOwnershipStatus {
    Applied,
    IgnoredStaleTime,
    InvalidSelection,
    InvalidOwnerWindow,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct SelectionOwnershipChange {
    pub status: SelectionOwnershipStatus,
    pub effective_time: u32,
    pub previous_owner: Option<SelectionOwner>,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum SelectionConversionKind {
    NotifyNoOwner,
    ForwardToOwner,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct SelectionConversion {
    pub kind: SelectionConversionKind,
    pub requestor_client: u64,
    pub requestor_window: u32,
    pub selection: u32,
    pub target: u32,
    pub property: u32,
    pub time: u32,
    pub owner: Option<SelectionOwner>,
}

#[derive(Clone, Debug, Default)]
pub struct SelectionStore {
    owners: HashMap<u32, SelectionOwner>,
    xfixes_subscriptions: Vec<XFixesSelectionSubscription>,
}

impl SelectionStore {
    pub fn set_owner(
        &mut self,
        client: u64,
        selection: u32,
        owner_window: u32,
        owner_window_exists: bool,
        request_time: u32,
        server_time: u32,
    ) -> SelectionOwnershipChange {
        let effective_time = if request_time == 0 {
            server_time
        } else {
            request_time
        };
        let mut result = SelectionOwnershipChange {
            status: SelectionOwnershipStatus::Applied,
            effective_time,
            previous_owner: None,
        };
        if selection == 0 {
            result.status = SelectionOwnershipStatus::InvalidSelection;
            return result;
        }
        if owner_window != 0 && !owner_window_exists {
            result.status = SelectionOwnershipStatus::InvalidOwnerWindow;
            return result;
        }
        let current = self.owners.get(&selection).copied();
        let last_change = current.map_or(0, |owner| owner.last_change_time);
        if (last_change != 0 && later(last_change, effective_time))
            || later(effective_time, server_time)
        {
            result.status = SelectionOwnershipStatus::IgnoredStaleTime;
            return result;
        }
        result.previous_owner = current;
        if owner_window == 0 {
            self.owners.remove(&selection);
        } else {
            self.owners.insert(
                selection,
                SelectionOwner {
                    client,
                    window: owner_window,
                    last_change_time: effective_time,
                },
            );
        }
        result
    }

    pub fn owner(&self, selection: u32) -> Option<SelectionOwner> {
        self.owners.get(&selection).copied()
    }

    #[allow(clippy::too_many_arguments)]
    pub fn convert(
        &self,
        requestor_client: u64,
        requestor_window: u32,
        selection: u32,
        target: u32,
        property: u32,
        request_time: u32,
        server_time: u32,
    ) -> SelectionConversion {
        let owner = self.owner(selection);
        SelectionConversion {
            kind: if owner.is_some() {
                SelectionConversionKind::ForwardToOwner
            } else {
                SelectionConversionKind::NotifyNoOwner
            },
            requestor_client,
            requestor_window,
            selection,
            target,
            property: if owner.is_some() { property } else { 0 },
            time: if request_time == 0 {
                server_time
            } else {
                request_time
            },
            owner,
        }
    }

    pub fn clear_window(&mut self, window: u32) -> Vec<u32> {
        let mut cleared = Vec::new();
        self.owners.retain(|selection, owner| {
            if owner.window == window {
                cleared.push(*selection);
                false
            } else {
                true
            }
        });
        cleared.sort_unstable();
        self.xfixes_subscriptions
            .retain(|subscription| subscription.window != window);
        cleared
    }

    pub fn clear_client(&mut self, client: u64) -> Vec<u32> {
        let mut cleared = Vec::new();
        self.owners.retain(|selection, owner| {
            if owner.client == client {
                cleared.push(*selection);
                false
            } else {
                true
            }
        });
        cleared.sort_unstable();
        self.xfixes_subscriptions
            .retain(|subscription| subscription.client != client);
        cleared
    }

    pub fn select_xfixes(&mut self, client: u64, window: u32, selection: u32, mask: u32) -> bool {
        let found = self.xfixes_subscriptions.iter().position(|subscription| {
            subscription.client == client
                && subscription.window == window
                && subscription.selection == selection
        });
        if mask == 0 {
            if let Some(index) = found {
                self.xfixes_subscriptions.remove(index);
            }
            return true;
        }
        if let Some(index) = found {
            self.xfixes_subscriptions[index].mask = mask;
            return true;
        }
        if self.xfixes_subscriptions.len() >= MAXIMUM_XFIXES_SUBSCRIPTIONS
            || self.xfixes_subscriptions.try_reserve(1).is_err()
        {
            return false;
        }
        self.xfixes_subscriptions.push(XFixesSelectionSubscription {
            client,
            window,
            selection,
            mask,
        });
        true
    }

    pub fn xfixes_notifications(
        &self,
        selection: u32,
        subtype: u8,
        owner: u32,
        timestamp: u32,
        selection_timestamp: u32,
    ) -> Vec<XFixesSelectionNotification> {
        let bit = 1u32.checked_shl(u32::from(subtype)).unwrap_or(0);
        let mut result: Vec<_> = self
            .xfixes_subscriptions
            .iter()
            .filter(|subscription| {
                subscription.selection == selection && subscription.mask & bit != 0
            })
            .map(|subscription| XFixesSelectionNotification {
                client: subscription.client,
                subtype,
                window: subscription.window,
                owner,
                selection,
                timestamp,
                selection_timestamp,
            })
            .collect();
        result.sort_unstable_by_key(|notification| notification.client);
        result
    }

    pub fn owned_by_client(&self, client: u64) -> Vec<(u32, SelectionOwner)> {
        let mut result: Vec<_> = self
            .owners
            .iter()
            .filter(|(_, owner)| owner.client == client)
            .map(|(selection, owner)| (*selection, *owner))
            .collect();
        result.sort_unstable_by_key(|entry| entry.0);
        result
    }

    pub fn owned_by_window(&self, window: u32) -> Vec<(u32, SelectionOwner)> {
        let mut result: Vec<_> = self
            .owners
            .iter()
            .filter(|(_, owner)| owner.window == window)
            .map(|(selection, owner)| (*selection, *owner))
            .collect();
        result.sort_unstable_by_key(|entry| entry.0);
        result
    }

    pub fn len(&self) -> usize {
        self.owners.len()
    }

    pub fn is_empty(&self) -> bool {
        self.owners.is_empty()
    }
}

fn later(lhs: u32, rhs: u32) -> bool {
    lhs.wrapping_sub(rhs) as i32 > 0
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn ownership_conversion_cleanup_and_wrap_match_legacy() {
        let primary = 1;
        let clipboard = 100;
        let mut store = SelectionStore::default();

        let mut change = store.set_owner(10, primary, 20, true, 0, 1000);
        assert_eq!(change.status, SelectionOwnershipStatus::Applied);
        assert_eq!(change.effective_time, 1000);
        assert_eq!(
            store.owner(primary),
            Some(SelectionOwner {
                client: 10,
                window: 20,
                last_change_time: 1000
            })
        );

        change = store.set_owner(11, primary, 21, true, 999, 1001);
        assert_eq!(change.status, SelectionOwnershipStatus::IgnoredStaleTime);
        change = store.set_owner(11, primary, 21, true, 1002, 1001);
        assert_eq!(change.status, SelectionOwnershipStatus::IgnoredStaleTime);
        change = store.set_owner(11, primary, 21, true, 1001, 1001);
        assert_eq!(change.status, SelectionOwnershipStatus::Applied);
        assert_eq!(change.previous_owner.unwrap().client, 10);

        assert_eq!(
            store.set_owner(10, 0, 20, true, 0, 1002).status,
            SelectionOwnershipStatus::InvalidSelection
        );
        assert_eq!(
            store.set_owner(10, clipboard, 99, false, 0, 1002).status,
            SelectionOwnershipStatus::InvalidOwnerWindow
        );

        let conversion = store.convert(12, 30, primary, 200, 201, 0, 1003);
        assert_eq!(conversion.kind, SelectionConversionKind::ForwardToOwner);
        assert_eq!(conversion.property, 201);
        assert_eq!(conversion.time, 1003);
        let absent = store.convert(12, 30, clipboard, 200, 201, 1004, 1004);
        assert_eq!(absent.kind, SelectionConversionKind::NotifyNoOwner);
        assert_eq!(absent.property, 0);

        assert_eq!(
            store.set_owner(10, clipboard, 20, true, 0, 1005).status,
            SelectionOwnershipStatus::Applied
        );
        assert_eq!(store.clear_window(20), vec![clipboard]);
        assert_eq!(
            store.set_owner(11, clipboard, 22, true, 0, 1006).status,
            SelectionOwnershipStatus::Applied
        );
        assert_eq!(store.clear_client(11), vec![primary, clipboard]);
        assert!(store.is_empty());

        let near_wrap = u32::MAX - 2;
        assert_eq!(
            store
                .set_owner(1, primary, 2, true, near_wrap, near_wrap)
                .status,
            SelectionOwnershipStatus::Applied
        );
        assert_eq!(
            store.set_owner(1, primary, 2, true, 1, 1).status,
            SelectionOwnershipStatus::Applied
        );
    }

    #[test]
    fn xfixes_subscriptions_update_remove_sort_and_clear() {
        let mut store = SelectionStore::default();
        assert!(store.select_xfixes(2, 20, 1, 1 << 1));
        assert!(store.select_xfixes(1, 10, 1, 1 << 1));
        assert!(store.select_xfixes(1, 11, 2, 1 << 1));
        assert_eq!(
            store.xfixes_notifications(1, 1, 30, 40, 50),
            vec![
                XFixesSelectionNotification {
                    client: 1,
                    subtype: 1,
                    window: 10,
                    owner: 30,
                    selection: 1,
                    timestamp: 40,
                    selection_timestamp: 50
                },
                XFixesSelectionNotification {
                    client: 2,
                    subtype: 1,
                    window: 20,
                    owner: 30,
                    selection: 1,
                    timestamp: 40,
                    selection_timestamp: 50
                },
            ]
        );
        assert!(store.select_xfixes(1, 10, 1, 1 << 2));
        assert!(
            store
                .xfixes_notifications(1, 1, 0, 0, 0)
                .iter()
                .all(|n| n.client != 1)
        );
        assert!(store.select_xfixes(1, 10, 1, 0));
        store.clear_window(20);
        assert!(store.xfixes_notifications(1, 1, 0, 0, 0).is_empty());
        store.clear_client(1);
        assert!(store.xfixes_notifications(2, 1, 0, 0, 0).is_empty());
    }

    #[test]
    fn xfixes_subscription_limit_matches_legacy() {
        let mut store = SelectionStore::default();
        for client in 0..MAXIMUM_XFIXES_SUBSCRIPTIONS as u64 {
            assert!(store.select_xfixes(client, client as u32, 1, 1));
        }
        assert!(!store.select_xfixes(u64::MAX, u32::MAX, 1, 1));
        assert!(store.select_xfixes(0, 0, 1, 2));
    }
}
