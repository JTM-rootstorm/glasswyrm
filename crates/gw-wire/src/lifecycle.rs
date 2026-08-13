use core::fmt;

use crate::policy::{
    POLICY_WINDOW_UPSERT_WIRE_SIZE, PolicyAppliedState, PolicyDecodeError, PolicyWindowType,
    PolicyWindowUpsert, decode_policy_window_upsert, encode_policy_window_upsert,
};
use crate::{ByteReader, ByteWriter, PrimitiveDecodeError};

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u16)]
pub enum PolicyStackMode {
    None = 0,
    Above = 1,
    Below = 2,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct PolicyLifecycleWindowUpsert {
    pub window: PolicyWindowUpsert,
    pub geometry_serial: u64,
    pub stack_serial: u64,
    pub stack_sibling: u32,
    pub stack_mode: PolicyStackMode,
    pub flags: u32,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct SurfacePolicyUpsert {
    pub surface_id: u64,
    pub x11_window_id: u32,
    pub workspace_id: u32,
    pub window_type: PolicyWindowType,
    pub applied_state: PolicyAppliedState,
    pub focused: bool,
    pub managed: bool,
    pub decoration_eligible: bool,
    pub override_redirect: bool,
    pub attention_requested: bool,
    pub fullscreen_eligible: u16,
    pub direct_scanout_eligible: u16,
    pub flags: u32,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum LifecycleDecodeError {
    Truncated,
    TrailingData,
    InvalidValue,
}

impl fmt::Display for LifecycleDecodeError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(match self {
            Self::Truncated => "GWIPC lifecycle payload is truncated",
            Self::TrailingData => "GWIPC lifecycle payload has trailing data",
            Self::InvalidValue => "GWIPC lifecycle payload contains an invalid value",
        })
    }
}

impl std::error::Error for LifecycleDecodeError {}

impl From<PrimitiveDecodeError> for LifecycleDecodeError {
    fn from(error: PrimitiveDecodeError) -> Self {
        match error {
            PrimitiveDecodeError::Truncated => Self::Truncated,
            PrimitiveDecodeError::TrailingData => Self::TrailingData,
            PrimitiveDecodeError::LimitExceeded | PrimitiveDecodeError::InvalidUtf8 => {
                Self::InvalidValue
            }
        }
    }
}

impl From<PolicyDecodeError> for LifecycleDecodeError {
    fn from(error: PolicyDecodeError) -> Self {
        match error {
            PolicyDecodeError::Truncated => Self::Truncated,
            PolicyDecodeError::TrailingData => Self::TrailingData,
            PolicyDecodeError::InvalidValue => Self::InvalidValue,
        }
    }
}

#[must_use]
pub fn encode_policy_lifecycle_window_upsert(value: &PolicyLifecycleWindowUpsert) -> Vec<u8> {
    let mut writer = ByteWriter::new();
    writer.write_bytes(&encode_policy_window_upsert(&value.window));
    writer.write_u64(value.geometry_serial);
    writer.write_u64(value.stack_serial);
    writer.write_u32(value.stack_sibling);
    writer.write_u16(value.stack_mode as u16);
    writer.write_u16(0);
    writer.write_u32(value.flags);
    writer.write_u32(0);
    writer.into_bytes()
}

pub fn decode_policy_lifecycle_window_upsert(
    bytes: &[u8],
) -> Result<PolicyLifecycleWindowUpsert, LifecycleDecodeError> {
    if bytes.len() < POLICY_WINDOW_UPSERT_WIRE_SIZE {
        return Err(LifecycleDecodeError::Truncated);
    }
    let window = decode_policy_window_upsert(&bytes[..POLICY_WINDOW_UPSERT_WIRE_SIZE])?;
    let mut reader = ByteReader::new(&bytes[POLICY_WINDOW_UPSERT_WIRE_SIZE..]);
    let geometry_serial = reader.read_u64()?;
    let stack_serial = reader.read_u64()?;
    let stack_sibling = reader.read_u32()?;
    let stack_mode = decode_stack_mode(reader.read_u16()?)?;
    let reserved1 = reader.read_u16()?;
    let flags = reader.read_u32()?;
    let reserved2 = reader.read_u32()?;
    reader.finish()?;
    let no_stack = stack_serial == 0;
    if reserved1 != 0
        || reserved2 != 0
        || flags != 0
        || (no_stack && (stack_sibling != 0 || stack_mode != PolicyStackMode::None))
        || (!no_stack && stack_mode == PolicyStackMode::None)
        || stack_sibling == window.window_id
    {
        return Err(LifecycleDecodeError::InvalidValue);
    }
    Ok(PolicyLifecycleWindowUpsert {
        window,
        geometry_serial,
        stack_serial,
        stack_sibling,
        stack_mode,
        flags,
    })
}

#[must_use]
pub fn encode_surface_policy_upsert(value: &SurfacePolicyUpsert) -> Vec<u8> {
    let mut writer = ByteWriter::new();
    writer.write_u64(value.surface_id);
    writer.write_u32(value.x11_window_id);
    writer.write_u32(value.workspace_id);
    writer.write_u16(value.window_type as u16);
    writer.write_u16(value.applied_state as u16);
    writer.write_u8(u8::from(value.focused));
    writer.write_u8(u8::from(value.managed));
    writer.write_u8(u8::from(value.decoration_eligible));
    writer.write_u8(u8::from(value.override_redirect));
    writer.write_u8(u8::from(value.attention_requested));
    writer.write_u8(value.fullscreen_eligible as u8);
    writer.write_u8(value.direct_scanout_eligible as u8);
    writer.write_u8(0);
    writer.write_u32(value.flags);
    writer.write_u32(0);
    writer.into_bytes()
}

pub fn decode_surface_policy_upsert(
    bytes: &[u8],
) -> Result<SurfacePolicyUpsert, LifecycleDecodeError> {
    let mut reader = ByteReader::new(bytes);
    let surface_id = reader.read_u64()?;
    let x11_window_id = reader.read_u32()?;
    let workspace_id = reader.read_u32()?;
    let window_type = decode_window_type(reader.read_u16()?)?;
    let applied_state = decode_applied_state(reader.read_u16()?)?;
    let focused = decode_bool(reader.read_u8()?)?;
    let managed = decode_bool(reader.read_u8()?)?;
    let decoration_eligible = decode_bool(reader.read_u8()?)?;
    let override_redirect = decode_bool(reader.read_u8()?)?;
    let attention_requested = decode_bool(reader.read_u8()?)?;
    let fullscreen_eligible = u16::from(reader.read_u8()?);
    let direct_scanout_eligible = u16::from(reader.read_u8()?);
    let reserved1 = reader.read_u8()?;
    let flags = reader.read_u32()?;
    let reserved2 = reader.read_u32()?;
    reader.finish()?;
    if surface_id == 0
        || x11_window_id == 0
        || workspace_id == 0
        || fullscreen_eligible > 2
        || direct_scanout_eligible > 2
        || reserved1 != 0
        || flags != 0
        || reserved2 != 0
    {
        return Err(LifecycleDecodeError::InvalidValue);
    }
    Ok(SurfacePolicyUpsert {
        surface_id,
        x11_window_id,
        workspace_id,
        window_type,
        applied_state,
        focused,
        managed,
        decoration_eligible,
        override_redirect,
        attention_requested,
        fullscreen_eligible,
        direct_scanout_eligible,
        flags,
    })
}

fn decode_bool(value: u8) -> Result<bool, LifecycleDecodeError> {
    match value {
        0 => Ok(false),
        1 => Ok(true),
        _ => Err(LifecycleDecodeError::InvalidValue),
    }
}

fn decode_window_type(value: u16) -> Result<PolicyWindowType, LifecycleDecodeError> {
    match value {
        0 => Ok(PolicyWindowType::Unknown),
        1 => Ok(PolicyWindowType::Normal),
        2 => Ok(PolicyWindowType::Dialog),
        3 => Ok(PolicyWindowType::Utility),
        _ => Err(LifecycleDecodeError::InvalidValue),
    }
}

fn decode_applied_state(value: u16) -> Result<PolicyAppliedState, LifecycleDecodeError> {
    match value {
        1 => Ok(PolicyAppliedState::Normal),
        2 => Ok(PolicyAppliedState::Maximized),
        3 => Ok(PolicyAppliedState::Fullscreen),
        4 => Ok(PolicyAppliedState::Minimized),
        _ => Err(LifecycleDecodeError::InvalidValue),
    }
}

fn decode_stack_mode(value: u16) -> Result<PolicyStackMode, LifecycleDecodeError> {
    match value {
        0 => Ok(PolicyStackMode::None),
        1 => Ok(PolicyStackMode::Above),
        2 => Ok(PolicyStackMode::Below),
        _ => Err(LifecycleDecodeError::InvalidValue),
    }
}
