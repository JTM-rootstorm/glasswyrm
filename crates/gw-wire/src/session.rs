use core::fmt;

use crate::{ByteReader, ByteWriter, PrimitiveDecodeError};

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u16)]
pub enum SessionState {
    Inactive = 1,
    Active = 2,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u16)]
pub enum SessionStateResult {
    Accepted = 1,
    AlreadyApplied = 2,
    InputUnavailable = 3,
    Failed = 4,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct SessionStateChange {
    pub generation: u64,
    pub state: SessionState,
    pub flags: u32,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct SessionStateAcknowledged {
    pub generation: u64,
    pub state: SessionState,
    pub result: SessionStateResult,
    pub flags: u32,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum SessionDecodeError {
    Truncated,
    TrailingData,
    InvalidValue,
}

impl fmt::Display for SessionDecodeError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(match self {
            Self::Truncated => "GWIPC session payload is truncated",
            Self::TrailingData => "GWIPC session payload has trailing data",
            Self::InvalidValue => "GWIPC session payload contains an invalid value",
        })
    }
}

impl std::error::Error for SessionDecodeError {}

impl From<PrimitiveDecodeError> for SessionDecodeError {
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
pub fn encode_session_state_change(value: &SessionStateChange) -> Vec<u8> {
    let mut writer = ByteWriter::new();
    writer.write_u64(value.generation);
    writer.write_u16(value.state as u16);
    writer.write_u16(0);
    writer.write_u32(value.flags);
    writer.into_bytes()
}

pub fn decode_session_state_change(bytes: &[u8]) -> Result<SessionStateChange, SessionDecodeError> {
    let mut reader = ByteReader::new(bytes);
    let generation = reader.read_u64()?;
    let state = decode_state(reader.read_u16()?)?;
    let reserved = reader.read_u16()?;
    let flags = reader.read_u32()?;
    reader.finish()?;
    if generation == 0 || reserved != 0 || flags != 0 {
        return Err(SessionDecodeError::InvalidValue);
    }
    Ok(SessionStateChange {
        generation,
        state,
        flags,
    })
}

#[must_use]
pub fn encode_session_state_acknowledged(value: &SessionStateAcknowledged) -> Vec<u8> {
    let mut writer = ByteWriter::new();
    writer.write_u64(value.generation);
    writer.write_u16(value.state as u16);
    writer.write_u16(value.result as u16);
    writer.write_u32(value.flags);
    writer.into_bytes()
}

pub fn decode_session_state_acknowledged(
    bytes: &[u8],
) -> Result<SessionStateAcknowledged, SessionDecodeError> {
    let mut reader = ByteReader::new(bytes);
    let generation = reader.read_u64()?;
    let state = decode_state(reader.read_u16()?)?;
    let result = decode_result(reader.read_u16()?)?;
    let flags = reader.read_u32()?;
    reader.finish()?;
    if generation == 0 || flags != 0 {
        return Err(SessionDecodeError::InvalidValue);
    }
    Ok(SessionStateAcknowledged {
        generation,
        state,
        result,
        flags,
    })
}

fn decode_state(value: u16) -> Result<SessionState, SessionDecodeError> {
    match value {
        1 => Ok(SessionState::Inactive),
        2 => Ok(SessionState::Active),
        _ => Err(SessionDecodeError::InvalidValue),
    }
}

fn decode_result(value: u16) -> Result<SessionStateResult, SessionDecodeError> {
    match value {
        1 => Ok(SessionStateResult::Accepted),
        2 => Ok(SessionStateResult::AlreadyApplied),
        3 => Ok(SessionStateResult::InputUnavailable),
        4 => Ok(SessionStateResult::Failed),
        _ => Err(SessionDecodeError::InvalidValue),
    }
}
