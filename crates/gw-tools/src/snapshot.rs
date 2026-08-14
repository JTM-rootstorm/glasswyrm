use std::collections::BTreeMap;
use std::fmt;

use gw_types::{MessageFlags, MessageType, Sequence, SnapshotDomain};
use gw_wire::compositor::{OutputUpsert, decode_output_upsert};
use gw_wire::{
    OutputConfigurationResult, OutputDescriptorUpsert, OutputModeUpsert,
    decode_output_configuration_acknowledged, decode_output_descriptor_upsert,
    decode_output_mode_upsert, decode_snapshot_begin, decode_snapshot_end,
};

// Query flags are part of the stable M13 output-control contract.
pub(crate) const OUTPUT_QUERY_FLAGS: u32 = (1 << 0) | (1 << 1) | (1 << 2);
const MAXIMUM_SNAPSHOT_ITEMS: u32 = u16::MAX as u32;

#[derive(Clone, Debug, Default, Eq, PartialEq)]
pub struct OutputSnapshot {
    pub generation: u64,
    pub primary_output_id: u64,
    pub root_width: u32,
    pub root_height: u32,
    pub enabled_output_count: u32,
    pub descriptors: BTreeMap<u64, OutputDescriptorUpsert>,
    pub modes: Vec<OutputModeUpsert>,
    pub outputs: BTreeMap<u64, OutputUpsert>,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub(crate) enum ConsumeOutcome {
    Pending,
    Complete,
    Busy,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SnapshotError(&'static str);

impl SnapshotError {
    pub(crate) const fn new(detail: &'static str) -> Self {
        Self(detail)
    }
}

impl fmt::Display for SnapshotError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(self.0)
    }
}

impl std::error::Error for SnapshotError {}

pub(crate) struct SnapshotDecoder {
    request_id: u64,
    request_sequence: Sequence,
    snapshot_id: u64,
    expected_items: u32,
    actual_items: u32,
    reading: bool,
    ended: bool,
    acknowledged: bool,
    snapshot: OutputSnapshot,
}

impl SnapshotDecoder {
    pub(crate) fn new(request_id: u64, request_sequence: Sequence) -> Self {
        Self {
            request_id,
            request_sequence,
            snapshot_id: 0,
            expected_items: 0,
            actual_items: 0,
            reading: false,
            ended: false,
            acknowledged: false,
            snapshot: OutputSnapshot::default(),
        }
    }

    pub(crate) fn consume(
        &mut self,
        record: &gw_ipc::ReceivedRecord,
    ) -> Result<ConsumeOutcome, SnapshotError> {
        let message_type = record.envelope.message_type;
        if message_type == MessageType::SNAPSHOT_BEGIN {
            return self.consume_begin(record);
        }
        if message_type == MessageType::SNAPSHOT_END {
            return self.consume_end(record);
        }
        if message_type == MessageType::SNAPSHOT_ABORT {
            return Err(SnapshotError::new(
                "control server aborted the output snapshot",
            ));
        }
        if message_type == MessageType::OUTPUT_CONFIGURATION_ACKNOWLEDGED {
            return self.consume_acknowledgement(record);
        }
        self.consume_item(record)
    }

    pub(crate) fn take(self) -> OutputSnapshot {
        self.snapshot
    }

    fn consume_begin(
        &mut self,
        record: &gw_ipc::ReceivedRecord,
    ) -> Result<ConsumeOutcome, SnapshotError> {
        let begin = decode_snapshot_begin(&record.payload)
            .map_err(|_| SnapshotError::new("control server sent malformed snapshot framing"))?;
        if record.envelope.flags.bits() != 0
            || self.reading
            || self.ended
            || begin.domain != SnapshotDomain::Outputs
            || begin.generation.get() == 0
            || begin.expected_item_count > MAXIMUM_SNAPSHOT_ITEMS
        {
            return Err(SnapshotError::new(
                "control server sent an invalid output snapshot begin",
            ));
        }
        self.snapshot_id = begin.snapshot_id.get();
        self.snapshot.generation = begin.generation.get();
        self.expected_items = begin.expected_item_count;
        self.reading = true;
        Ok(ConsumeOutcome::Pending)
    }

    fn consume_end(
        &mut self,
        record: &gw_ipc::ReceivedRecord,
    ) -> Result<ConsumeOutcome, SnapshotError> {
        let end = decode_snapshot_end(&record.payload)
            .map_err(|_| SnapshotError::new("control server sent malformed snapshot framing"))?;
        if record.envelope.flags.bits() != 0
            || !self.reading
            || self.ended
            || end.snapshot_id.get() != self.snapshot_id
            || end.generation.get() != self.snapshot.generation
            || end.actual_item_count != self.actual_items
            || end.actual_item_count != self.expected_items
        {
            return Err(SnapshotError::new(
                "control server sent an incomplete output snapshot",
            ));
        }
        self.reading = false;
        self.ended = true;
        Ok(if self.acknowledged {
            ConsumeOutcome::Complete
        } else {
            ConsumeOutcome::Pending
        })
    }

