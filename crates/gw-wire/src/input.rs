use core::fmt;

use crate::{ByteReader, ByteWriter, PrimitiveDecodeError};

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u16)]
pub enum SyntheticInputResult {
    Accepted = 1,
    Clamped = 2,
    InvalidTransition = 3,
    FocusUnchanged = 4,
    FocusRejected = 5,
    LimitExceeded = 6,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct SyntheticMotion {
    pub input_id: u64,
    pub time_ms: u32,
    pub root_x: i32,
    pub root_y: i32,
    pub flags: u32,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct SyntheticButton {
    pub input_id: u64,
    pub time_ms: u32,
    pub button: u8,
    pub pressed: bool,
    pub flags: u32,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct SyntheticKey {
    pub input_id: u64,
    pub time_ms: u32,
    pub keycode: u8,
    pub pressed: bool,
    pub flags: u32,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct SyntheticBarrier {
    pub input_id: u64,
    pub flags: u32,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct SyntheticInputAcknowledged {
    pub input_id: u64,
    pub time_ms: u32,
    pub result: SyntheticInputResult,
    pub root_x: i32,
    pub root_y: i32,
    pub pointer_window: u32,
    pub focus_window: u32,
    pub state: u16,
    pub delivered_event_count: u32,
    pub flags: u32,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum InputDecodeError {
    Truncated,
    TrailingData,
    InvalidValue,
}

impl fmt::Display for InputDecodeError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(match self {
            Self::Truncated => "GWIPC input payload is truncated",
            Self::TrailingData => "GWIPC input payload has trailing data",
            Self::InvalidValue => "GWIPC input payload contains an invalid value",
        })
    }
}

impl std::error::Error for InputDecodeError {}

impl From<PrimitiveDecodeError> for InputDecodeError {
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
pub fn encode_synthetic_motion(value: &SyntheticMotion) -> Vec<u8> {
    let mut writer = ByteWriter::new();
    writer.write_u64(value.input_id);
    writer.write_u32(value.time_ms);
    writer.write_i32(value.root_x);
    writer.write_i32(value.root_y);
    writer.write_u32(value.flags);
    writer.into_bytes()
}

pub fn decode_synthetic_motion(bytes: &[u8]) -> Result<SyntheticMotion, InputDecodeError> {
    let mut reader = ByteReader::new(bytes);
    let value = SyntheticMotion {
        input_id: reader.read_u64()?,
        time_ms: reader.read_u32()?,
        root_x: reader.read_i32()?,
        root_y: reader.read_i32()?,
        flags: reader.read_u32()?,
    };
    reader.finish()?;
    validate_identity(value.input_id, value.time_ms, value.flags)?;
    Ok(value)
}

#[must_use]
pub fn encode_synthetic_button(value: &SyntheticButton) -> Vec<u8> {
    let mut writer = ByteWriter::new();
    writer.write_u64(value.input_id);
    writer.write_u32(value.time_ms);
    writer.write_u8(value.button);
    writer.write_u8(u8::from(value.pressed));
    writer.write_u16(0);
    writer.write_u32(value.flags);
    writer.into_bytes()
}

pub fn decode_synthetic_button(bytes: &[u8]) -> Result<SyntheticButton, InputDecodeError> {
    let mut reader = ByteReader::new(bytes);
    let input_id = reader.read_u64()?;
    let time_ms = reader.read_u32()?;
    let button = reader.read_u8()?;
    let pressed = reader.read_u8()?;
    let reserved = reader.read_u16()?;
    let flags = reader.read_u32()?;
    reader.finish()?;
    let pressed = decode_bool(pressed)?;
    validate_identity(input_id, time_ms, flags)?;
    if !(1..=5).contains(&button) || reserved != 0 {
        return Err(InputDecodeError::InvalidValue);
    }
    Ok(SyntheticButton {
        input_id,
        time_ms,
        button,
        pressed,
        flags,
    })
}

#[must_use]
pub fn encode_synthetic_key(value: &SyntheticKey) -> Vec<u8> {
    let mut writer = ByteWriter::new();
    writer.write_u64(value.input_id);
    writer.write_u32(value.time_ms);
    writer.write_u8(value.keycode);
    writer.write_u8(u8::from(value.pressed));
    writer.write_u16(0);
    writer.write_u32(value.flags);
    writer.into_bytes()
}

pub fn decode_synthetic_key(bytes: &[u8]) -> Result<SyntheticKey, InputDecodeError> {
    let mut reader = ByteReader::new(bytes);
    let input_id = reader.read_u64()?;
    let time_ms = reader.read_u32()?;
    let keycode = reader.read_u8()?;
    let pressed = reader.read_u8()?;
    let reserved = reader.read_u16()?;
    let flags = reader.read_u32()?;
    reader.finish()?;
    let pressed = decode_bool(pressed)?;
    validate_identity(input_id, time_ms, flags)?;
    if keycode < 8 || reserved != 0 {
        return Err(InputDecodeError::InvalidValue);
    }
    Ok(SyntheticKey {
        input_id,
        time_ms,
        keycode,
        pressed,
        flags,
    })
}

#[must_use]
pub fn encode_synthetic_barrier(value: &SyntheticBarrier) -> Vec<u8> {
    let mut writer = ByteWriter::new();
    writer.write_u64(value.input_id);
    writer.write_u32(value.flags);
    writer.into_bytes()
}

pub fn decode_synthetic_barrier(bytes: &[u8]) -> Result<SyntheticBarrier, InputDecodeError> {
    let mut reader = ByteReader::new(bytes);
    let value = SyntheticBarrier {
        input_id: reader.read_u64()?,
        flags: reader.read_u32()?,
    };
    reader.finish()?;
    if value.input_id == 0 || value.flags != 0 {
        return Err(InputDecodeError::InvalidValue);
    }
    Ok(value)
}

#[must_use]
pub fn encode_synthetic_input_acknowledged(value: &SyntheticInputAcknowledged) -> Vec<u8> {
    let mut writer = ByteWriter::new();
    writer.write_u64(value.input_id);
    writer.write_u32(value.time_ms);
    writer.write_u16(value.result as u16);
    writer.write_i32(value.root_x);
    writer.write_i32(value.root_y);
    writer.write_u32(value.pointer_window);
    writer.write_u32(value.focus_window);
    writer.write_u16(value.state);
    writer.write_u16(0);
    writer.write_u32(value.delivered_event_count);
    writer.write_u32(value.flags);
    writer.into_bytes()
}

pub fn decode_synthetic_input_acknowledged(
    bytes: &[u8],
) -> Result<SyntheticInputAcknowledged, InputDecodeError> {
    let mut reader = ByteReader::new(bytes);
    let input_id = reader.read_u64()?;
    let time_ms = reader.read_u32()?;
    let result = reader.read_u16()?;
    let root_x = reader.read_i32()?;
    let root_y = reader.read_i32()?;
    let pointer_window = reader.read_u32()?;
    let focus_window = reader.read_u32()?;
    let state = reader.read_u16()?;
    let reserved = reader.read_u16()?;
    let delivered_event_count = reader.read_u32()?;
    let flags = reader.read_u32()?;
    reader.finish()?;
    let result = decode_result(result)?;
    validate_identity(input_id, time_ms, flags)?;
    if reserved != 0 {
        return Err(InputDecodeError::InvalidValue);
    }
    Ok(SyntheticInputAcknowledged {
        input_id,
        time_ms,
        result,
        root_x,
        root_y,
        pointer_window,
        focus_window,
        state,
        delivered_event_count,
        flags,
    })
}

fn validate_identity(input_id: u64, time_ms: u32, flags: u32) -> Result<(), InputDecodeError> {
    if input_id == 0 || time_ms == 0 || flags != 0 {
        Err(InputDecodeError::InvalidValue)
    } else {
        Ok(())
    }
}

fn decode_bool(value: u8) -> Result<bool, InputDecodeError> {
    match value {
        0 => Ok(false),
        1 => Ok(true),
        _ => Err(InputDecodeError::InvalidValue),
    }
}

fn decode_result(value: u16) -> Result<SyntheticInputResult, InputDecodeError> {
    match value {
        1 => Ok(SyntheticInputResult::Accepted),
        2 => Ok(SyntheticInputResult::Clamped),
        3 => Ok(SyntheticInputResult::InvalidTransition),
        4 => Ok(SyntheticInputResult::FocusUnchanged),
        5 => Ok(SyntheticInputResult::FocusRejected),
        6 => Ok(SyntheticInputResult::LimitExceeded),
        _ => Err(InputDecodeError::InvalidValue),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn malformed_value_does_not_mask_payload_shape() {
        let mut invalid = vec![0; 20];
        invalid[13] = 2;
        assert_eq!(
            decode_synthetic_button(&invalid),
            Err(InputDecodeError::InvalidValue)
        );
        assert_eq!(
            decode_synthetic_button(&invalid[..19]),
            Err(InputDecodeError::Truncated)
        );

        invalid.push(0);
        assert_eq!(
            decode_synthetic_button(&invalid),
            Err(InputDecodeError::TrailingData)
        );
    }
}
