use crate::compositor::ContractDecodeError;
use crate::{ByteReader, ByteWriter};

pub const KNOWN_VRR_REASON_MASK: u64 = (1u64 << 33) - 1;
pub const VRR_REASON_SIMULATED_HEADLESS: u64 = 1u64 << 31;
pub const PRESENTATION_TIMING_SIMULATED: u32 = 1;

macro_rules! wire_enum{($name:ident{$($variant:ident=$value:expr),+$(,)?})=>{#[derive(Clone,Copy,Debug,Eq,PartialEq)]#[repr(u16)]pub enum $name{$($variant=$value),+}impl TryFrom<u16> for $name{type Error=ContractDecodeError;fn try_from(v:u16)->Result<Self,Self::Error>{match v{$($value=>Ok(Self::$variant),)+_=>Err(ContractDecodeError::InvalidValue)}}}};}
wire_enum!(VrrPolicyMode{Off=1,Fullscreen=2,Focused=3,AppRequested=4,AlwaysEligible=5});
wire_enum!(VrrWindowPreference{Default=0,Disable=1,Allow=2,Prefer=3});
wire_enum!(VrrDecision{Disabled=1,Enabled=2,Unsupported=3,Rejected=4});

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct OutputVrrCapabilityUpsert {
    pub output_id: u64,
    pub connector_property_present: bool,
    pub hardware_capable: bool,
    pub kms_controllable: bool,
    pub simulated: bool,
    pub range_available: bool,
    pub atomic_required: bool,
    pub minimum_refresh_millihertz: u32,
    pub maximum_refresh_millihertz: u32,
    pub reason_flags: u64,
    pub flags: u32,
}
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct OutputVrrPolicyUpsert {
    pub output_id: u64,
    pub mode: VrrPolicyMode,
    pub flags: u32,
}
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct OutputVrrStateUpsert {
    pub output_id: u64,
    pub requested_mode: VrrPolicyMode,
    pub decision: VrrDecision,
    pub desired_enabled: bool,
    pub effective_enabled: bool,
    pub property_readback_valid: bool,
    pub session_active: bool,
    pub candidate_window_id: u32,
    pub candidate_surface_id: u64,
    pub reason_flags: u64,
    pub state_generation: u64,
    pub transition_serial: u64,
    pub last_commit_id: u64,
    pub last_presented_generation: u64,
    pub last_flip_sequence: u32,
    pub flags: u32,
    pub last_flip_timestamp_nanoseconds: u64,
    pub last_interval_nanoseconds: u64,
}
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct SurfaceVrrState {
    pub surface_id: u64,
    pub window_id: u32,
    pub output_id: u64,
    pub preference: VrrWindowPreference,
    pub policy_selected: bool,
    pub policy_eligible: bool,
    pub focused: bool,
    pub fullscreen: bool,
    pub borderless_fullscreen: bool,
    pub exclusive_output_membership: bool,
    pub reason_flags: u64,
    pub policy_generation: u64,
    pub flags: u32,
}
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct PolicyWindowVrrUpsert {
    pub window_id: u32,
    pub preference: VrrWindowPreference,
    pub flags: u32,
}
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct PolicyOutputVrrUpsert {
    pub output_id: u64,
    pub mode: VrrPolicyMode,
    pub hardware_capable: bool,
    pub kms_controllable: bool,
    pub flags: u32,
}
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct PolicyWindowVrrState {
    pub window_id: u32,
    pub output_id: u64,
    pub preference: VrrWindowPreference,
    pub selected: bool,
    pub eligible: bool,
    pub focused: bool,
    pub fullscreen: bool,
    pub borderless_fullscreen: bool,
    pub exclusive_output_membership: bool,
    pub reason_flags: u64,
    pub flags: u32,
}
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct PolicyOutputVrrState {
    pub output_id: u64,
    pub mode: VrrPolicyMode,
    pub selected_window_id: u32,
    pub desired_enabled: bool,
    pub candidate_required: bool,
    pub reason_flags: u64,
    pub flags: u32,
}
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct PresentationTiming {
    pub output_id: u64,
    pub commit_id: u64,
    pub presented_generation: u64,
    pub flip_sequence: u32,
    pub flags: u32,
    pub kernel_timestamp_nanoseconds: u64,
    pub interval_nanoseconds: u64,
    pub effective_vrr_enabled: bool,
    pub timestamp_available: bool,
}

fn decode_bool(value: u8) -> Result<bool, ContractDecodeError> {
    match value {
        0 => Ok(false),
        1 => Ok(true),
        _ => Err(ContractDecodeError::InvalidValue),
    }
}
fn finish(r: ByteReader<'_>, valid: bool) -> Result<(), ContractDecodeError> {
    if !r.is_empty() {
        Err(ContractDecodeError::TrailingData)
    } else if valid {
        Ok(())
    } else {
        Err(ContractDecodeError::InvalidValue)
    }
}
fn reasons(v: u64) -> bool {
    v & !KNOWN_VRR_REASON_MASK == 0
}

#[must_use]
pub fn encode_output_vrr_capability_upsert(v: &OutputVrrCapabilityUpsert) -> Vec<u8> {
    let mut w = ByteWriter::new();
    w.write_u64(v.output_id);
    for b in [
        v.connector_property_present,
        v.hardware_capable,
        v.kms_controllable,
        v.simulated,
        v.range_available,
        v.atomic_required,
    ] {
        w.write_u8(u8::from(b))
    }
    w.write_u16(0);
    w.write_u32(v.minimum_refresh_millihertz);
    w.write_u32(v.maximum_refresh_millihertz);
    w.write_u64(v.reason_flags);
    w.write_u32(v.flags);
    w.write_u32(0);
    w.into_bytes()
}
pub fn decode_output_vrr_capability_upsert(
    bytes: &[u8],
) -> Result<OutputVrrCapabilityUpsert, ContractDecodeError> {
    let mut r = ByteReader::new(bytes);
    let output_id = r.read_u64()?;
    let connector_property_present = r.read_u8()?;
    let hardware_capable = r.read_u8()?;
    let kms_controllable = r.read_u8()?;
    let simulated = r.read_u8()?;
    let range_available = r.read_u8()?;
    let atomic_required = r.read_u8()?;
    let reserved16 = r.read_u16()?;
    let minimum_refresh_millihertz = r.read_u32()?;
    let maximum_refresh_millihertz = r.read_u32()?;
    let reason_flags = r.read_u64()?;
    let flags = r.read_u32()?;
    let reserved32 = r.read_u32()?;
    finish(r, true)?;
    let connector_property_present = decode_bool(connector_property_present)?;
    let hardware_capable = decode_bool(hardware_capable)?;
    let kms_controllable = decode_bool(kms_controllable)?;
    let simulated = decode_bool(simulated)?;
    let range_available = decode_bool(range_available)?;
    let atomic_required = decode_bool(atomic_required)?;
    let v = OutputVrrCapabilityUpsert {
        output_id,
        connector_property_present,
        hardware_capable,
        kms_controllable,
        simulated,
        range_available,
        atomic_required,
        minimum_refresh_millihertz,
        maximum_refresh_millihertz,
        reason_flags,
        flags,
    };
    let range = if range_available {
        minimum_refresh_millihertz != 0 && minimum_refresh_millihertz < maximum_refresh_millihertz
    } else {
        minimum_refresh_millihertz == 0 && maximum_refresh_millihertz == 0
    };
    if !(output_id != 0
        && reserved16 == 0
        && reserved32 == 0
        && flags == 0
        && range
        && reasons(reason_flags)
        && (!hardware_capable || connector_property_present)
        && (!simulated || !hardware_capable))
    {
        return Err(ContractDecodeError::InvalidValue);
    }
    Ok(v)
}

#[must_use]
pub fn encode_output_vrr_policy_upsert(v: &OutputVrrPolicyUpsert) -> Vec<u8> {
    let mut w = ByteWriter::new();
    w.write_u64(v.output_id);
    w.write_u16(v.mode as u16);
    w.write_u16(0);
    w.write_u32(v.flags);
    w.into_bytes()
}
pub fn decode_output_vrr_policy_upsert(
    bytes: &[u8],
) -> Result<OutputVrrPolicyUpsert, ContractDecodeError> {
    let mut r = ByteReader::new(bytes);
    let output_id = r.read_u64()?;
    let mode = r.read_u16()?;
    let reserved = r.read_u16()?;
    let flags = r.read_u32()?;
    finish(r, true)?;
    let v = OutputVrrPolicyUpsert {
        output_id,
        mode: mode.try_into()?,
        flags,
    };
    if v.output_id == 0 || reserved != 0 || v.flags != 0 {
        return Err(ContractDecodeError::InvalidValue);
    }
    Ok(v)
}

#[must_use]
pub fn encode_output_vrr_state_upsert(v: &OutputVrrStateUpsert) -> Vec<u8> {
    let mut w = ByteWriter::new();
    w.write_u64(v.output_id);
    w.write_u16(v.requested_mode as u16);
    w.write_u16(v.decision as u16);
    for b in [
        v.desired_enabled,
        v.effective_enabled,
        v.property_readback_valid,
        v.session_active,
    ] {
        w.write_u8(u8::from(b))
    }
    w.write_u32(v.candidate_window_id);
    w.write_u32(0);
    w.write_u64(v.candidate_surface_id);
    w.write_u64(v.reason_flags);
    w.write_u64(v.state_generation);
    w.write_u64(v.transition_serial);
    w.write_u64(v.last_commit_id);
    w.write_u64(v.last_presented_generation);
    w.write_u32(v.last_flip_sequence);
    w.write_u32(v.flags);
    w.write_u64(v.last_flip_timestamp_nanoseconds);
    w.write_u64(v.last_interval_nanoseconds);
    w.into_bytes()
}
pub fn decode_output_vrr_state_upsert(
    bytes: &[u8],
) -> Result<OutputVrrStateUpsert, ContractDecodeError> {
    let mut r = ByteReader::new(bytes);
    let output_id = r.read_u64()?;
    let requested_mode = r.read_u16()?;
    let decision = r.read_u16()?;
    let desired_enabled = r.read_u8()?;
    let effective_enabled = r.read_u8()?;
    let property_readback_valid = r.read_u8()?;
    let session_active = r.read_u8()?;
    let candidate_window_id = r.read_u32()?;
    let reserved = r.read_u32()?;
    let candidate_surface_id = r.read_u64()?;
    let reason_flags = r.read_u64()?;
    let state_generation = r.read_u64()?;
    let transition_serial = r.read_u64()?;
    let last_commit_id = r.read_u64()?;
    let last_presented_generation = r.read_u64()?;
    let last_flip_sequence = r.read_u32()?;
    let flags = r.read_u32()?;
    let last_flip_timestamp_nanoseconds = r.read_u64()?;
    let last_interval_nanoseconds = r.read_u64()?;
    finish(r, true)?;
    let v = OutputVrrStateUpsert {
        output_id,
        requested_mode: requested_mode.try_into()?,
        decision: decision.try_into()?,
        desired_enabled: decode_bool(desired_enabled)?,
        effective_enabled: decode_bool(effective_enabled)?,
        property_readback_valid: decode_bool(property_readback_valid)?,
        session_active: decode_bool(session_active)?,
        candidate_window_id,
        candidate_surface_id,
        reason_flags,
        state_generation,
        transition_serial,
        last_commit_id,
        last_presented_generation,
        last_flip_sequence,
        flags,
        last_flip_timestamp_nanoseconds,
        last_interval_nanoseconds,
    };
    let simulated = v.reason_flags & VRR_REASON_SIMULATED_HEADLESS != 0;
    let enabled = !v.effective_enabled
        || (v.desired_enabled
            && v.decision == VrrDecision::Enabled
            && (v.property_readback_valid || simulated));
    if !(v.output_id != 0
        && reserved == 0
        && v.flags == 0
        && reasons(v.reason_flags)
        && v.state_generation != 0
        && enabled
        && (v.decision == VrrDecision::Enabled || v.reason_flags != 0))
    {
        return Err(ContractDecodeError::InvalidValue);
    }
    Ok(v)
}

#[must_use]
pub fn encode_surface_vrr_state(v: &SurfaceVrrState) -> Vec<u8> {
    let mut w = ByteWriter::new();
    w.write_u64(v.surface_id);
    w.write_u32(v.window_id);
    w.write_u32(0);
    w.write_u64(v.output_id);
    w.write_u16(v.preference as u16);
    for b in [
        v.policy_selected,
        v.policy_eligible,
        v.focused,
        v.fullscreen,
        v.borderless_fullscreen,
        v.exclusive_output_membership,
    ] {
        w.write_u8(u8::from(b))
    }
    w.write_u64(v.reason_flags);
    w.write_u64(v.policy_generation);
    w.write_u32(v.flags);
    w.write_u32(0);
    w.into_bytes()
}
pub fn decode_surface_vrr_state(bytes: &[u8]) -> Result<SurfaceVrrState, ContractDecodeError> {
    let mut r = ByteReader::new(bytes);
    let surface_id = r.read_u64()?;
    let window_id = r.read_u32()?;
    let a = r.read_u32()?;
    let output_id = r.read_u64()?;
    let preference = r.read_u16()?;
    let policy_selected = r.read_u8()?;
    let policy_eligible = r.read_u8()?;
    let focused = r.read_u8()?;
    let fullscreen = r.read_u8()?;
    let borderless_fullscreen = r.read_u8()?;
    let exclusive_output_membership = r.read_u8()?;
    let reason_flags = r.read_u64()?;
    let policy_generation = r.read_u64()?;
    let flags = r.read_u32()?;
    let b = r.read_u32()?;
    finish(r, true)?;
    let v = SurfaceVrrState {
        surface_id,
        window_id,
        output_id,
        preference: preference.try_into()?,
        policy_selected: decode_bool(policy_selected)?,
        policy_eligible: decode_bool(policy_eligible)?,
        focused: decode_bool(focused)?,
        fullscreen: decode_bool(fullscreen)?,
        borderless_fullscreen: decode_bool(borderless_fullscreen)?,
        exclusive_output_membership: decode_bool(exclusive_output_membership)?,
        reason_flags,
        policy_generation,
        flags,
    };
    if !(surface_id != 0
        && window_id != 0
        && output_id != 0
        && a == 0
        && b == 0
        && flags == 0
        && reasons(reason_flags)
        && policy_generation != 0
        && (!v.policy_selected || v.policy_eligible))
    {
        return Err(ContractDecodeError::InvalidValue);
    }
    Ok(v)
}

#[must_use]
pub fn encode_policy_window_vrr_upsert(v: &PolicyWindowVrrUpsert) -> Vec<u8> {
    let mut w = ByteWriter::new();
    w.write_u32(v.window_id);
    w.write_u16(v.preference as u16);
    w.write_u16(0);
    w.write_u32(v.flags);
    w.write_u32(0);
    w.into_bytes()
}
pub fn decode_policy_window_vrr_upsert(
    bytes: &[u8],
) -> Result<PolicyWindowVrrUpsert, ContractDecodeError> {
    let mut r = ByteReader::new(bytes);
    let window_id = r.read_u32()?;
    let preference = r.read_u16()?;
    let reserved = r.read_u16()?;
    let flags = r.read_u32()?;
    let z = r.read_u32()?;
    finish(r, true)?;
    let v = PolicyWindowVrrUpsert {
        window_id,
        preference: preference.try_into()?,
        flags,
    };
    if v.window_id == 0 || reserved != 0 || v.flags != 0 || z != 0 {
        return Err(ContractDecodeError::InvalidValue);
    }
    Ok(v)
}
#[must_use]
pub fn encode_policy_output_vrr_upsert(v: &PolicyOutputVrrUpsert) -> Vec<u8> {
    let mut w = ByteWriter::new();
    w.write_u64(v.output_id);
    w.write_u16(v.mode as u16);
    w.write_u8(u8::from(v.hardware_capable));
    w.write_u8(u8::from(v.kms_controllable));
    w.write_u32(v.flags);
    w.into_bytes()
}
pub fn decode_policy_output_vrr_upsert(
    bytes: &[u8],
) -> Result<PolicyOutputVrrUpsert, ContractDecodeError> {
    let mut r = ByteReader::new(bytes);
    let output_id = r.read_u64()?;
    let mode = r.read_u16()?;
    let hardware_capable = r.read_u8()?;
    let kms_controllable = r.read_u8()?;
    let flags = r.read_u32()?;
    finish(r, true)?;
    let v = PolicyOutputVrrUpsert {
        output_id,
        mode: mode.try_into()?,
        hardware_capable: decode_bool(hardware_capable)?,
        kms_controllable: decode_bool(kms_controllable)?,
        flags,
    };
    if v.output_id == 0 || v.flags != 0 {
        return Err(ContractDecodeError::InvalidValue);
    }
    Ok(v)
}

#[must_use]
pub fn encode_policy_window_vrr_state(v: &PolicyWindowVrrState) -> Vec<u8> {
    let mut w = ByteWriter::new();
    w.write_u32(v.window_id);
    w.write_u32(0);
    w.write_u64(v.output_id);
    w.write_u16(v.preference as u16);
    for b in [
        v.selected,
        v.eligible,
        v.focused,
        v.fullscreen,
        v.borderless_fullscreen,
        v.exclusive_output_membership,
    ] {
        w.write_u8(u8::from(b))
    }
    w.write_u64(v.reason_flags);
    w.write_u32(v.flags);
    w.write_u32(0);
    w.into_bytes()
}
pub fn decode_policy_window_vrr_state(
    bytes: &[u8],
) -> Result<PolicyWindowVrrState, ContractDecodeError> {
    let mut r = ByteReader::new(bytes);
    let window_id = r.read_u32()?;
    let a = r.read_u32()?;
    let output_id = r.read_u64()?;
    let preference = r.read_u16()?;
    let selected = r.read_u8()?;
    let eligible = r.read_u8()?;
    let focused = r.read_u8()?;
    let fullscreen = r.read_u8()?;
    let borderless_fullscreen = r.read_u8()?;
    let exclusive_output_membership = r.read_u8()?;
    let reason_flags = r.read_u64()?;
    let flags = r.read_u32()?;
    let b = r.read_u32()?;
    finish(r, true)?;
    let v = PolicyWindowVrrState {
        window_id,
        output_id,
        preference: preference.try_into()?,
        selected: decode_bool(selected)?,
        eligible: decode_bool(eligible)?,
        focused: decode_bool(focused)?,
        fullscreen: decode_bool(fullscreen)?,
        borderless_fullscreen: decode_bool(borderless_fullscreen)?,
        exclusive_output_membership: decode_bool(exclusive_output_membership)?,
        reason_flags,
        flags,
    };
    if !(window_id != 0
        && output_id != 0
        && a == 0
        && b == 0
        && flags == 0
        && reasons(reason_flags)
        && (!v.selected || v.eligible))
    {
        return Err(ContractDecodeError::InvalidValue);
    }
    Ok(v)
}
#[must_use]
pub fn encode_policy_output_vrr_state(v: &PolicyOutputVrrState) -> Vec<u8> {
    let mut w = ByteWriter::new();
    w.write_u64(v.output_id);
    w.write_u16(v.mode as u16);
    w.write_u8(u8::from(v.desired_enabled));
    w.write_u8(u8::from(v.candidate_required));
    w.write_u32(v.selected_window_id);
    w.write_u64(v.reason_flags);
    w.write_u32(v.flags);
    w.write_u32(0);
    w.into_bytes()
}
pub fn decode_policy_output_vrr_state(
    bytes: &[u8],
) -> Result<PolicyOutputVrrState, ContractDecodeError> {
    let mut r = ByteReader::new(bytes);
    let output_id = r.read_u64()?;
    let mode = r.read_u16()?;
    let desired_enabled = r.read_u8()?;
    let candidate_required = r.read_u8()?;
    let selected_window_id = r.read_u32()?;
    let reason_flags = r.read_u64()?;
    let flags = r.read_u32()?;
    let z = r.read_u32()?;
    finish(r, true)?;
    let v = PolicyOutputVrrState {
        output_id,
        mode: mode.try_into()?,
        desired_enabled: decode_bool(desired_enabled)?,
        candidate_required: decode_bool(candidate_required)?,
        selected_window_id,
        reason_flags,
        flags,
    };
    if !(v.output_id != 0
        && z == 0
        && v.flags == 0
        && reasons(v.reason_flags)
        && (!v.candidate_required || v.selected_window_id != 0 || !v.desired_enabled))
    {
        return Err(ContractDecodeError::InvalidValue);
    }
    Ok(v)
}

#[must_use]
pub fn encode_presentation_timing(v: &PresentationTiming) -> Vec<u8> {
    let mut w = ByteWriter::new();
    w.write_u64(v.output_id);
    w.write_u64(v.commit_id);
    w.write_u64(v.presented_generation);
    w.write_u32(v.flip_sequence);
    w.write_u32(v.flags);
    w.write_u64(v.kernel_timestamp_nanoseconds);
    w.write_u64(v.interval_nanoseconds);
    w.write_u8(u8::from(v.effective_vrr_enabled));
    w.write_u8(u8::from(v.timestamp_available));
    w.write_u16(0);
    w.write_u32(0);
    w.into_bytes()
}
pub fn decode_presentation_timing(bytes: &[u8]) -> Result<PresentationTiming, ContractDecodeError> {
    let mut r = ByteReader::new(bytes);
    let output_id = r.read_u64()?;
    let commit_id = r.read_u64()?;
    let presented_generation = r.read_u64()?;
    let flip_sequence = r.read_u32()?;
    let flags = r.read_u32()?;
    let kernel_timestamp_nanoseconds = r.read_u64()?;
    let interval_nanoseconds = r.read_u64()?;
    let effective_vrr_enabled = r.read_u8()?;
    let timestamp_available = r.read_u8()?;
    let a = r.read_u16()?;
    let b = r.read_u32()?;
    finish(r, true)?;
    let v = PresentationTiming {
        output_id,
        commit_id,
        presented_generation,
        flip_sequence,
        flags,
        kernel_timestamp_nanoseconds,
        interval_nanoseconds,
        effective_vrr_enabled: decode_bool(effective_vrr_enabled)?,
        timestamp_available: decode_bool(timestamp_available)?,
    };
    let timing = if v.timestamp_available {
        v.kernel_timestamp_nanoseconds != 0
    } else {
        v.kernel_timestamp_nanoseconds == 0 && v.interval_nanoseconds == 0
    };
    if !(v.output_id != 0
        && v.commit_id != 0
        && v.presented_generation != 0
        && (v.flags & !PRESENTATION_TIMING_SIMULATED) == 0
        && a == 0
        && b == 0
        && timing)
    {
        return Err(ContractDecodeError::InvalidValue);
    }
    Ok(v)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn malformed_value_does_not_mask_payload_shape() {
        let invalid = vec![0; 16];
        assert_eq!(
            decode_policy_output_vrr_upsert(&invalid),
            Err(ContractDecodeError::InvalidValue)
        );
        assert_eq!(
            decode_policy_output_vrr_upsert(&invalid[..15]),
            Err(ContractDecodeError::Truncated)
        );

        let mut trailing = invalid;
        trailing.push(0);
        assert_eq!(
            decode_policy_output_vrr_upsert(&trailing),
            Err(ContractDecodeError::TrailingData)
        );
    }
}
