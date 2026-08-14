use std::collections::BTreeMap;
use std::fmt;

use gw_types::{MessageFlags, MessageType, Sequence, SnapshotDomain};
use gw_wire::compositor::{OutputUpsert, decode_output_upsert, decode_surface_upsert};
use gw_wire::vrr::{
    OutputVrrCapabilityUpsert, OutputVrrStateUpsert, PresentationTiming, SurfaceVrrState,
    VrrPolicyMode, decode_output_vrr_capability_upsert, decode_output_vrr_policy_upsert,
    decode_output_vrr_state_upsert, decode_presentation_timing, decode_surface_vrr_state,
};
use gw_wire::{
    OutputConfigurationResult, OutputDescriptorUpsert, OutputModeUpsert, SurfaceScaleMode,
    decode_output_configuration_acknowledged, decode_output_descriptor_upsert,
    decode_output_mode_upsert, decode_snapshot_begin, decode_snapshot_end,
    decode_surface_output_state, decode_surface_policy_upsert,
};

// Query flags are part of the stable M13 output-control contract.
pub(crate) const OUTPUT_QUERY_FLAGS: u32 = (1 << 0) | (1 << 1) | (1 << 2);
pub(crate) const OUTPUT_QUERY_WINDOWS: u32 = 1 << 3;
pub(crate) const OUTPUT_QUERY_VRR: u32 = 1 << 4;
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
    pub windows: BTreeMap<u32, WindowSnapshot>,
    pub vrr_capabilities: BTreeMap<u64, OutputVrrCapabilityUpsert>,
    pub vrr_policies: BTreeMap<u64, VrrPolicyMode>,
    pub vrr_outputs: BTreeMap<u64, OutputVrrStateUpsert>,
    pub vrr_windows: BTreeMap<u32, SurfaceVrrState>,
    pub vrr_timings: BTreeMap<u64, PresentationTiming>,
    pub vrr_queried: bool,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct WindowSnapshot {
    pub surface_id: u64,
    pub window_id: u32,
    pub logical_x: i32,
    pub logical_y: i32,
    pub logical_width: u32,
    pub logical_height: u32,
    pub primary_output_id: u64,
    pub output_ids: Vec<u64>,
    pub preferred_scale_numerator: u32,
    pub preferred_scale_denominator: u32,
    pub client_buffer_scale: u32,
    pub scale_mode: SurfaceScaleMode,
    pub visible: bool,
    pub focused: bool,
    pub fullscreen: bool,
}

