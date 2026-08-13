use core::fmt;

use crate::{ByteReader, ByteWriter, PrimitiveDecodeError};

pub const POLICY_WINDOW_UPSERT_WIRE_SIZE: usize = 80;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u16)]
pub enum PolicyWindowType {
    Unknown = 0,
    Normal = 1,
    Dialog = 2,
    Utility = 3,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u16)]
pub enum PolicyMapIntent {
    Unmapped = 0,
    WantsMap = 1,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u16)]
pub enum PolicyAppliedState {
    Normal = 1,
    Maximized = 2,
    Fullscreen = 3,
    Minimized = 4,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u16)]
pub enum PolicyResult {
    Accepted = 1,
    RejectedIncompleteSnapshot = 2,
    RejectedInvalidContext = 3,
    RejectedInvalidWindow = 4,
    RejectedUnknownReference = 5,
    RejectedLimit = 6,
    RejectedUnsupportedMetadata = 7,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct PolicyContextUpsert {
    pub root_window_id: u32,
    pub workspace_id: u32,
    pub output_id: u64,
    pub work_x: i32,
    pub work_y: i32,
    pub work_width: u32,
    pub work_height: u32,
    pub flags: u32,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct PolicyWindowUpsert {
    pub window_id: u32,
    pub parent_window_id: u32,
    pub transient_for: u32,
    pub workspace_id: u32,
    pub requested_x: i32,
    pub requested_y: i32,
    pub requested_width: u32,
    pub requested_height: u32,
    pub border_width: u32,
    pub window_type: PolicyWindowType,
    pub map_intent: PolicyMapIntent,
    pub override_redirect: bool,
    pub decoration_preference: u16,
    pub fullscreen_requested: bool,
    pub maximized_requested: bool,
    pub minimized_requested: bool,
    pub attention_requested: bool,
    pub creation_serial: u64,
    pub map_serial: u64,
    pub focus_serial: u64,
    pub flags: u32,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct PolicyWindowRemove {
    pub window_id: u32,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct PolicyCommit {
    pub commit_id: u64,
    pub producer_generation: u64,
    pub flags: u32,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct PolicyWindowState {
    pub window_id: u32,
    pub transient_for: u32,
    pub workspace_id: u32,
    pub output_id: u64,
    pub final_x: i32,
    pub final_y: i32,
    pub final_width: u32,
    pub final_height: u32,
    pub stacking: i32,
    pub window_type: PolicyWindowType,
    pub applied_state: PolicyAppliedState,
    pub visible: bool,
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
pub struct PolicyAcknowledged {
    pub commit_id: u64,
    pub producer_generation: u64,
    pub applied_generation: u64,
    pub policy_hash: u64,
    pub window_count: u32,
    pub result: PolicyResult,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct PolicyBindingsUpsert {
    pub move_modifiers: u16,
    pub resize_modifiers: u16,
    pub close_modifiers: u16,
    pub move_button: u8,
    pub resize_button: u8,
    pub close_keysym: u32,
    pub minimum_width: u32,
    pub minimum_height: u32,
    pub raise_on_focus: bool,
    pub consume_wm_bindings: bool,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum PolicyDecodeError {
    Truncated,
    TrailingData,
    InvalidValue,
}

impl fmt::Display for PolicyDecodeError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(match self {
            Self::Truncated => "GWIPC policy payload is truncated",
            Self::TrailingData => "GWIPC policy payload has trailing data",
            Self::InvalidValue => "GWIPC policy payload contains an invalid value",
        })
    }
}

impl std::error::Error for PolicyDecodeError {}

impl From<PrimitiveDecodeError> for PolicyDecodeError {
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

#[must_use]
pub fn encode_policy_context_upsert(value: &PolicyContextUpsert) -> Vec<u8> {
    let mut writer = ByteWriter::new();
    writer.write_u32(value.root_window_id);
    writer.write_u32(value.workspace_id);
    writer.write_u64(value.output_id);
    writer.write_i32(value.work_x);
    writer.write_i32(value.work_y);
    writer.write_u32(value.work_width);
    writer.write_u32(value.work_height);
    writer.write_u32(value.flags);
    writer.write_u32(0);
    writer.into_bytes()
}

pub fn decode_policy_context_upsert(
    bytes: &[u8],
) -> Result<PolicyContextUpsert, PolicyDecodeError> {
    let mut reader = ByteReader::new(bytes);
    let value = PolicyContextUpsert {
        root_window_id: reader.read_u32()?,
        workspace_id: reader.read_u32()?,
        output_id: reader.read_u64()?,
        work_x: reader.read_i32()?,
        work_y: reader.read_i32()?,
        work_width: reader.read_u32()?,
        work_height: reader.read_u32()?,
        flags: reader.read_u32()?,
    };
    let reserved = reader.read_u32()?;
    reader.finish()?;
    if value.root_window_id == 0
        || value.workspace_id == 0
        || value.output_id == 0
        || !valid_extent(value.work_x, value.work_width)
        || !valid_extent(value.work_y, value.work_height)
        || value.flags != 0
        || reserved != 0
    {
        return Err(PolicyDecodeError::InvalidValue);
    }
    Ok(value)
}

#[must_use]
pub fn encode_policy_window_upsert(value: &PolicyWindowUpsert) -> Vec<u8> {
    let mut writer = ByteWriter::with_capacity(POLICY_WINDOW_UPSERT_WIRE_SIZE);
    writer.write_u32(value.window_id);
    writer.write_u32(value.parent_window_id);
    writer.write_u32(value.transient_for);
    writer.write_u32(value.workspace_id);
    writer.write_i32(value.requested_x);
    writer.write_i32(value.requested_y);
    writer.write_u32(value.requested_width);
    writer.write_u32(value.requested_height);
    writer.write_u32(value.border_width);
    writer.write_u16(value.window_type as u16);
    writer.write_u16(value.map_intent as u16);
    writer.write_u8(u8::from(value.override_redirect));
    writer.write_u8(value.decoration_preference as u8);
    writer.write_u8(u8::from(value.fullscreen_requested));
    writer.write_u8(u8::from(value.maximized_requested));
    writer.write_u8(u8::from(value.minimized_requested));
    writer.write_u8(u8::from(value.attention_requested));
    writer.write_u16(0);
    writer.write_u64(value.creation_serial);
    writer.write_u64(value.map_serial);
    writer.write_u64(value.focus_serial);
    writer.write_u32(value.flags);
    writer.write_u32(0);
    writer.into_bytes()
}

pub fn decode_policy_window_upsert(bytes: &[u8]) -> Result<PolicyWindowUpsert, PolicyDecodeError> {
    let mut reader = ByteReader::new(bytes);
    let window_id = reader.read_u32()?;
    let parent_window_id = reader.read_u32()?;
    let transient_for = reader.read_u32()?;
    let workspace_id = reader.read_u32()?;
    let requested_x = reader.read_i32()?;
    let requested_y = reader.read_i32()?;
    let requested_width = reader.read_u32()?;
    let requested_height = reader.read_u32()?;
    let border_width = reader.read_u32()?;
    let window_type = reader.read_u16()?;
    let map_intent = reader.read_u16()?;
    let override_redirect = reader.read_u8()?;
    let decoration_preference = u16::from(reader.read_u8()?);
    let fullscreen_requested = reader.read_u8()?;
    let maximized_requested = reader.read_u8()?;
    let minimized_requested = reader.read_u8()?;
    let attention_requested = reader.read_u8()?;
    let reserved16 = reader.read_u16()?;
    let creation_serial = reader.read_u64()?;
    let map_serial = reader.read_u64()?;
    let focus_serial = reader.read_u64()?;
    let flags = reader.read_u32()?;
    let reserved32 = reader.read_u32()?;
    reader.finish()?;
    let window_type = decode_window_type(window_type)?;
    let map_intent = decode_map_intent(map_intent)?;
    let override_redirect = decode_bool(override_redirect)?;
    let fullscreen_requested = decode_bool(fullscreen_requested)?;
    let maximized_requested = decode_bool(maximized_requested)?;
    let minimized_requested = decode_bool(minimized_requested)?;
    let attention_requested = decode_bool(attention_requested)?;
    if window_id == 0
        || requested_width == 0
        || requested_height == 0
        || decoration_preference > 2
        || creation_serial == 0
        || (map_intent != PolicyMapIntent::Unmapped && map_serial == 0)
        || flags & !0x7 != 0
        || reserved16 != 0
        || reserved32 != 0
    {
        return Err(PolicyDecodeError::InvalidValue);
    }
    Ok(PolicyWindowUpsert {
        window_id,
        parent_window_id,
        transient_for,
        workspace_id,
        requested_x,
        requested_y,
        requested_width,
        requested_height,
        border_width,
        window_type,
        map_intent,
        override_redirect,
        decoration_preference,
        fullscreen_requested,
        maximized_requested,
        minimized_requested,
        attention_requested,
        creation_serial,
        map_serial,
        focus_serial,
        flags,
    })
}

#[must_use]
pub fn encode_policy_window_remove(value: &PolicyWindowRemove) -> Vec<u8> {
    let mut writer = ByteWriter::new();
    writer.write_u32(value.window_id);
    writer.write_u32(0);
    writer.into_bytes()
}

pub fn decode_policy_window_remove(bytes: &[u8]) -> Result<PolicyWindowRemove, PolicyDecodeError> {
    let mut reader = ByteReader::new(bytes);
    let value = PolicyWindowRemove {
        window_id: reader.read_u32()?,
    };
    let reserved = reader.read_u32()?;
    reader.finish()?;
    if value.window_id == 0 || reserved != 0 {
        return Err(PolicyDecodeError::InvalidValue);
    }
    Ok(value)
}

#[must_use]
pub fn encode_policy_commit(value: &PolicyCommit) -> Vec<u8> {
    let mut writer = ByteWriter::new();
    writer.write_u64(value.commit_id);
    writer.write_u64(value.producer_generation);
    writer.write_u32(value.flags);
    writer.write_u32(0);
    writer.into_bytes()
}

pub fn decode_policy_commit(bytes: &[u8]) -> Result<PolicyCommit, PolicyDecodeError> {
    let mut reader = ByteReader::new(bytes);
    let value = PolicyCommit {
        commit_id: reader.read_u64()?,
        producer_generation: reader.read_u64()?,
        flags: reader.read_u32()?,
    };
    let reserved = reader.read_u32()?;
    reader.finish()?;
    if value.commit_id == 0 || value.producer_generation == 0 || value.flags != 0 || reserved != 0 {
        return Err(PolicyDecodeError::InvalidValue);
    }
    Ok(value)
}

#[must_use]
pub fn encode_policy_window_state(value: &PolicyWindowState) -> Vec<u8> {
    let mut writer = ByteWriter::new();
    writer.write_u32(value.window_id);
    writer.write_u32(value.transient_for);
    writer.write_u32(value.workspace_id);
    writer.write_u32(0);
    writer.write_u64(value.output_id);
    writer.write_i32(value.final_x);
    writer.write_i32(value.final_y);
    writer.write_u32(value.final_width);
    writer.write_u32(value.final_height);
    writer.write_i32(value.stacking);
    writer.write_u16(value.window_type as u16);
    writer.write_u16(value.applied_state as u16);
    for value in [
        value.visible,
        value.focused,
        value.managed,
        value.decoration_eligible,
        value.override_redirect,
        value.attention_requested,
    ] {
        writer.write_u8(u8::from(value));
    }
    writer.write_u8(value.fullscreen_eligible as u8);
    writer.write_u8(value.direct_scanout_eligible as u8);
    writer.write_u32(value.flags);
    writer.write_u32(0);
    writer.into_bytes()
}

pub fn decode_policy_window_state(bytes: &[u8]) -> Result<PolicyWindowState, PolicyDecodeError> {
    let mut reader = ByteReader::new(bytes);
    let window_id = reader.read_u32()?;
    let transient_for = reader.read_u32()?;
    let workspace_id = reader.read_u32()?;
    let reserved1 = reader.read_u32()?;
    let output_id = reader.read_u64()?;
    let final_x = reader.read_i32()?;
    let final_y = reader.read_i32()?;
    let final_width = reader.read_u32()?;
    let final_height = reader.read_u32()?;
    let stacking = reader.read_i32()?;
    let window_type = reader.read_u16()?;
    let applied_state = reader.read_u16()?;
    let visible = reader.read_u8()?;
    let focused = reader.read_u8()?;
    let managed = reader.read_u8()?;
    let decoration_eligible = reader.read_u8()?;
    let override_redirect = reader.read_u8()?;
    let attention_requested = reader.read_u8()?;
    let fullscreen_eligible = u16::from(reader.read_u8()?);
    let direct_scanout_eligible = u16::from(reader.read_u8()?);
    let flags = reader.read_u32()?;
    let reserved2 = reader.read_u32()?;
    reader.finish()?;
    let window_type = decode_window_type(window_type)?;
    let applied_state = decode_applied_state(applied_state)?;
    let visible = decode_bool(visible)?;
    let focused = decode_bool(focused)?;
    let managed = decode_bool(managed)?;
    let decoration_eligible = decode_bool(decoration_eligible)?;
    let override_redirect = decode_bool(override_redirect)?;
    let attention_requested = decode_bool(attention_requested)?;
    if window_id == 0
        || workspace_id == 0
        || output_id == 0
        || final_width == 0
        || final_height == 0
        || fullscreen_eligible > 2
        || direct_scanout_eligible > 2
        || flags != 0
        || reserved1 != 0
        || reserved2 != 0
    {
        return Err(PolicyDecodeError::InvalidValue);
    }
    Ok(PolicyWindowState {
        window_id,
        transient_for,
        workspace_id,
        output_id,
        final_x,
        final_y,
        final_width,
        final_height,
        stacking,
        window_type,
        applied_state,
        visible,
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

#[must_use]
pub fn encode_policy_acknowledged(value: &PolicyAcknowledged) -> Vec<u8> {
    let mut writer = ByteWriter::new();
    writer.write_u64(value.commit_id);
    writer.write_u64(value.producer_generation);
    writer.write_u64(value.applied_generation);
    writer.write_u64(value.policy_hash);
    writer.write_u32(value.window_count);
    writer.write_u16(value.result as u16);
    writer.write_u16(0);
    writer.into_bytes()
}

pub fn decode_policy_acknowledged(bytes: &[u8]) -> Result<PolicyAcknowledged, PolicyDecodeError> {
    let mut reader = ByteReader::new(bytes);
    let commit_id = reader.read_u64()?;
    let producer_generation = reader.read_u64()?;
    let applied_generation = reader.read_u64()?;
    let policy_hash = reader.read_u64()?;
    let window_count = reader.read_u32()?;
    let result = reader.read_u16()?;
    let reserved = reader.read_u16()?;
    reader.finish()?;
    let result = decode_policy_result(result)?;
    if commit_id == 0 || producer_generation == 0 || reserved != 0 {
        return Err(PolicyDecodeError::InvalidValue);
    }
    Ok(PolicyAcknowledged {
        commit_id,
        producer_generation,
        applied_generation,
        policy_hash,
        window_count,
        result,
    })
}

#[must_use]
pub fn encode_policy_bindings_upsert(value: &PolicyBindingsUpsert) -> Vec<u8> {
    let mut writer = ByteWriter::new();
    writer.write_u16(value.move_modifiers);
    writer.write_u16(value.resize_modifiers);
    writer.write_u16(value.close_modifiers);
    writer.write_u16(0);
    writer.write_u8(value.move_button);
    writer.write_u8(value.resize_button);
    writer.write_u16(0);
    writer.write_u32(value.close_keysym);
    writer.write_u32(value.minimum_width);
    writer.write_u32(value.minimum_height);
    writer.write_u8(u8::from(value.raise_on_focus));
    writer.write_u8(u8::from(value.consume_wm_bindings));
    writer.write_u16(0);
    writer.write_u32(0);
    writer.into_bytes()
}

pub fn decode_policy_bindings_upsert(
    bytes: &[u8],
) -> Result<PolicyBindingsUpsert, PolicyDecodeError> {
    let mut reader = ByteReader::new(bytes);
    let move_modifiers = reader.read_u16()?;
    let resize_modifiers = reader.read_u16()?;
    let close_modifiers = reader.read_u16()?;
    let reserved1 = reader.read_u16()?;
    let move_button = reader.read_u8()?;
    let resize_button = reader.read_u8()?;
    let reserved2 = reader.read_u16()?;
    let close_keysym = reader.read_u32()?;
    let minimum_width = reader.read_u32()?;
    let minimum_height = reader.read_u32()?;
    let raise_on_focus = reader.read_u8()?;
    let consume_wm_bindings = reader.read_u8()?;
    let reserved3 = reader.read_u16()?;
    let reserved4 = reader.read_u32()?;
    reader.finish()?;
    let raise_on_focus = decode_bool(raise_on_focus)?;
    let consume_wm_bindings = decode_bool(consume_wm_bindings)?;
    if (move_modifiers | resize_modifiers | close_modifiers) & !0x00ff != 0
        || !(1..=9).contains(&move_button)
        || !(1..=9).contains(&resize_button)
        || close_keysym == 0
        || minimum_width == 0
        || minimum_height == 0
        || minimum_width > 16_384
        || minimum_height > 16_384
        || reserved1 != 0
        || reserved2 != 0
        || reserved3 != 0
        || reserved4 != 0
    {
        return Err(PolicyDecodeError::InvalidValue);
    }
    Ok(PolicyBindingsUpsert {
        move_modifiers,
        resize_modifiers,
        close_modifiers,
        move_button,
        resize_button,
        close_keysym,
        minimum_width,
        minimum_height,
        raise_on_focus,
        consume_wm_bindings,
    })
}

fn valid_extent(position: i32, size: u32) -> bool {
    size != 0 && i64::from(position) + i64::from(size) - 1 <= i64::from(i32::MAX)
}

fn decode_bool(value: u8) -> Result<bool, PolicyDecodeError> {
    match value {
        0 => Ok(false),
        1 => Ok(true),
        _ => Err(PolicyDecodeError::InvalidValue),
    }
}

fn decode_window_type(value: u16) -> Result<PolicyWindowType, PolicyDecodeError> {
    match value {
        0 => Ok(PolicyWindowType::Unknown),
        1 => Ok(PolicyWindowType::Normal),
        2 => Ok(PolicyWindowType::Dialog),
        3 => Ok(PolicyWindowType::Utility),
        _ => Err(PolicyDecodeError::InvalidValue),
    }
}

fn decode_map_intent(value: u16) -> Result<PolicyMapIntent, PolicyDecodeError> {
    match value {
        0 => Ok(PolicyMapIntent::Unmapped),
        1 => Ok(PolicyMapIntent::WantsMap),
        _ => Err(PolicyDecodeError::InvalidValue),
    }
}

fn decode_applied_state(value: u16) -> Result<PolicyAppliedState, PolicyDecodeError> {
    match value {
        1 => Ok(PolicyAppliedState::Normal),
        2 => Ok(PolicyAppliedState::Maximized),
        3 => Ok(PolicyAppliedState::Fullscreen),
        4 => Ok(PolicyAppliedState::Minimized),
        _ => Err(PolicyDecodeError::InvalidValue),
    }
}

fn decode_policy_result(value: u16) -> Result<PolicyResult, PolicyDecodeError> {
    match value {
        1 => Ok(PolicyResult::Accepted),
        2 => Ok(PolicyResult::RejectedIncompleteSnapshot),
        3 => Ok(PolicyResult::RejectedInvalidContext),
        4 => Ok(PolicyResult::RejectedInvalidWindow),
        5 => Ok(PolicyResult::RejectedUnknownReference),
        6 => Ok(PolicyResult::RejectedLimit),
        7 => Ok(PolicyResult::RejectedUnsupportedMetadata),
        _ => Err(PolicyDecodeError::InvalidValue),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn malformed_value_does_not_mask_payload_shape() {
        let mut invalid = vec![0; POLICY_WINDOW_UPSERT_WIRE_SIZE];
        invalid[40..42].copy_from_slice(&u16::MAX.to_le_bytes());
        assert_eq!(
            decode_policy_window_upsert(&invalid),
            Err(PolicyDecodeError::InvalidValue)
        );
        assert_eq!(
            decode_policy_window_upsert(&invalid[..POLICY_WINDOW_UPSERT_WIRE_SIZE - 1]),
            Err(PolicyDecodeError::Truncated)
        );

        invalid.push(0);
        assert_eq!(
            decode_policy_window_upsert(&invalid),
            Err(PolicyDecodeError::TrailingData)
        );
    }
}