    fn consume_acknowledgement(
        &mut self,
        record: &gw_ipc::ReceivedRecord,
    ) -> Result<ConsumeOutcome, SnapshotError> {
        let acknowledgement = decode_output_configuration_acknowledged(&record.payload)
            .map_err(|_| SnapshotError::new("control server sent a malformed output record"))?;
        if record.envelope.flags != MessageFlags::REPLY
            || record.envelope.reply_to != self.request_sequence
            || acknowledgement.request_id != self.request_id
        {
            return Err(SnapshotError::new(
                "control server rejected the output query",
            ));
        }
        if acknowledgement.result == OutputConfigurationResult::Busy {
            if self.reading || self.ended || self.acknowledged {
                return Err(SnapshotError::new(
                    "control server sent BUSY after starting an output snapshot",
                ));
            }
            return Ok(ConsumeOutcome::Busy);
        }
        if acknowledgement.result != OutputConfigurationResult::Accepted
            || !self.ended
            || acknowledgement.applied_generation != self.snapshot.generation
        {
            return Err(SnapshotError::new(
                "control server rejected the output query",
            ));
        }
        self.snapshot.primary_output_id = acknowledgement.primary_output_id;
        self.snapshot.root_width = acknowledgement.root_logical_width;
        self.snapshot.root_height = acknowledgement.root_logical_height;
        self.snapshot.enabled_output_count = acknowledgement.enabled_output_count;
        self.acknowledged = true;
        Ok(ConsumeOutcome::Complete)
    }

    fn consume_item(
        &mut self,
        record: &gw_ipc::ReceivedRecord,
    ) -> Result<ConsumeOutcome, SnapshotError> {
        if !self.reading || record.envelope.flags != MessageFlags::SNAPSHOT_ITEM {
            return Err(SnapshotError::new(
                "control server sent an output item outside a snapshot",
            ));
        }
        self.actual_items = self.actual_items.checked_add(1).ok_or_else(|| {
            SnapshotError::new("control server exceeded its output snapshot count")
        })?;
        if self.actual_items > self.expected_items {
            return Err(SnapshotError::new(
                "control server exceeded its output snapshot count",
            ));
        }
        match record.envelope.message_type {
            MessageType::OUTPUT_DESCRIPTOR_UPSERT => {
                let value = decode_output_descriptor_upsert(&record.payload).map_err(|_| {
                    SnapshotError::new("control server sent a malformed output record")
                })?;
                if self
                    .snapshot
                    .descriptors
                    .insert(value.output_id, value)
                    .is_some()
                {
                    return Err(SnapshotError::new(
                        "output snapshot contains a duplicate descriptor",
                    ));
                }
            }
            MessageType::OUTPUT_MODE_UPSERT => {
                let value = decode_output_mode_upsert(&record.payload)
                    .map_err(|_| SnapshotError::new("output snapshot contains an invalid mode"))?;
                self.snapshot.modes.push(value);
            }
            MessageType::OUTPUT_UPSERT => {
                let value = decode_output_upsert(&record.payload).map_err(|_| {
                    SnapshotError::new("control server sent a malformed output record")
                })?;
                if self
                    .snapshot
                    .outputs
                    .insert(value.output_id, value)
                    .is_some()
                {
                    return Err(SnapshotError::new(
                        "output snapshot contains duplicate layout state",
                    ));
                }
            }
            _ => {
                return Err(SnapshotError::new(
                    "control server sent an unexpected snapshot item",
                ));
            }
        }
        Ok(ConsumeOutcome::Pending)
    }
}

#[cfg(test)]
mod tests {
    use gw_ipc::ReceivedRecord;
    use gw_types::{Generation, SnapshotId};
    use gw_wire::{
        Envelope, OutputConfigurationAcknowledged, SnapshotBegin,
        encode_output_configuration_acknowledged, encode_snapshot_begin,
    };

    use super::*;

    fn record(
        message_type: MessageType,
        sequence: u64,
        flags: MessageFlags,
        reply_to: u64,
        payload: Vec<u8>,
    ) -> ReceivedRecord {
        let mut envelope =
            Envelope::request(message_type, Sequence::new(sequence), payload.len() as u32);
        envelope.flags = flags;
        envelope.reply_to = Sequence::new(reply_to);
        ReceivedRecord {
            envelope,
            payload,
            fds: Vec::new(),
        }
    }

    fn acknowledgement(result: OutputConfigurationResult) -> ReceivedRecord {
        record(
            MessageType::OUTPUT_CONFIGURATION_ACKNOWLEDGED,
            2,
            MessageFlags::REPLY,
            2,
            encode_output_configuration_acknowledged(&OutputConfigurationAcknowledged {
                request_id: 1,
                applied_generation: 1,
                result,
                flags: 0,
                primary_output_id: 11,
                root_logical_width: 640,
                root_logical_height: 480,
                enabled_output_count: 1,
            }),
        )
    }

    #[test]
    fn busy_is_typed_only_before_snapshot_framing() {
        let mut decoder = SnapshotDecoder::new(1, Sequence::new(2));
        assert_eq!(
            decoder.consume(&acknowledgement(OutputConfigurationResult::Busy)),
            Ok(ConsumeOutcome::Busy)
        );

        let mut decoder = SnapshotDecoder::new(1, Sequence::new(2));
        let begin = record(
            MessageType::SNAPSHOT_BEGIN,
            2,
            MessageFlags::default(),
            0,
            encode_snapshot_begin(SnapshotBegin {
                snapshot_id: SnapshotId::new(91),
                domain: SnapshotDomain::Outputs,
                flags: 0,
                generation: Generation::new(1),
                expected_item_count: 0,
            }),
        );
        assert_eq!(decoder.consume(&begin), Ok(ConsumeOutcome::Pending));
        assert_eq!(
            decoder
                .consume(&acknowledgement(OutputConfigurationResult::Busy))
                .unwrap_err()
                .to_string(),
            "control server sent BUSY after starting an output snapshot"
        );
    }

    #[test]
    fn acknowledgements_must_reply_to_the_query_sequence() {
        let mut invalid = acknowledgement(OutputConfigurationResult::Busy);
        invalid.envelope.reply_to = Sequence::new(9);
        assert_eq!(
            SnapshotDecoder::new(1, Sequence::new(2))
                .consume(&invalid)
                .unwrap_err()
                .to_string(),
            "control server rejected the output query"
        );
    }
}
