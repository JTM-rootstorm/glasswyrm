use glasswyrm_x11::ByteOrder;
use std::collections::{BTreeMap, VecDeque};

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub(crate) struct QueueLimits {
    pub maximum_messages: usize,
    pub maximum_bytes: usize,
}

impl Default for QueueLimits {
    fn default() -> Self {
        Self {
            maximum_messages: 256,
            maximum_bytes: 1024 * 1024,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq, Ord, PartialOrd)]
pub(crate) struct QueueEndpoint(u64);

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub(crate) struct RecipientSnapshot {
    pub client_id: u64,
    pub byte_order: ByteOrder,
    pub sequence: u64,
    pub endpoint: QueueEndpoint,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub(crate) enum DeliveryResult {
    Enqueued,
    Disconnected,
    Saturated,
}

#[derive(Debug)]
struct ClientQueue {
    byte_order: ByteOrder,
    endpoint: QueueEndpoint,
    next_sequence: u64,
    packets: VecDeque<Vec<u8>>,
    queued_bytes: usize,
    connected: bool,
}

/// Prototype for the serialized recipient-snapshot and bounded-delivery model.
///
/// Canonical state chooses recipients and reserves their sequences first. The
/// returned snapshots are then safe to use after the state lock is released;
/// delivery never requires socket I/O or another process to make progress.
#[derive(Debug)]
pub(crate) struct ExecutionModel {
    clients: BTreeMap<u64, ClientQueue>,
    next_endpoint: u64,
    limits: QueueLimits,
}

impl ExecutionModel {
    pub(crate) fn new(limits: QueueLimits) -> Self {
        Self {
            clients: BTreeMap::new(),
            next_endpoint: 1,
            limits,
        }
    }

    pub(crate) fn register_client(&mut self, client_id: u64, byte_order: ByteOrder) -> bool {
        if self.clients.contains_key(&client_id) || self.next_endpoint == u64::MAX {
            return false;
        }
        let endpoint = QueueEndpoint(self.next_endpoint);
        self.next_endpoint += 1;
        self.clients.insert(
            client_id,
            ClientQueue {
                byte_order,
                endpoint,
                next_sequence: 1,
                packets: VecDeque::new(),
                queued_bytes: 0,
                connected: true,
            },
        );
        true
    }

    pub(crate) fn snapshot_recipients(
        &mut self,
        client_ids: impl IntoIterator<Item = u64>,
    ) -> Vec<RecipientSnapshot> {
        let mut snapshots = Vec::new();
        for client_id in client_ids {
            let Some(client) = self.clients.get_mut(&client_id) else {
                continue;
            };
            if !client.connected || client.next_sequence == u64::MAX {
                continue;
            }
            snapshots.push(RecipientSnapshot {
                client_id,
                byte_order: client.byte_order,
                sequence: client.next_sequence,
                endpoint: client.endpoint,
            });
            client.next_sequence += 1;
        }
        snapshots
    }

    pub(crate) fn deliver(
        &mut self,
        recipient: RecipientSnapshot,
        packet: Vec<u8>,
    ) -> DeliveryResult {
        let Some(client) = self.clients.get_mut(&recipient.client_id) else {
            return DeliveryResult::Disconnected;
        };
        if !client.connected || client.endpoint != recipient.endpoint {
            return DeliveryResult::Disconnected;
        }
        let exceeds_messages = client.packets.len() >= self.limits.maximum_messages;
        let exceeds_bytes = packet.len()
            > self
                .limits
                .maximum_bytes
                .saturating_sub(client.queued_bytes);
        if exceeds_messages || exceeds_bytes {
            client.connected = false;
            client.packets.clear();
            client.queued_bytes = 0;
            return DeliveryResult::Saturated;
        }
        client.queued_bytes += packet.len();
        client.packets.push_back(packet);
        DeliveryResult::Enqueued
    }

    pub(crate) fn disconnect_client(&mut self, client_id: u64) {
        if let Some(client) = self.clients.get_mut(&client_id) {
            client.connected = false;
            client.packets.clear();
            client.queued_bytes = 0;
        }
    }

    pub(crate) fn remove_client(&mut self, client_id: u64) -> bool {
        self.clients.remove(&client_id).is_some()
    }

    pub(crate) fn take_packet(&mut self, client_id: u64) -> Option<Vec<u8>> {
        let client = self.clients.get_mut(&client_id)?;
        let packet = client.packets.pop_front()?;
        client.queued_bytes -= packet.len();
        Some(packet)
    }

    #[cfg(test)]
    fn connected(&self, client_id: u64) -> bool {
        self.clients
            .get(&client_id)
            .is_some_and(|client| client.connected)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn packet(recipient: RecipientSnapshot) -> Vec<u8> {
        let sequence = match recipient.byte_order {
            ByteOrder::LittleEndian => recipient.sequence.to_le_bytes(),
            ByteOrder::BigEndian => recipient.sequence.to_be_bytes(),
        };
        let mut packet = vec![recipient.byte_order.marker()];
        packet.extend_from_slice(&sequence);
        packet
    }

    #[test]
    fn two_byte_orders_fan_out_from_one_serialized_snapshot() {
        let mut model = ExecutionModel::new(QueueLimits::default());
        assert!(model.register_client(11, ByteOrder::LittleEndian));
        assert!(model.register_client(22, ByteOrder::BigEndian));
        let recipients = model.snapshot_recipients([11, 22]);
        assert_eq!(recipients.len(), 2);
        for recipient in recipients {
            assert_eq!(
                model.deliver(recipient, packet(recipient)),
                DeliveryResult::Enqueued
            );
        }
        let little = model.take_packet(11).unwrap();
        let big = model.take_packet(22).unwrap();
        assert_eq!(little[0], ByteOrder::LittleEndian.marker());
        assert_eq!(big[0], ByteOrder::BigEndian.marker());
        assert_eq!(&little[1..], &1_u64.to_le_bytes());
        assert_eq!(&big[1..], &1_u64.to_be_bytes());
    }

    #[test]
    fn disconnect_between_snapshot_and_delivery_is_local_to_that_recipient() {
        let mut model = ExecutionModel::new(QueueLimits::default());
        model.register_client(1, ByteOrder::LittleEndian);
        model.register_client(2, ByteOrder::BigEndian);
        let recipients = model.snapshot_recipients([1, 2]);
        model.disconnect_client(1);
        assert_eq!(
            model.deliver(recipients[0], packet(recipients[0])),
            DeliveryResult::Disconnected
        );
        assert_eq!(
            model.deliver(recipients[1], packet(recipients[1])),
            DeliveryResult::Enqueued
        );
        assert!(model.take_packet(1).is_none());
        assert!(model.take_packet(2).is_some());
    }

    #[test]
    fn saturation_disconnects_only_the_slow_client_and_clears_retained_bytes() {
        let mut model = ExecutionModel::new(QueueLimits {
            maximum_messages: 1,
            maximum_bytes: 32,
        });
        model.register_client(1, ByteOrder::LittleEndian);
        model.register_client(2, ByteOrder::BigEndian);
        let first = model.snapshot_recipients([1, 2]);
        for recipient in first {
            assert_eq!(
                model.deliver(recipient, packet(recipient)),
                DeliveryResult::Enqueued
            );
        }
        assert!(model.take_packet(2).is_some());
        let second = model.snapshot_recipients([1, 2]);
        assert_eq!(
            model.deliver(second[0], packet(second[0])),
            DeliveryResult::Saturated
        );
        assert!(!model.connected(1));
        assert!(model.take_packet(1).is_none());
        assert_eq!(
            model.deliver(second[1], packet(second[1])),
            DeliveryResult::Enqueued
        );
        assert!(model.connected(2));
        assert!(model.remove_client(1));
        assert!(!model.remove_client(1));
    }
}
