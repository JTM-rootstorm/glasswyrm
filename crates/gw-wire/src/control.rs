use core::fmt;

use gw_types::{Capabilities, ConnectionId, GWIPC_WIRE_VERSION, RejectReason, Role, WireVersion};

use crate::{ByteReader, ByteWriter, PrimitiveDecodeError};

pub const MAXIMUM_INSTANCE_LABEL_BYTES: usize = 64;
pub const MAXIMUM_DIAGNOSTIC_BYTES: usize = 256;
pub const HARD_MAXIMUM_PAYLOAD: u32 = 1024 * 1024;
pub const HARD_MAXIMUM_FDS: u16 = 16;

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct Hello {
    pub minimum_version: WireVersion,
    pub maximum_version: WireVersion,
    pub sender_role: Role,
    pub offered_capabilities: Capabilities,
    pub required_capabilities: Capabilities,
    pub maximum_payload: u32,
    pub maximum_fd_count: u16,
    pub sender_instance_id: [u8; 16],
    pub name: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct Welcome {
    pub selected_version: WireVersion,
    pub sender_role: Role,
    pub negotiated_capabilities: Capabilities,
    pub negotiated_maximum_payload: u32,
    pub negotiated_maximum_fd_count: u16,
    pub connection_id: ConnectionId,
    pub sender_instance_id: [u8; 16],
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct Reject {
    pub reason: RejectReason,
    pub supported_minimum_version: WireVersion,
    pub supported_maximum_version: WireVersion,
    pub detail: String,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ControlEncodeError {
    LimitExceeded,
}

impl fmt::Display for ControlEncodeError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str("GWIPC control payload exceeds a wire limit")
    }
}

impl std::error::Error for ControlEncodeError {}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ControlDecodeError {
    Truncated,
    TrailingData,
    InvalidValue,
    LimitExceeded,
}

impl fmt::Display for ControlDecodeError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(match self {
            Self::Truncated => "GWIPC control payload is truncated",
            Self::TrailingData => "GWIPC control payload has trailing data",
            Self::InvalidValue => "GWIPC control payload contains an invalid value",
            Self::LimitExceeded => "GWIPC control payload exceeds a wire limit",
        })
    }
}

impl std::error::Error for ControlDecodeError {}

impl From<PrimitiveDecodeError> for ControlDecodeError {
    fn from(error: PrimitiveDecodeError) -> Self {
        match error {
            PrimitiveDecodeError::Truncated => Self::Truncated,
            PrimitiveDecodeError::TrailingData => Self::TrailingData,
            PrimitiveDecodeError::LimitExceeded => Self::LimitExceeded,
            PrimitiveDecodeError::InvalidUtf8 => Self::InvalidValue,
        }
    }
}

pub fn encode_hello(value: &Hello) -> Result<Vec<u8>, ControlEncodeError> {
    let name_size =
        u16::try_from(value.name.len()).map_err(|_| ControlEncodeError::LimitExceeded)?;
    if value.name.len() > MAXIMUM_INSTANCE_LABEL_BYTES {
        return Err(ControlEncodeError::LimitExceeded);
    }
    let mut writer = ByteWriter::new();
    writer.write_u16(value.minimum_version.major);
    writer.write_u16(value.minimum_version.minor);
    writer.write_u16(value.maximum_version.major);
    writer.write_u16(value.maximum_version.minor);
    writer.write_u16(value.sender_role as u16);
    writer.write_u16(0);
    writer.write_u64(value.offered_capabilities.bits());
    writer.write_u64(value.required_capabilities.bits());
    writer.write_u32(value.maximum_payload);
    writer.write_u16(value.maximum_fd_count);
    writer.write_u16(name_size);
    writer.write_bytes(&value.sender_instance_id);
    writer.write_bytes(value.name.as_bytes());
    Ok(writer.into_bytes())
}

