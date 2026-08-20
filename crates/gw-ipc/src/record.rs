//! Transport-neutral GWIPC record and attachment vocabulary.
//!
//! The wire envelope and payload are protocol data. Attachments describe the
//! semantic objects carried beside that data; a Linux adapter may represent
//! those objects as file descriptors, while another backend need not.

use gw_wire::Envelope;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum AttachmentKind {
    Handle,
    SharedBuffer,
    Synchronization,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct AttachmentDescriptor {
    pub kind: AttachmentKind,
    pub minimum_size: Option<u64>,
}

impl AttachmentDescriptor {
    #[must_use]
    pub const fn handle() -> Self {
        Self {
            kind: AttachmentKind::Handle,
            minimum_size: None,
        }
    }

    #[must_use]
    pub const fn shared_buffer(minimum_size: u64) -> Self {
        Self {
            kind: AttachmentKind::SharedBuffer,
            minimum_size: Some(minimum_size),
        }
    }

    #[must_use]
    pub const fn synchronization() -> Self {
        Self {
            kind: AttachmentKind::Synchronization,
            minimum_size: None,
        }
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct Record {
    pub envelope: Envelope,
    pub payload: Vec<u8>,
    pub attachments: Vec<AttachmentDescriptor>,
}

impl Record {
    #[must_use]
    pub fn new(
        envelope: Envelope,
        payload: Vec<u8>,
        attachments: Vec<AttachmentDescriptor>,
    ) -> Self {
        Self {
            envelope,
            payload,
            attachments,
        }
    }

    #[must_use]
    pub fn without_attachments(envelope: Envelope, payload: Vec<u8>) -> Self {
        Self::new(envelope, payload, Vec::new())
    }
}

/// Read-only protocol view implemented by both native transport records and
/// in-memory records. State machines consume this interface rather than an
/// operating-system handle representation.
pub trait RecordSource {
    fn envelope(&self) -> &Envelope;
    fn payload(&self) -> &[u8];
    fn attachment_count(&self) -> usize;
}

impl RecordSource for Record {
    fn envelope(&self) -> &Envelope {
        &self.envelope
    }
    fn payload(&self) -> &[u8] {
        &self.payload
    }
    fn attachment_count(&self) -> usize {
        self.attachments.len()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use gw_types::{MessageType, Sequence};

    #[test]
    fn in_memory_record_exposes_semantic_attachments_without_native_handles() {
        let payload = vec![1, 2, 3];
        let envelope = Envelope::request(MessageType::BUFFER_ATTACH, Sequence::new(7), 3);
        let record = Record::new(
            envelope,
            payload.clone(),
            vec![AttachmentDescriptor::shared_buffer(4096)],
        );
        assert_eq!(record.envelope(), &envelope);
        assert_eq!(record.payload(), payload);
        assert_eq!(record.attachment_count(), 1);
        assert_eq!(record.attachments[0].kind, AttachmentKind::SharedBuffer);
        assert_eq!(record.attachments[0].minimum_size, Some(4096));
    }
}
