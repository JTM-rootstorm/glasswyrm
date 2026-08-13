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

fn read_bool(r: &mut ByteReader<'_>) -> Result<bool, ContractDecodeError> {
    match r.read_u8()? {
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
    let connector_property_present = read_bool(&mut r)?;
    let hardware_capable = read_bool(&mut r)?;
    let kms_controllable = read_bool(&mut r)?;
    let simulated = read_bool(&mut r)?;
    let range_available = read_bool(&mut r)?;
    let atomic_required = read_bool(&mut r)?;
    let reserved16 = r.read_u16()?;
    let minimum_refresh_millihertz = r.read_u32()?;
    let maximum_refresh_millihertz = r.read_u32()?;
    let reason_flags = r.read_u64()?;
    let flags = r.read_u32()?;
    let reserved32 = r.read_u32()?;
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
    finish(
        r,
        output_id != 0
            && reserved16 == 0
            && reserved32 == 0
            && flags == 0
            && range
            && reasons(reason_flags)
            && (!hardware_capable || connector_property_present)
            && (!simulated || !hardware_capable),
    )?;
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
    let v = OutputVrrPolicyUpsert {
        output_id: r.read_u64()?,
        mode: r.read_u16()?.try_into()?,
        flags: {
            if r.read_u16()? != 0 {
                return Err(ContractDecodeError::InvalidValue);
            }
            r.read_u32()?
        },
    };
    finish(r, v.output_id != 0 && v.flags == 0)?;
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
    let v = OutputVrrStateUpsert {
        output_id: r.read_u64()?,
        requested_mode: r.read_u16()?.try_into()?,
        decision: r.read_u16()?.try_into()?,
        desired_enabled: read_bool(&mut r)?,
        effective_enabled: read_bool(&mut r)?,
        property_readback_valid: read_bool(&mut r)?,
        session_active: read_bool(&mut r)?,
        candidate_window_id: r.read_u32()?,
        candidate_surface_id: {
            if r.read_u32()? != 0 {
                return Err(ContractDecodeError::InvalidValue);
            }
            r.read_u64()?
        },
        reason_flags: r.read_u64()?,
        state_generation: r.read_u64()?,
        transition_serial: r.read_u64()?,
        last_commit_id: r.read_u64()?,
        last_presented_generation: r.read_u64()?,
        last_flip_sequence: r.read_u32()?,
        flags: r.read_u32()?,
        last_flip_timestamp_nanoseconds: r.read_u64()?,
        last_interval_nanoseconds: r.read_u64()?,
    };
    let simulated = v.reason_flags & VRR_REASON_SIMULATED_HEADLESS != 0;
    let enabled = !v.effective_enabled
        || (v.desired_enabled
            && v.decision == VrrDecision::Enabled
            && (v.property_readback_valid || simulated));
    finish(
        r,
        v.output_id != 0
            && v.flags == 0
            && reasons(v.reason_flags)
            && v.state_generation != 0
            && enabled
            && (v.decision == VrrDecision::Enabled || v.reason_flags != 0),
    )?;
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
    let preference = r.read_u16()?.try_into()?;
    let policy_selected = read_bool(&mut r)?;
    let policy_eligible = read_bool(&mut r)?;
    let focused = read_bool(&mut r)?;
    let fullscreen = read_bool(&mut r)?;
    let borderless_fullscreen = read_bool(&mut r)?;
    let exclusive_output_membership = read_bool(&mut r)?;
    let reason_flags = r.read_u64()?;
    let policy_generation = r.read_u64()?;
    let flags = r.read_u32()?;
    let b = r.read_u32()?;
    let v = SurfaceVrrState {
        surface_id,
        window_id,
        output_id,
        preference,
        policy_selected,
        policy_eligible,
        focused,
        fullscreen,
        borderless_fullscreen,
        exclusive_output_membership,
        reason_flags,
        policy_generation,
        flags,
    };
    finish(
        r,
        surface_id != 0
            && window_id != 0
            && output_id != 0
            && a == 0
            && b == 0
            && flags == 0
            && reasons(reason_flags)
            && policy_generation != 0
            && (!policy_selected || policy_eligible),
    )?;
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
    let v = PolicyWindowVrrUpsert {
        window_id: r.read_u32()?,
        preference: r.read_u16()?.try_into()?,
        flags: {
            if r.read_u16()? != 0 {
                return Err(ContractDecodeError::InvalidValue);
            }
            r.read_u32()?
        },
    };
    let z = r.read_u32()?;
    finish(r, v.window_id != 0 && v.flags == 0 && z == 0)?;
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
    let v = PolicyOutputVrrUpsert {
        output_id: r.read_u64()?,
        mode: r.read_u16()?.try_into()?,
        hardware_capable: read_bool(&mut r)?,
        kms_controllable: read_bool(&mut r)?,
        flags: r.read_u32()?,
    };
    finish(r, v.output_id != 0 && v.flags == 0)?;
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
    let preference = r.read_u16()?.try_into()?;
    let selected = read_bool(&mut r)?;
    let eligible = read_bool(&mut r)?;
    let focused = read_bool(&mut r)?;
    let fullscreen = read_bool(&mut r)?;
    let borderless_fullscreen = read_bool(&mut r)?;
    let exclusive_output_membership = read_bool(&mut r)?;
    let reason_flags = r.read_u64()?;
    let flags = r.read_u32()?;
    let b = r.read_u32()?;
    let v = PolicyWindowVrrState {
        window_id,
        output_id,
        preference,
        selected,
        eligible,
        focused,
        fullscreen,
        borderless_fullscreen,
        exclusive_output_membership,
        reason_flags,
        flags,
    };
    finish(
        r,
        window_id != 0
            && output_id != 0
            && a == 0
            && b == 0
            && flags == 0
            && reasons(reason_flags)
            && (!selected || eligible),
    )?;
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
    let v = PolicyOutputVrrState {
        output_id: r.read_u64()?,
        mode: r.read_u16()?.try_into()?,
        desired_enabled: read_bool(&mut r)?,
        candidate_required: read_bool(&mut r)?,
        selected_window_id: r.read_u32()?,
        reason_flags: r.read_u64()?,
        flags: r.read_u32()?,
    };
    let z = r.read_u32()?;
    finish(
        r,
        v.output_id != 0
            && z == 0
            && v.flags == 0
            && reasons(v.reason_flags)
            && (!v.candidate_required || v.selected_window_id != 0 || !v.desired_enabled),
    )?;
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
    let v = PresentationTiming {
        output_id: r.read_u64()?,
        commit_id: r.read_u64()?,
        presented_generation: r.read_u64()?,
        flip_sequence: r.read_u32()?,
        flags: r.read_u32()?,
        kernel_timestamp_nanoseconds: r.read_u64()?,
        interval_nanoseconds: r.read_u64()?,
        effective_vrr_enabled: read_bool(&mut r)?,
        timestamp_available: read_bool(&mut r)?,
    };
    let a = r.read_u16()?;
    let b = r.read_u32()?;
    let timing = if v.timestamp_available {
        v.kernel_timestamp_nanoseconds != 0
    } else {
        v.kernel_timestamp_nanoseconds == 0 && v.interval_nanoseconds == 0
    };
    finish(
        r,
        v.output_id != 0
            && v.commit_id != 0
            && v.presented_generation != 0
            && (v.flags & !PRESENTATION_TIMING_SIMULATED) == 0
            && a == 0
            && b == 0
            && timing,
    )?;
    Ok(v)
}