pub fn decode_hello(bytes: &[u8]) -> Result<Hello, ControlDecodeError> {
    let mut reader = ByteReader::new(bytes);
    let minimum_version = WireVersion::new(reader.read_u16()?, reader.read_u16()?);
    let maximum_version = WireVersion::new(reader.read_u16()?, reader.read_u16()?);
    let sender_role = decode_peer_role(reader.read_u16()?)?;
    let reserved = reader.read_u16()?;
    let offered_capabilities = Capabilities::from_bits_retain(reader.read_u64()?);
    let required_capabilities =
        Capabilities::from_bits(reader.read_u64()?).ok_or(ControlDecodeError::InvalidValue)?;
    let maximum_payload = reader.read_u32()?;
    let maximum_fd_count = reader.read_u16()?;
    let name_size = usize::from(reader.read_u16()?);
    let sender_instance_id = reader
        .read_bytes(16)?
        .try_into()
        .map_err(|_| ControlDecodeError::Truncated)?;
    if name_size > MAXIMUM_INSTANCE_LABEL_BYTES {
        return Err(ControlDecodeError::LimitExceeded);
    }
    let name = std::str::from_utf8(reader.read_bytes(name_size)?)
        .map_err(|_| ControlDecodeError::InvalidValue)?
        .to_owned();
    reader.finish()?;

    if reserved != 0
        || minimum_version > maximum_version
        || sender_instance_id == [0; 16]
        || maximum_payload == 0
        || maximum_payload > HARD_MAXIMUM_PAYLOAD
        || maximum_fd_count > HARD_MAXIMUM_FDS
    {
        return Err(ControlDecodeError::InvalidValue);
    }

    Ok(Hello {
        minimum_version,
        maximum_version,
        sender_role,
        offered_capabilities,
        required_capabilities,
        maximum_payload,
        maximum_fd_count,
        sender_instance_id,
        name,
    })
}

#[must_use]
pub fn encode_welcome(value: &Welcome) -> Vec<u8> {
    let mut writer = ByteWriter::new();
    writer.write_u16(value.selected_version.major);
    writer.write_u16(value.selected_version.minor);
    writer.write_u16(value.sender_role as u16);
    writer.write_u16(0);
    writer.write_u64(value.negotiated_capabilities.bits());
    writer.write_u32(value.negotiated_maximum_payload);
    writer.write_u16(value.negotiated_maximum_fd_count);
    writer.write_u16(0);
    writer.write_u64(value.connection_id.get());
    writer.write_bytes(&value.sender_instance_id);
    writer.into_bytes()
}

pub fn decode_welcome(bytes: &[u8]) -> Result<Welcome, ControlDecodeError> {
    let mut reader = ByteReader::new(bytes);
    let selected_version = WireVersion::new(reader.read_u16()?, reader.read_u16()?);
    let sender_role = decode_peer_role(reader.read_u16()?)?;
    let reserved1 = reader.read_u16()?;
    let negotiated_capabilities =
        Capabilities::from_bits(reader.read_u64()?).ok_or(ControlDecodeError::InvalidValue)?;
    let negotiated_maximum_payload = reader.read_u32()?;
    let negotiated_maximum_fd_count = reader.read_u16()?;
    let reserved2 = reader.read_u16()?;
    let connection_id = ConnectionId::new(reader.read_u64()?);
    let sender_instance_id = reader
        .read_bytes(16)?
        .try_into()
        .map_err(|_| ControlDecodeError::Truncated)?;
    reader.finish()?;

    if reserved1 != 0
        || reserved2 != 0
        || selected_version != GWIPC_WIRE_VERSION
        || negotiated_maximum_payload == 0
        || negotiated_maximum_payload > HARD_MAXIMUM_PAYLOAD
        || negotiated_maximum_fd_count > HARD_MAXIMUM_FDS
        || connection_id.get() == 0
        || sender_instance_id == [0; 16]
    {
        return Err(ControlDecodeError::InvalidValue);
    }

    Ok(Welcome {
        selected_version,
        sender_role,
        negotiated_capabilities,
        negotiated_maximum_payload,
        negotiated_maximum_fd_count,
        connection_id,
        sender_instance_id,
    })
}

pub fn encode_reject(value: &Reject) -> Result<Vec<u8>, ControlEncodeError> {
    let detail_size =
        u16::try_from(value.detail.len()).map_err(|_| ControlEncodeError::LimitExceeded)?;
    if value.detail.len() > MAXIMUM_DIAGNOSTIC_BYTES {
        return Err(ControlEncodeError::LimitExceeded);
    }
    let mut writer = ByteWriter::new();
    writer.write_u16(value.reason as u16);
    writer.write_u16(detail_size);
    writer.write_u16(value.supported_minimum_version.major);
    writer.write_u16(value.supported_minimum_version.minor);
    writer.write_u16(value.supported_maximum_version.major);
    writer.write_u16(value.supported_maximum_version.minor);
    writer.write_u32(0);
    writer.write_bytes(value.detail.as_bytes());
    Ok(writer.into_bytes())
}