impl WindowSnapshot {
    fn new(surface_id: u64, window_id: u32) -> Self {
        Self {
            surface_id,
            window_id,
            logical_x: 0,
            logical_y: 0,
            logical_width: 0,
            logical_height: 0,
            primary_output_id: 0,
            output_ids: Vec::new(),
            preferred_scale_numerator: 1,
            preferred_scale_denominator: 1,
            client_buffer_scale: 1,
            scale_mode: SurfaceScaleMode::Legacy,
            visible: false,
            focused: false,
            fullscreen: false,
        }
    }
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
    pub(crate) fn new(request_id: u64, request_sequence: Sequence, query_flags: u32) -> Self {
        Self {
            request_id,
            request_sequence,
            snapshot_id: 0,
            expected_items: 0,
            actual_items: 0,
            reading: false,
            ended: false,
            acknowledged: false,
            snapshot: OutputSnapshot {
                vrr_queried: query_flags & OUTPUT_QUERY_VRR != 0,
                ..OutputSnapshot::default()
            },
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
        if matches!(
            record.envelope.message_type,
            MessageType::OUTPUT_VRR_CAPABILITY_UPSERT
                | MessageType::OUTPUT_VRR_POLICY_UPSERT
                | MessageType::OUTPUT_VRR_STATE_UPSERT
                | MessageType::SURFACE_VRR_STATE
                | MessageType::PRESENTATION_TIMING
        ) && !self.snapshot.vrr_queried
        {
            return Err(SnapshotError::new(
                "control server sent unrequested VRR state",
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
            MessageType::SURFACE_UPSERT => {
                let value = decode_surface_upsert(&record.payload).map_err(|_| {
                    SnapshotError::new("control server sent a malformed window record")
                })?;
                if value.x11_window_id == 0 {
                    return Err(SnapshotError::new(
                        "window snapshot contains an invalid surface",
                    ));
                }
                let window = self
                    .snapshot
                    .windows
                    .entry(value.x11_window_id)
                    .or_insert_with(|| WindowSnapshot::new(value.surface_id, value.x11_window_id));
                window.surface_id = value.surface_id;
                window.logical_x = value.logical_x;
                window.logical_y = value.logical_y;
                window.logical_width = value.logical_width;
                window.logical_height = value.logical_height;
                window.primary_output_id = value.output_id;
                window.visible = value.visible;
            }
            MessageType::SURFACE_POLICY_UPSERT => {
                let value = decode_surface_policy_upsert(&record.payload).map_err(|_| {
                    SnapshotError::new("control server sent a malformed window record")
                })?;
                if value.x11_window_id == 0 {
                    return Err(SnapshotError::new(
                        "window snapshot contains invalid policy state",
                    ));
                }
                let window = self
                    .snapshot
                    .windows
                    .entry(value.x11_window_id)
                    .or_insert_with(|| WindowSnapshot::new(value.surface_id, value.x11_window_id));
                window.surface_id = value.surface_id;
                window.focused = value.focused;
                window.fullscreen = value.applied_state == gw_wire::PolicyAppliedState::Fullscreen;
            }
            MessageType::SURFACE_OUTPUT_STATE => {
                let value = decode_surface_output_state(&record.payload).map_err(|_| {
                    SnapshotError::new("control server sent a malformed window record")
                })?;
                let window = self
                    .snapshot
                    .windows
                    .values_mut()
                    .find(|window| window.surface_id == value.surface_id)
                    .ok_or_else(|| {
                        SnapshotError::new("window membership precedes its surface record")
                    })?;
                window.primary_output_id = value.primary_output_id;
                window.output_ids = value.output_ids;
                window.preferred_scale_numerator = value.preferred_scale_numerator;
                window.preferred_scale_denominator = value.preferred_scale_denominator;
                window.client_buffer_scale = value.client_buffer_scale;
                window.scale_mode = value.scale_mode;
            }
            MessageType::OUTPUT_VRR_CAPABILITY_UPSERT => {
                let value = decode_output_vrr_capability_upsert(&record.payload).map_err(|_| {
                    SnapshotError::new("control server sent a malformed VRR record")
                })?;
                if self
                    .snapshot
                    .vrr_capabilities
                    .insert(value.output_id, value)
                    .is_some()
                {
                    return Err(SnapshotError::new(
                        "VRR snapshot contains duplicate capability state",
                    ));
                }
            }
            MessageType::OUTPUT_VRR_POLICY_UPSERT => {
                let value = decode_output_vrr_policy_upsert(&record.payload).map_err(|_| {
                    SnapshotError::new("control server sent a malformed VRR record")
                })?;
                if self
                    .snapshot
                    .vrr_policies
                    .insert(value.output_id, value.mode)
                    .is_some()
                {
                    return Err(SnapshotError::new(
                        "VRR snapshot contains duplicate output policy",
                    ));
                }
            }
            MessageType::OUTPUT_VRR_STATE_UPSERT => {
                let value = decode_output_vrr_state_upsert(&record.payload).map_err(|_| {
                    SnapshotError::new("control server sent a malformed VRR record")
                })?;
                if self
                    .snapshot
                    .vrr_outputs
                    .insert(value.output_id, value)
                    .is_some()
                {
                    return Err(SnapshotError::new(
                        "VRR snapshot contains duplicate effective state",
                    ));
                }
            }
            MessageType::SURFACE_VRR_STATE => {
                let value = decode_surface_vrr_state(&record.payload).map_err(|_| {
                    SnapshotError::new("control server sent a malformed VRR record")
                })?;
                if self
                    .snapshot
                    .vrr_windows
                    .insert(value.window_id, value)
                    .is_some()
                {
                    return Err(SnapshotError::new(
                        "VRR snapshot contains duplicate window state",
                    ));
                }
            }
            MessageType::PRESENTATION_TIMING => {
                let value = decode_presentation_timing(&record.payload).map_err(|_| {
                    SnapshotError::new("control server sent a malformed VRR record")
                })?;
                if self
                    .snapshot
                    .vrr_timings
                    .insert(value.output_id, value)
                    .is_some()
                {
                    return Err(SnapshotError::new(
                        "VRR snapshot contains duplicate timing state",
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
        let mut decoder = SnapshotDecoder::new(1, Sequence::new(2), OUTPUT_QUERY_FLAGS);
        assert_eq!(
            decoder.consume(&acknowledgement(OutputConfigurationResult::Busy)),
            Ok(ConsumeOutcome::Busy)
        );

        let mut decoder = SnapshotDecoder::new(1, Sequence::new(2), OUTPUT_QUERY_FLAGS);
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
            SnapshotDecoder::new(1, Sequence::new(2), OUTPUT_QUERY_FLAGS)
                .consume(&invalid)
                .unwrap_err()
                .to_string(),
            "control server rejected the output query"
        );
    }
}
