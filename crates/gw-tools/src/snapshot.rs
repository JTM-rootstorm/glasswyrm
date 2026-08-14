use std::collections::{BTreeMap, BTreeSet};
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
const OUTPUT_QUERY_DESCRIPTORS: u32 = 1 << 0;
const OUTPUT_QUERY_MODES: u32 = 1 << 1;
const OUTPUT_QUERY_LAYOUT: u32 = 1 << 2;
const MAXIMUM_MODES_PER_OUTPUT: usize = 128;
const MAXIMUM_TOTAL_MODES: usize = gw_wire::MAXIMUM_MANAGED_OUTPUTS * MAXIMUM_MODES_PER_OUTPUT;
const MAXIMUM_WINDOWS: usize = 4096;

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
    query_flags: u32,
    snapshot_id: u64,
    expected_items: u32,
    actual_items: u32,
    reading: bool,
    ended: bool,
    acknowledged: bool,
    mode_ids: BTreeSet<u64>,
    mode_counts: BTreeMap<u64, usize>,
    surface_windows: BTreeMap<u64, u32>,
    snapshot: OutputSnapshot,
}

impl SnapshotDecoder {
    pub(crate) fn new(request_id: u64, request_sequence: Sequence, query_flags: u32) -> Self {
        Self {
            request_id,
            request_sequence,
            query_flags,
            snapshot_id: 0,
            expected_items: 0,
            actual_items: 0,
            reading: false,
            ended: false,
            acknowledged: false,
            mode_ids: BTreeSet::new(),
            mode_counts: BTreeMap::new(),
            surface_windows: BTreeMap::new(),
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
        self.validate_relationships()?;
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
                if self.snapshot.descriptors.contains_key(&value.output_id) {
                    return Err(SnapshotError::new(
                        "output snapshot contains a duplicate descriptor",
                    ));
                }
                if self.snapshot.descriptors.len() == gw_wire::MAXIMUM_MANAGED_OUTPUTS {
                    return Err(SnapshotError::new(
                        "output snapshot contains more than eight outputs",
                    ));
                }
                self.snapshot.descriptors.insert(value.output_id, value);
            }
            MessageType::OUTPUT_MODE_UPSERT => {
                let value = decode_output_mode_upsert(&record.payload)
                    .map_err(|_| SnapshotError::new("output snapshot contains an invalid mode"))?;
                if !self.mode_ids.insert(value.mode_id) {
                    return Err(SnapshotError::new(
                        "output snapshot contains a duplicate mode ID",
                    ));
                }
                if self.snapshot.modes.len() == MAXIMUM_TOTAL_MODES {
                    return Err(SnapshotError::new(
                        "output snapshot contains too many modes",
                    ));
                }
                let count = self.mode_counts.entry(value.output_id).or_default();
                if *count == MAXIMUM_MODES_PER_OUTPUT {
                    return Err(SnapshotError::new(
                        "output snapshot contains more than 128 modes for one output",
                    ));
                }
                *count += 1;
                self.snapshot.modes.push(value);
            }
            MessageType::OUTPUT_UPSERT => {
                let value = decode_output_upsert(&record.payload).map_err(|_| {
                    SnapshotError::new("control server sent a malformed output record")
                })?;
                if self.snapshot.outputs.contains_key(&value.output_id) {
                    return Err(SnapshotError::new(
                        "output snapshot contains duplicate layout state",
                    ));
                }
                if self.snapshot.outputs.len() == gw_wire::MAXIMUM_MANAGED_OUTPUTS {
                    return Err(SnapshotError::new(
                        "output snapshot contains more than eight outputs",
                    ));
                }
                self.snapshot.outputs.insert(value.output_id, value);
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
                let window = self.window_for_record(value.surface_id, value.x11_window_id)?;
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
                let window = self.window_for_record(value.surface_id, value.x11_window_id)?;
                window.focused = value.focused;
                window.fullscreen = value.applied_state == gw_wire::PolicyAppliedState::Fullscreen;
            }
            MessageType::SURFACE_OUTPUT_STATE => {
                let value = decode_surface_output_state(&record.payload).map_err(|_| {
                    SnapshotError::new("control server sent a malformed window record")
                })?;
                let window_id = self
                    .surface_windows
                    .get(&value.surface_id)
                    .copied()
                    .ok_or_else(|| {
                        SnapshotError::new("window membership precedes its surface record")
                    })?;
                let window = self.snapshot.windows.get_mut(&window_id).ok_or_else(|| {
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
                if self.snapshot.vrr_windows.contains_key(&value.window_id) {
                    return Err(SnapshotError::new(
                        "VRR snapshot contains duplicate window state",
                    ));
                }
                if self.snapshot.vrr_windows.len() == MAXIMUM_WINDOWS {
                    return Err(SnapshotError::new(
                        "VRR snapshot contains more than 4096 window states",
                    ));
                }
                self.snapshot.vrr_windows.insert(value.window_id, value);
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

    fn window_for_record(
        &mut self,
        surface_id: u64,
        window_id: u32,
    ) -> Result<&mut WindowSnapshot, SnapshotError> {
        if self
            .surface_windows
            .get(&surface_id)
            .is_some_and(|&existing| existing != window_id)
            || self
                .snapshot
                .windows
                .get(&window_id)
                .is_some_and(|existing| existing.surface_id != surface_id)
        {
            return Err(SnapshotError::new(
                "window snapshot contains conflicting surface identities",
            ));
        }
        if !self.snapshot.windows.contains_key(&window_id) {
            if self.snapshot.windows.len() == MAXIMUM_WINDOWS {
                return Err(SnapshotError::new(
                    "window snapshot contains more than 4096 windows",
                ));
            }
            self.snapshot
                .windows
                .insert(window_id, WindowSnapshot::new(surface_id, window_id));
            self.surface_windows.insert(surface_id, window_id);
        }
        self.snapshot
            .windows
            .get_mut(&window_id)
            .ok_or_else(|| SnapshotError::new("window snapshot contains invalid surface state"))
    }

    fn validate_relationships(&self) -> Result<(), SnapshotError> {
        let descriptors_queried = self.query_flags & OUTPUT_QUERY_DESCRIPTORS != 0;
        let modes_queried = self.query_flags & OUTPUT_QUERY_MODES != 0;
        let layout_queried = self.query_flags & OUTPUT_QUERY_LAYOUT != 0;

        if descriptors_queried {
            let mut names = BTreeSet::new();
            if self
                .snapshot
                .descriptors
                .values()
                .any(|descriptor| !names.insert(descriptor.name.as_str()))
            {
                return Err(SnapshotError::new(
                    "output snapshot contains duplicate output names",
                ));
            }
        }
        if descriptors_queried
            && layout_queried
            && self
                .snapshot
                .descriptors
                .keys()
                .ne(self.snapshot.outputs.keys())
        {
            return Err(SnapshotError::new(
                "output snapshot descriptor and layout sets disagree",
            ));
        }
        if modes_queried
            && self.snapshot.modes.iter().any(|mode| {
                (descriptors_queried && !self.snapshot.descriptors.contains_key(&mode.output_id))
                    || (layout_queried && !self.snapshot.outputs.contains_key(&mode.output_id))
            })
        {
            return Err(SnapshotError::new(
                "output snapshot mode references an unknown output",
            ));
        }
        if layout_queried {
            let enabled_count = self
                .snapshot
                .outputs
                .values()
                .filter(|output| output.enabled)
                .count();
            if self.snapshot.outputs.is_empty()
                || enabled_count != self.snapshot.enabled_output_count as usize
                || !self
                    .snapshot
                    .outputs
                    .get(&self.snapshot.primary_output_id)
                    .is_some_and(|output| output.enabled)
            {
                return Err(SnapshotError::new(
                    "output snapshot acknowledgement does not match its layout",
                ));
            }
        }
        if layout_queried
            && self.snapshot.windows.values().any(|window| {
                !self
                    .snapshot
                    .outputs
                    .contains_key(&window.primary_output_id)
                    || window
                        .output_ids
                        .iter()
                        .any(|output_id| !self.snapshot.outputs.contains_key(output_id))
            })
        {
            return Err(SnapshotError::new(
                "window snapshot references an unknown output",
            ));
        }
        let mut surface_ids = BTreeSet::new();
        if self
            .snapshot
            .windows
            .values()
            .any(|window| !surface_ids.insert(window.surface_id))
        {
            return Err(SnapshotError::new(
                "window snapshot contains duplicate surface identities",
            ));
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use gw_ipc::ReceivedRecord;
    use gw_types::{Generation, SnapshotId};
    use gw_wire::compositor::{
        OPACITY_ONE, OutputUpsert, SdrColorMetadata, SurfaceUpsert, Transform, TriState,
        encode_output_upsert, encode_surface_upsert,
    };
    use gw_wire::vrr::{SurfaceVrrState, VrrWindowPreference, encode_surface_vrr_state};
    use gw_wire::{
        Envelope, OutputConfigurationAcknowledged, OutputDescriptorUpsert, OutputKind,
        OutputModeUpsert, SnapshotBegin, SnapshotEnd, encode_output_configuration_acknowledged,
        encode_output_descriptor_upsert, encode_output_mode_upsert, encode_snapshot_begin,
        encode_snapshot_end,
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

    fn begin(expected_item_count: u32) -> ReceivedRecord {
        record(
            MessageType::SNAPSHOT_BEGIN,
            2,
            MessageFlags::default(),
            0,
            encode_snapshot_begin(SnapshotBegin {
                snapshot_id: SnapshotId::new(91),
                domain: SnapshotDomain::Outputs,
                flags: 0,
                generation: Generation::new(1),
                expected_item_count,
            }),
        )
    }

    fn end(actual_item_count: u32) -> ReceivedRecord {
        record(
            MessageType::SNAPSHOT_END,
            2,
            MessageFlags::default(),
            0,
            encode_snapshot_end(SnapshotEnd {
                snapshot_id: SnapshotId::new(91),
                generation: Generation::new(1),
                actual_item_count,
            }),
        )
    }

    fn descriptor(output_id: u64, name: String) -> OutputDescriptorUpsert {
        OutputDescriptorUpsert {
            output_id,
            kind: OutputKind::Headless,
            capability_flags: 0x2b,
            name,
            physical_width_millimeters: 0,
            physical_height_millimeters: 0,
            supported_transform_mask: 1,
            minimum_scale_numerator: 1,
            minimum_scale_denominator: 1,
            maximum_scale_numerator: 4,
            maximum_scale_denominator: 1,
            maximum_scale_denominator_value: 120,
            maximum_physical_width: 4096,
            maximum_physical_height: 4096,
        }
    }

    fn mode(output_id: u64, mode_id: u64) -> OutputModeUpsert {
        OutputModeUpsert {
            output_id,
            mode_id,
            physical_width: 640,
            physical_height: 480,
            refresh_millihertz: 60_000,
            preferred: true,
            current: true,
            flags: 0,
        }
    }

    fn output(output_id: u64) -> OutputUpsert {
        OutputUpsert {
            output_id,
            enabled: true,
            logical_x: 0,
            logical_y: 0,
            logical_width: 640,
            logical_height: 480,
            physical_pixel_width: 640,
            physical_pixel_height: 480,
            refresh_millihertz: 60_000,
            scale_numerator: 1,
            scale_denominator: 1,
            transform: Transform::Normal,
            color: SdrColorMetadata::default(),
        }
    }

    fn surface(surface_id: u64, window_id: u32) -> SurfaceUpsert {
        SurfaceUpsert {
            surface_id,
            x11_window_id: window_id,
            parent_surface_id: 0,
            output_id: 11,
            logical_x: 0,
            logical_y: 0,
            logical_width: 1,
            logical_height: 1,
            stacking: 0,
            visible: true,
            clipping: false,
            clip_x: 0,
            clip_y: 0,
            clip_width: 0,
            clip_height: 0,
            transform: Transform::Normal,
            opacity: OPACITY_ONE,
            scale_numerator: 1,
            scale_denominator: 1,
            color: SdrColorMetadata::default(),
            presentation_flags: 0,
            fullscreen_eligible: TriState::Unknown,
            direct_scanout_eligible: TriState::Unknown,
        }
    }

    fn surface_vrr_state(surface_id: u64, window_id: u32) -> SurfaceVrrState {
        SurfaceVrrState {
            surface_id,
            window_id,
            output_id: 11,
            preference: VrrWindowPreference::Default,
            policy_selected: false,
            policy_eligible: false,
            focused: false,
            fullscreen: false,
            borderless_fullscreen: false,
            exclusive_output_membership: false,
            reason_flags: 0,
            policy_generation: 1,
            flags: 0,
        }
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

    #[test]
    fn ninth_output_is_rejected_before_snapshot_allocation_can_grow() {
        let mut decoder = SnapshotDecoder::new(1, Sequence::new(2), OUTPUT_QUERY_DESCRIPTORS);
        decoder.consume(&begin(9)).unwrap();
        for output_id in 1..=8 {
            let payload = encode_output_descriptor_upsert(&descriptor(
                output_id,
                format!("OUTPUT-{output_id}"),
            ))
            .unwrap();
            decoder
                .consume(&record(
                    MessageType::OUTPUT_DESCRIPTOR_UPSERT,
                    output_id + 2,
                    MessageFlags::SNAPSHOT_ITEM,
                    0,
                    payload,
                ))
                .unwrap();
        }
        let payload =
            encode_output_descriptor_upsert(&descriptor(9, "OUTPUT-9".to_owned())).unwrap();
        assert_eq!(
            decoder
                .consume(&record(
                    MessageType::OUTPUT_DESCRIPTOR_UPSERT,
                    11,
                    MessageFlags::SNAPSHOT_ITEM,
                    0,
                    payload,
                ))
                .unwrap_err()
                .to_string(),
            "output snapshot contains more than eight outputs"
        );
    }

    #[test]
    fn four_thousand_ninety_seventh_window_is_rejected() {
        let mut decoder = SnapshotDecoder::new(1, Sequence::new(2), OUTPUT_QUERY_WINDOWS);
        decoder.consume(&begin(4097)).unwrap();
        for window_id in 1..=4096 {
            decoder
                .consume(&record(
                    MessageType::SURFACE_UPSERT,
                    u64::from(window_id) + 2,
                    MessageFlags::SNAPSHOT_ITEM,
                    0,
                    encode_surface_upsert(&surface(u64::from(window_id), window_id)),
                ))
                .unwrap();
        }
        assert_eq!(decoder.snapshot.windows.len(), 4096);
        assert_eq!(decoder.surface_windows.len(), 4096);

        let error = decoder
            .consume(&record(
                MessageType::SURFACE_UPSERT,
                4099,
                MessageFlags::SNAPSHOT_ITEM,
                0,
                encode_surface_upsert(&surface(4097, 4097)),
            ))
            .unwrap_err();
        assert_eq!(
            error.to_string(),
            "window snapshot contains more than 4096 windows"
        );
    }

    #[test]
    fn surface_identity_cannot_be_reassigned_between_windows() {
        let mut decoder = SnapshotDecoder::new(1, Sequence::new(2), OUTPUT_QUERY_WINDOWS);
        decoder.consume(&begin(2)).unwrap();
        decoder
            .consume(&record(
                MessageType::SURFACE_UPSERT,
                3,
                MessageFlags::SNAPSHOT_ITEM,
                0,
                encode_surface_upsert(&surface(1, 1)),
            ))
            .unwrap();

        let error = decoder
            .consume(&record(
                MessageType::SURFACE_UPSERT,
                4,
                MessageFlags::SNAPSHOT_ITEM,
                0,
                encode_surface_upsert(&surface(1, 2)),
            ))
            .unwrap_err();
        assert_eq!(
            error.to_string(),
            "window snapshot contains conflicting surface identities"
        );
    }

    #[test]
    fn four_thousand_ninety_seventh_vrr_window_state_is_rejected() {
        let flags = OUTPUT_QUERY_WINDOWS | OUTPUT_QUERY_VRR;
        let mut decoder = SnapshotDecoder::new(1, Sequence::new(2), flags);
        decoder.consume(&begin(4097)).unwrap();
        for window_id in 1..=4096 {
            decoder
                .consume(&record(
                    MessageType::SURFACE_VRR_STATE,
                    u64::from(window_id) + 2,
                    MessageFlags::SNAPSHOT_ITEM,
                    0,
                    encode_surface_vrr_state(&surface_vrr_state(u64::from(window_id), window_id)),
                ))
                .unwrap();
        }

        let error = decoder
            .consume(&record(
                MessageType::SURFACE_VRR_STATE,
                4099,
                MessageFlags::SNAPSHOT_ITEM,
                0,
                encode_surface_vrr_state(&surface_vrr_state(4097, 4097)),
            ))
            .unwrap_err();
        assert_eq!(
            error.to_string(),
            "VRR snapshot contains more than 4096 window states"
        );
    }

    #[test]
    fn hundred_twenty_ninth_mode_for_one_output_is_rejected() {
        let mut decoder = SnapshotDecoder::new(1, Sequence::new(2), OUTPUT_QUERY_MODES);
        decoder.consume(&begin(129)).unwrap();
        for mode_id in 1..=128 {
            decoder
                .consume(&record(
                    MessageType::OUTPUT_MODE_UPSERT,
                    mode_id + 2,
                    MessageFlags::SNAPSHOT_ITEM,
                    0,
                    encode_output_mode_upsert(&mode(11, mode_id)),
                ))
                .unwrap();
        }
        assert_eq!(
            decoder
                .consume(&record(
                    MessageType::OUTPUT_MODE_UPSERT,
                    131,
                    MessageFlags::SNAPSHOT_ITEM,
                    0,
                    encode_output_mode_upsert(&mode(11, 129)),
                ))
                .unwrap_err()
                .to_string(),
            "output snapshot contains more than 128 modes for one output"
        );
    }

    #[test]
    fn duplicate_mode_ids_are_rejected_across_outputs() {
        let mut decoder = SnapshotDecoder::new(1, Sequence::new(2), OUTPUT_QUERY_MODES);
        decoder.consume(&begin(2)).unwrap();
        decoder
            .consume(&record(
                MessageType::OUTPUT_MODE_UPSERT,
                3,
                MessageFlags::SNAPSHOT_ITEM,
                0,
                encode_output_mode_upsert(&mode(11, 21)),
            ))
            .unwrap();
        assert_eq!(
            decoder
                .consume(&record(
                    MessageType::OUTPUT_MODE_UPSERT,
                    4,
                    MessageFlags::SNAPSHOT_ITEM,
                    0,
                    encode_output_mode_upsert(&mode(12, 21)),
                ))
                .unwrap_err()
                .to_string(),
            "output snapshot contains a duplicate mode ID"
        );
    }

    #[test]
    fn non_vrr_queries_validate_inventory_relationships() {
        let mut decoder = SnapshotDecoder::new(1, Sequence::new(2), OUTPUT_QUERY_FLAGS);
        decoder.consume(&begin(3)).unwrap();
        let descriptor_payload =
            encode_output_descriptor_upsert(&descriptor(11, "LEFT".to_owned())).unwrap();
        for (message_type, payload) in [
            (MessageType::OUTPUT_DESCRIPTOR_UPSERT, descriptor_payload),
            (
                MessageType::OUTPUT_MODE_UPSERT,
                encode_output_mode_upsert(&mode(12, 21)),
            ),
            (
                MessageType::OUTPUT_UPSERT,
                encode_output_upsert(&output(11)),
            ),
        ] {
            decoder
                .consume(&record(
                    message_type,
                    3,
                    MessageFlags::SNAPSHOT_ITEM,
                    0,
                    payload,
                ))
                .unwrap();
        }
        decoder.consume(&end(3)).unwrap();
        assert_eq!(
            decoder
                .consume(&acknowledgement(OutputConfigurationResult::Accepted))
                .unwrap_err()
                .to_string(),
            "output snapshot mode references an unknown output"
        );
    }
}
