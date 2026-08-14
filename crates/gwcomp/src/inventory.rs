use std::collections::BTreeMap;

use gw_wire::compositor::{OutputUpsert, SdrColorMetadata, Transform, encode_output_upsert};
use gw_wire::output::{
    OUTPUT_ARBITRARY_HEADLESS_MODE, OutputConfigurationAcknowledged, OutputConfigurationResult,
    OutputDescriptorUpsert, OutputKind, OutputModeUpsert, encode_output_configuration_acknowledged,
    encode_output_descriptor_upsert, encode_output_mode_upsert,
};
use gw_wire::vrr::{
    OutputVrrCapabilityUpsert, OutputVrrPolicyUpsert, OutputVrrStateUpsert,
    VRR_REASON_SIMULATED_HEADLESS, VrrDecision, VrrPolicyMode, encode_output_vrr_capability_upsert,
    encode_output_vrr_policy_upsert, encode_output_vrr_state_upsert,
};

use crate::{HeadlessOutput, HeadlessVrr};

const OUTPUT_CONNECTED: u32 = 1;
const OUTPUT_SCALE_CONFIGURABLE: u32 = 1 << 3;
const OUTPUT_TRANSFORM_CONFIGURABLE: u32 = 1 << 4;
const OUTPUT_PRIMARY_ELIGIBLE: u32 = 1 << 5;
const ALL_TRANSFORMS: u32 = 0xff;
const REASON_OUTPUT_NOT_DRM: u64 = 1 << 2;
const REASON_OUTPUT_NOT_VRR_CAPABLE: u64 = 1 << 3;
const REASON_VRR_PROPERTY_MISSING: u64 = 1 << 5;
const REASON_POLICY_OFF: u64 = 1 << 10;

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct OutputRecord {
    pub descriptor: OutputDescriptorUpsert,
    pub mode: OutputModeUpsert,
    pub state: OutputUpsert,
    pub vrr_capability: OutputVrrCapabilityUpsert,
    pub vrr_policy: OutputVrrPolicyUpsert,
    pub vrr_state: OutputVrrStateUpsert,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct Inventory {
    pub generation: u64,
    pub primary_output_id: u64,
    pub root_width: u32,
    pub root_height: u32,
    pub outputs: BTreeMap<u64, OutputRecord>,
    output_order: Vec<u64>,
}

#[derive(Clone, Debug)]
pub struct PublishedRecord {
    pub message_type: gw_types::MessageType,
    pub payload: Vec<u8>,
}

impl Inventory {
    #[must_use]
    #[allow(clippy::too_many_lines)]
    pub fn build(outputs: &[HeadlessOutput], vrr: &[HeadlessVrr]) -> Self {
        let mut records = BTreeMap::new();
        let mut output_order = Vec::with_capacity(outputs.len());
        let mut logical_x = 0_u32;
        let mut root_height = 0_u32;
        for output in outputs {
            let output_id = headless_output_id(&output.name);
            let mode_id = output_mode_id(output_id, output);
            let simulation = vrr.iter().find(|request| request.name == output.name);
            let capability = simulation.map_or(
                OutputVrrCapabilityUpsert {
                    output_id,
                    connector_property_present: false,
                    hardware_capable: false,
                    kms_controllable: false,
                    simulated: false,
                    range_available: false,
                    atomic_required: false,
                    minimum_refresh_millihertz: 0,
                    maximum_refresh_millihertz: 0,
                    reason_flags: REASON_OUTPUT_NOT_DRM
                        | REASON_OUTPUT_NOT_VRR_CAPABLE
                        | REASON_VRR_PROPERTY_MISSING,
                    flags: 0,
                },
                |request| OutputVrrCapabilityUpsert {
                    output_id,
                    connector_property_present: true,
                    hardware_capable: false,
                    kms_controllable: true,
                    simulated: true,
                    range_available: true,
                    atomic_required: false,
                    minimum_refresh_millihertz: request.minimum_refresh_millihertz,
                    maximum_refresh_millihertz: request.maximum_refresh_millihertz,
                    reason_flags: VRR_REASON_SIMULATED_HEADLESS,
                    flags: 0,
                },
            );
            let state_reasons = if simulation.is_some() {
                REASON_POLICY_OFF | VRR_REASON_SIMULATED_HEADLESS
            } else {
                capability.reason_flags
            };
            let record = OutputRecord {
                descriptor: OutputDescriptorUpsert {
                    output_id,
                    kind: OutputKind::Headless,
                    capability_flags: OUTPUT_CONNECTED
                        | OUTPUT_ARBITRARY_HEADLESS_MODE
                        | OUTPUT_SCALE_CONFIGURABLE
                        | OUTPUT_TRANSFORM_CONFIGURABLE
                        | OUTPUT_PRIMARY_ELIGIBLE,
                    name: output.name.clone(),
                    physical_width_millimeters: 0,
                    physical_height_millimeters: 0,
                    supported_transform_mask: ALL_TRANSFORMS,
                    minimum_scale_numerator: 1,
                    minimum_scale_denominator: 1,
                    maximum_scale_numerator: 4,
                    maximum_scale_denominator: 1,
                    maximum_scale_denominator_value: 16,
                    maximum_physical_width: 4096,
                    maximum_physical_height: 4096,
                },
                mode: OutputModeUpsert {
                    output_id,
                    mode_id,
                    physical_width: output.width,
                    physical_height: output.height,
                    refresh_millihertz: output.refresh_millihertz,
                    preferred: true,
                    current: true,
                    flags: 0,
                },
                state: OutputUpsert {
                    output_id,
                    enabled: true,
                    logical_x: i32::try_from(logical_x)
                        .expect("validated headless layout fits i32"),
                    logical_y: 0,
                    logical_width: output.width,
                    logical_height: output.height,
                    physical_pixel_width: output.width,
                    physical_pixel_height: output.height,
                    refresh_millihertz: output.refresh_millihertz,
                    scale_numerator: 1,
                    scale_denominator: 1,
                    transform: Transform::Normal,
                    color: SdrColorMetadata::default(),
                },
                vrr_capability: capability,
                vrr_policy: OutputVrrPolicyUpsert {
                    output_id,
                    mode: VrrPolicyMode::Off,
                    flags: 0,
                },
                vrr_state: OutputVrrStateUpsert {
                    output_id,
                    requested_mode: VrrPolicyMode::Off,
                    decision: if simulation.is_some() {
                        VrrDecision::Disabled
                    } else {
                        VrrDecision::Unsupported
                    },
                    desired_enabled: false,
                    effective_enabled: false,
                    property_readback_valid: false,
                    session_active: true,
                    candidate_window_id: 0,
                    candidate_surface_id: 0,
                    reason_flags: state_reasons,
                    state_generation: 1,
                    transition_serial: 1,
                    last_commit_id: 0,
                    last_presented_generation: 0,
                    last_flip_sequence: 0,
                    flags: 0,
                    last_flip_timestamp_nanoseconds: 0,
                    last_interval_nanoseconds: 0,
                },
            };
            logical_x += output.width;
            root_height = root_height.max(output.height);
            output_order.push(output_id);
            records.insert(output_id, record);
        }
        let primary_output_id = records.first_key_value().map_or(0, |(id, _)| *id);
        // The primary is CLI order, not stable-ID order.
        let primary_output_id = outputs
            .first()
            .map_or(primary_output_id, |output| headless_output_id(&output.name));
        Self {
            generation: 1,
            primary_output_id,
            root_width: logical_x,
            root_height,
            outputs: records,
            output_order,
        }
    }

    pub fn records(
        &self,
        flags: u32,
    ) -> Result<Vec<PublishedRecord>, gw_wire::compositor::ContractDecodeError> {
        let mut result = Vec::new();
        if flags & 1 != 0 {
            for output in self.output_order.iter().map(|id| &self.outputs[id]) {
                result.push(PublishedRecord {
                    message_type: gw_types::MessageType::OUTPUT_DESCRIPTOR_UPSERT,
                    payload: encode_output_descriptor_upsert(&output.descriptor)?,
                });
            }
        }
        if flags & 2 != 0 {
            for output in self.output_order.iter().map(|id| &self.outputs[id]) {
                result.push(PublishedRecord {
                    message_type: gw_types::MessageType::OUTPUT_MODE_UPSERT,
                    payload: encode_output_mode_upsert(&output.mode),
                });
            }
        }
        if flags & 4 != 0 {
            for output in self.output_order.iter().map(|id| &self.outputs[id]) {
                result.push(PublishedRecord {
                    message_type: gw_types::MessageType::OUTPUT_UPSERT,
                    payload: encode_output_upsert(&output.state),
                });
            }
        }
        if flags & 16 != 0 {
            for output in self.output_order.iter().map(|id| &self.outputs[id]) {
                result.push(PublishedRecord {
                    message_type: gw_types::MessageType::OUTPUT_VRR_CAPABILITY_UPSERT,
                    payload: encode_output_vrr_capability_upsert(&output.vrr_capability),
                });
            }
            for output in self.output_order.iter().map(|id| &self.outputs[id]) {
                result.push(PublishedRecord {
                    message_type: gw_types::MessageType::OUTPUT_VRR_POLICY_UPSERT,
                    payload: encode_output_vrr_policy_upsert(&output.vrr_policy),
                });
            }
            for output in self.output_order.iter().map(|id| &self.outputs[id]) {
                result.push(PublishedRecord {
                    message_type: gw_types::MessageType::OUTPUT_VRR_STATE_UPSERT,
                    payload: encode_output_vrr_state_upsert(&output.vrr_state),
                });
            }
        }
        Ok(result)
    }

    #[must_use]
    pub fn acknowledgement(
        &self,
        request_id: u64,
        result: OutputConfigurationResult,
    ) -> OutputConfigurationAcknowledged {
        OutputConfigurationAcknowledged {
            request_id,
            applied_generation: self.generation,
            result,
            flags: 0,
            primary_output_id: self.primary_output_id,
            root_logical_width: self.root_width,
            root_logical_height: self.root_height,
            enabled_output_count: self
                .outputs
                .values()
                .filter(|output| output.state.enabled)
                .count()
                .try_into()
                .expect("headless output count fits u32"),
        }
    }

    #[must_use]
    pub fn encoded_acknowledgement(
        &self,
        request_id: u64,
        result: OutputConfigurationResult,
    ) -> Vec<u8> {
        encode_output_configuration_acknowledged(&self.acknowledgement(request_id, result))
    }

    pub fn apply_configuration(&mut self, states: &[OutputUpsert], primary_output_id: u64) -> bool {
        if states.len() != self.outputs.len() || !self.outputs.contains_key(&primary_output_id) {
            return false;
        }
        let mut candidate = self.clone();
        let mut seen = std::collections::BTreeSet::new();
        for state in states {
            if !seen.insert(state.output_id) {
                return false;
            }
            let Some(output) = candidate.outputs.get_mut(&state.output_id) else {
                return false;
            };
            output.state = state.clone();
        }
        let Some((root_width, root_height)) = valid_layout(&candidate.outputs, primary_output_id)
        else {
            return false;
        };
        candidate.generation += 1;
        candidate.primary_output_id = primary_output_id;
        candidate.root_width = root_width;
        candidate.root_height = root_height;
        for output in candidate.outputs.values_mut() {
            output.vrr_state.state_generation = candidate.generation;
        }
        *self = candidate;
        true
    }
}

fn valid_layout(outputs: &BTreeMap<u64, OutputRecord>, primary: u64) -> Option<(u32, u32)> {
    if !outputs
        .get(&primary)
        .is_some_and(|output| output.state.enabled)
    {
        return None;
    }
    let enabled: Vec<_> = outputs
        .iter()
        .filter(|(_, output)| output.state.enabled)
        .collect();
    if enabled.is_empty() {
        return None;
    }
    let mut root_width = 0_u32;
    let mut root_height = 0_u32;
    let mut total_pixels = 0_u64;
    for (id, output) in enabled.iter().copied() {
        let state = &output.state;
        if *id != state.output_id
            || state.logical_x < 0
            || state.logical_y < 0
            || state.logical_width == 0
            || state.logical_height == 0
            || state.physical_pixel_width == 0
            || state.physical_pixel_height == 0
            || state.refresh_millihertz == 0
            || state.scale_numerator == 0
            || state.scale_denominator == 0
        {
            return None;
        }
        let right = state
            .logical_x
            .cast_unsigned()
            .checked_add(state.logical_width)?;
        let bottom = state
            .logical_y
            .cast_unsigned()
            .checked_add(state.logical_height)?;
        root_width = root_width.max(right);
        root_height = root_height.max(bottom);
        let pixels = u64::from(state.physical_pixel_width)
            .checked_mul(u64::from(state.physical_pixel_height))?;
        total_pixels = total_pixels.checked_add(pixels)?;
        if total_pixels > gwcomp_core::MAXIMUM_TOTAL_OUTPUT_PIXELS {
            return None;
        }
    }
    for (index, (_, left)) in enabled.iter().enumerate() {
        for (_, right) in &enabled[index + 1..] {
            let left = &left.state;
            let right = &right.state;
            let separated = i64::from(left.logical_x) + i64::from(left.logical_width)
                <= i64::from(right.logical_x)
                || i64::from(right.logical_x) + i64::from(right.logical_width)
                    <= i64::from(left.logical_x)
                || i64::from(left.logical_y) + i64::from(left.logical_height)
                    <= i64::from(right.logical_y)
                || i64::from(right.logical_y) + i64::from(right.logical_height)
                    <= i64::from(left.logical_y);
            if !separated {
                return None;
            }
        }
    }
    Some((root_width, root_height))
}

fn fnv_append(mut hash: u64, bytes: &[u8]) -> u64 {
    for byte in bytes {
        hash ^= u64::from(*byte);
        hash = hash.wrapping_mul(1_099_511_628_211);
    }
    hash
}
fn append_u32(hash: u64, value: u32) -> u64 {
    fnv_append(hash, &value.to_be_bytes())
}
fn append_u64(hash: u64, value: u64) -> u64 {
    fnv_append(hash, &value.to_be_bytes())
}

fn headless_output_id(name: &str) -> u64 {
    (fnv_append(
        fnv_append(14_695_981_039_346_656_037, b"glasswyrm:headless:"),
        name.as_bytes(),
    )) | (1 << 63)
}

fn output_mode_id(output_id: u64, output: &HeadlessOutput) -> u64 {
    let mode_name = format!(
        "{}x{}@{}",
        output.width, output.height, output.refresh_millihertz
    );
    let mut hash = fnv_append(14_695_981_039_346_656_037, b"glasswyrm:mode:");
    hash = append_u64(hash, output_id);
    hash = append_u32(hash, output.width);
    hash = append_u32(hash, output.height);
    hash = append_u32(hash, output.refresh_millihertz);
    hash = append_u32(hash, 0);
    hash = append_u32(
        hash,
        mode_name
            .len()
            .try_into()
            .expect("bounded mode name length fits u32"),
    );
    hash = fnv_append(hash, mode_name.as_bytes());
    (hash & !(1 << 63)) | (1 << 62)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn identities_match_m13_fixture() {
        assert_eq!(headless_output_id("LEFT"), 0xca9e_ec49_a394_3406);
        let output = HeadlessOutput {
            name: "LEFT".into(),
            width: 640,
            height: 480,
            refresh_millihertz: 60_000,
        };
        assert_eq!(
            output_mode_id(headless_output_id("LEFT"), &output),
            0x6b28_5010_714a_5e25
        );
    }

    #[test]
    fn inventory_is_stable_and_left_to_right() {
        let inventory = Inventory::build(
            &[
                HeadlessOutput {
                    name: "LEFT".into(),
                    width: 800,
                    height: 600,
                    refresh_millihertz: 60_000,
                },
                HeadlessOutput {
                    name: "RIGHT".into(),
                    width: 640,
                    height: 480,
                    refresh_millihertz: 75_000,
                },
            ],
            &[],
        );
        assert_eq!((inventory.root_width, inventory.root_height), (1440, 600));
        assert_eq!(
            inventory.outputs[&headless_output_id("RIGHT")]
                .state
                .logical_x,
            800
        );
        assert_eq!(inventory.records(7).unwrap().len(), 6);
    }

    #[test]
    fn configuration_rejects_duplicate_and_overflowing_layout_records() {
        let mut inventory = Inventory::build(
            &[
                HeadlessOutput {
                    name: "LEFT".into(),
                    width: 800,
                    height: 600,
                    refresh_millihertz: 60_000,
                },
                HeadlessOutput {
                    name: "RIGHT".into(),
                    width: 640,
                    height: 480,
                    refresh_millihertz: 60_000,
                },
            ],
            &[],
        );
        let primary = inventory.primary_output_id;
        let first = inventory.outputs[&primary].state.clone();
        assert!(!inventory.apply_configuration(&[first.clone(), first], primary));

        let mut states: Vec<_> = inventory
            .outputs
            .values()
            .map(|output| output.state.clone())
            .collect();
        states[0].logical_x = i32::MAX;
        states[0].logical_width = u32::MAX;
        assert!(!inventory.apply_configuration(&states, primary));
    }
}