pub fn decode_reject(bytes: &[u8]) -> Result<Reject, ControlDecodeError> {
    let mut reader = ByteReader::new(bytes);
    let reason = RejectReason::try_from(reader.read_u16()?)
        .map_err(|()| ControlDecodeError::InvalidValue)?;
    let detail_size = usize::from(reader.read_u16()?);
    let supported_minimum_version = WireVersion::new(reader.read_u16()?, reader.read_u16()?);
    let supported_maximum_version = WireVersion::new(reader.read_u16()?, reader.read_u16()?);
    let reserved = reader.read_u32()?;
    if detail_size > MAXIMUM_DIAGNOSTIC_BYTES {
        return Err(ControlDecodeError::LimitExceeded);
    }
    let detail = std::str::from_utf8(reader.read_bytes(detail_size)?)
        .map_err(|_| ControlDecodeError::InvalidValue)?
        .to_owned();
    reader.finish()?;

    if reserved != 0 || supported_minimum_version > supported_maximum_version {
        return Err(ControlDecodeError::InvalidValue);
    }
    Ok(Reject {
        reason,
        supported_minimum_version,
        supported_maximum_version,
        detail,
    })
}

fn decode_peer_role(value: u16) -> Result<Role, ControlDecodeError> {
    let role = Role::try_from(value).map_err(|()| ControlDecodeError::InvalidValue)?;
    if role == Role::Unknown {
        Err(ControlDecodeError::InvalidValue)
    } else {
        Ok(role)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn hello() -> Hello {
        Hello {
            minimum_version: GWIPC_WIRE_VERSION,
            maximum_version: GWIPC_WIRE_VERSION,
            sender_role: Role::WindowManager,
            offered_capabilities: Capabilities::FD_PASSING.with(Capabilities::WINDOW_POLICY),
            required_capabilities: Capabilities::WINDOW_POLICY,
            maximum_payload: 65_536,
            maximum_fd_count: 4,
            sender_instance_id: [7; 16],
            name: "gwm".to_owned(),
        }
    }

    #[test]
    fn hello_round_trips() {
        let value = hello();
        assert_eq!(decode_hello(&encode_hello(&value).unwrap()), Ok(value));
    }

    #[test]
    fn hello_rejects_truncation_trailing_bytes_and_bad_utf8() {
        let bytes = encode_hello(&hello()).unwrap();
        assert_eq!(
            decode_hello(&bytes[..bytes.len() - 1]),
            Err(ControlDecodeError::Truncated)
        );

        let mut trailing = bytes.clone();
        trailing.push(0);
        assert_eq!(
            decode_hello(&trailing),
            Err(ControlDecodeError::TrailingData)
        );

        let mut bad_utf8 = bytes;
        *bad_utf8.last_mut().unwrap() = 0xff;
        assert_eq!(
            decode_hello(&bad_utf8),
            Err(ControlDecodeError::InvalidValue)
        );
    }

    #[test]
    fn hello_rejects_unknown_required_capability_before_accepting_peer() {
        let mut bytes = encode_hello(&hello()).unwrap();
        bytes[20..28].copy_from_slice(&(1_u64 << 63).to_le_bytes());
        assert_eq!(decode_hello(&bytes), Err(ControlDecodeError::InvalidValue));
    }

    #[test]
    fn welcome_round_trips_and_enforces_current_version() {
        let value = Welcome {
            selected_version: GWIPC_WIRE_VERSION,
            sender_role: Role::ProtocolServer,
            negotiated_capabilities: Capabilities::FD_PASSING,
            negotiated_maximum_payload: 65_536,
            negotiated_maximum_fd_count: 4,
            connection_id: ConnectionId::new(9),
            sender_instance_id: [3; 16],
        };
        let bytes = encode_welcome(&value);
        assert_eq!(decode_welcome(&bytes), Ok(value));

        let mut wrong_version = bytes;
        wrong_version[0..2].copy_from_slice(&2_u16.to_le_bytes());
        assert_eq!(
            decode_welcome(&wrong_version),
            Err(ControlDecodeError::InvalidValue)
        );
    }

    #[test]
    fn reject_round_trips_and_rejects_invalid_version_range() {
        let value = Reject {
            reason: RejectReason::CapabilityMismatch,
            supported_minimum_version: GWIPC_WIRE_VERSION,
            supported_maximum_version: GWIPC_WIRE_VERSION,
            detail: "missing required capability".to_owned(),
        };
        let bytes = encode_reject(&value).unwrap();
        assert_eq!(decode_reject(&bytes), Ok(value));

        let mut reversed_range = bytes;
        reversed_range[4..6].copy_from_slice(&2_u16.to_le_bytes());
        assert_eq!(
            decode_reject(&reversed_range),
            Err(ControlDecodeError::InvalidValue)
        );
    }

    #[test]
    fn reject_diagnostic_limit_is_checked_before_reading_detail() {
        let mut bytes = vec![0; 16];
        bytes[0..2].copy_from_slice(&(RejectReason::InternalError as u16).to_le_bytes());
        bytes[2..4].copy_from_slice(&257_u16.to_le_bytes());
        assert_eq!(
            decode_reject(&bytes),
            Err(ControlDecodeError::LimitExceeded)
        );
    }
}
