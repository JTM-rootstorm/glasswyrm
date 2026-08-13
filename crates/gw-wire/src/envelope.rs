use core::fmt;

use gw_types::{GWIPC_WIRE_VERSION, MessageFlags, MessageType, Sequence, WireVersion};

use crate::{ByteReader, ByteWriter, PrimitiveDecodeError};

pub const GWIPC_MAGIC: [u8; 4] = *b"GWIP";
pub const GWIPC_ENVELOPE_SIZE: usize = 40;

/// Negotiated limits needed to validate an incoming envelope.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct DecodeLimits {
    pub maximum_payload: u32,
}

impl DecodeLimits {
    #[must_use]
    pub const fn new(maximum_payload: u32) -> Self {
        Self { maximum_payload }
    }
}

/// The fixed GWIPC record header.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Envelope {
    pub version: WireVersion,
    pub message_type: MessageType,
    pub flags: MessageFlags,
    pub payload_size: u32,
    pub fd_count: u16,
    pub sequence: Sequence,
    pub reply_to: Sequence,
}

impl Envelope {
    #[must_use]
    pub const fn request(message_type: MessageType, sequence: Sequence, payload_size: u32) -> Self {
        Self {
            version: GWIPC_WIRE_VERSION,
            message_type,
            flags: MessageFlags::from_bits(0).unwrap(),
            payload_size,
            fd_count: 0,
            sequence,
            reply_to: Sequence::new(0),
        }
    }
}

/// A legacy-compatible envelope validation failure.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum EnvelopeDecodeError {
    Truncated,
    InvalidValue,
    LimitExceeded,
    SizeMismatch,
}

impl fmt::Display for EnvelopeDecodeError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(match self {
            Self::Truncated => "GWIPC envelope is truncated",
            Self::InvalidValue => "GWIPC envelope contains an invalid value",
            Self::LimitExceeded => "GWIPC envelope exceeds a negotiated limit",
            Self::SizeMismatch => "GWIPC record or descriptor count does not match its envelope",
        })
    }
}

impl std::error::Error for EnvelopeDecodeError {}

impl From<PrimitiveDecodeError> for EnvelopeDecodeError {
    fn from(error: PrimitiveDecodeError) -> Self {
        match error {
            PrimitiveDecodeError::Truncated => Self::Truncated,
            PrimitiveDecodeError::TrailingData
            | PrimitiveDecodeError::LimitExceeded
            | PrimitiveDecodeError::InvalidUtf8 => Self::InvalidValue,
        }
    }
}

/// Encodes the exact 40-byte legacy M14 GWIPC envelope.
#[must_use]
pub fn encode_envelope(envelope: &Envelope) -> [u8; GWIPC_ENVELOPE_SIZE] {
    let mut writer = ByteWriter::with_capacity(GWIPC_ENVELOPE_SIZE);
    writer.write_bytes(&GWIPC_MAGIC);
    writer.write_u16(GWIPC_ENVELOPE_SIZE as u16);
    writer.write_u16(envelope.version.major);
    writer.write_u16(envelope.version.minor);
    writer.write_u16(envelope.message_type.get());
    writer.write_u32(envelope.flags.bits());
    writer.write_u32(envelope.payload_size);
    writer.write_u16(envelope.fd_count);
    writer.write_u16(0);
    writer.write_u64(envelope.sequence.get());
    writer.write_u64(envelope.reply_to.get());
    writer
        .into_bytes()
        .try_into()
        .expect("GWIPC envelope writer emitted exactly 40 bytes")
}

/// Decodes and validates an entire GWIPC record (header plus payload).
///
/// File descriptors travel out of band, so their actual count is supplied by
/// the transport and must equal the count declared in the envelope.
pub fn decode_envelope(
    record: &[u8],
    actual_fd_count: usize,
    limits: DecodeLimits,
) -> Result<Envelope, EnvelopeDecodeError> {
    if record.len() < GWIPC_ENVELOPE_SIZE {
        return Err(EnvelopeDecodeError::Truncated);
    }

    let mut reader = ByteReader::new(&record[..GWIPC_ENVELOPE_SIZE]);
    let magic = reader.read_bytes(GWIPC_MAGIC.len())?;
    let header_size = reader.read_u16()?;
    let version = WireVersion::new(reader.read_u16()?, reader.read_u16()?);
    let message_type = MessageType::new(reader.read_u16()?);
    let raw_flags = reader.read_u32()?;
    let payload_size = reader.read_u32()?;
    let fd_count = reader.read_u16()?;
    let reserved = reader.read_u16()?;
    let sequence = Sequence::new(reader.read_u64()?);
    let reply_to = Sequence::new(reader.read_u64()?);

    let Some(flags) = MessageFlags::from_bits(raw_flags) else {
        return Err(EnvelopeDecodeError::InvalidValue);
    };
    if magic != GWIPC_MAGIC
        || usize::from(header_size) != GWIPC_ENVELOPE_SIZE
        || reserved != 0
        || sequence.get() == 0
    {
        return Err(EnvelopeDecodeError::InvalidValue);
    }
    if payload_size > limits.maximum_payload || actual_fd_count > usize::from(u16::MAX) {
        return Err(EnvelopeDecodeError::LimitExceeded);
    }
    if usize::from(fd_count) != actual_fd_count
        || record.len() - GWIPC_ENVELOPE_SIZE != payload_size as usize
    {
        return Err(EnvelopeDecodeError::SizeMismatch);
    }

    let is_reply = flags.contains(MessageFlags::REPLY);
    let is_error = flags.contains(MessageFlags::ERROR);
    if is_reply && reply_to.get() == 0 {
        return Err(EnvelopeDecodeError::InvalidValue);
    }
    if !is_reply && (reply_to.get() != 0 || is_error) {
        return Err(EnvelopeDecodeError::InvalidValue);
    }

    Ok(Envelope {
        version,
        message_type,
        flags,
        payload_size,
        fd_count,
        sequence,
        reply_to,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    fn record(envelope: &Envelope, payload: &[u8]) -> Vec<u8> {
        let mut record = encode_envelope(envelope).to_vec();
        record.extend_from_slice(payload);
        record
    }

    #[test]
    fn canonical_ping_matches_legacy_bytes() {
        let envelope = Envelope::request(MessageType::PING, Sequence::new(1), 0);
        assert_eq!(
            encode_envelope(&envelope),
            [
                b'G', b'W', b'I', b'P', 40, 0, 1, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            ]
        );
    }

    #[test]
    fn envelope_and_payload_round_trip() {
        let mut envelope = Envelope::request(MessageType::OUTPUT_UPSERT, Sequence::new(9), 3);
        envelope.fd_count = 1;
        let record = record(&envelope, &[1, 2, 3]);
        assert_eq!(
            decode_envelope(&record, 1, DecodeLimits::new(64)),
            Ok(envelope)
        );
    }

    #[test]
    fn reply_invariants_are_enforced() {
        let mut envelope = Envelope::request(MessageType::PONG, Sequence::new(2), 0);
        envelope.flags = MessageFlags::REPLY;
        assert_eq!(
            decode_envelope(&record(&envelope, &[]), 0, DecodeLimits::new(64)),
            Err(EnvelopeDecodeError::InvalidValue)
        );

        envelope.reply_to = Sequence::new(1);
        assert_eq!(
            decode_envelope(&record(&envelope, &[]), 0, DecodeLimits::new(64)),
            Ok(envelope)
        );

        envelope.flags = MessageFlags::ERROR;
        assert_eq!(
            decode_envelope(&record(&envelope, &[]), 0, DecodeLimits::new(64)),
            Err(EnvelopeDecodeError::InvalidValue)
        );
    }

    #[test]
    fn malformed_fixed_fields_are_rejected() {
        let envelope = Envelope::request(MessageType::PING, Sequence::new(1), 0);
        for offset in [0, 4, 22] {
            let mut malformed = record(&envelope, &[]);
            malformed[offset] ^= 0xff;
            assert_eq!(
                decode_envelope(&malformed, 0, DecodeLimits::new(64)),
                Err(EnvelopeDecodeError::InvalidValue),
                "offset {offset}"
            );
        }

        let mut zero_sequence = record(&envelope, &[]);
        zero_sequence[24..32].fill(0);
        assert_eq!(
            decode_envelope(&zero_sequence, 0, DecodeLimits::new(64)),
            Err(EnvelopeDecodeError::InvalidValue)
        );

        let mut unknown_flags = record(&envelope, &[]);
        unknown_flags[12..16].copy_from_slice(&(1_u32 << 31).to_le_bytes());
        assert_eq!(
            decode_envelope(&unknown_flags, 0, DecodeLimits::new(64)),
            Err(EnvelopeDecodeError::InvalidValue)
        );
    }

    #[test]
    fn truncation_payload_and_descriptor_mismatches_are_distinct() {
        let envelope = Envelope::request(MessageType::PING, Sequence::new(1), 0);
        assert_eq!(
            decode_envelope(&[0; 39], 0, DecodeLimits::new(64)),
            Err(EnvelopeDecodeError::Truncated)
        );

        assert_eq!(
            decode_envelope(&record(&envelope, &[0]), 0, DecodeLimits::new(64)),
            Err(EnvelopeDecodeError::SizeMismatch)
        );

        let mut with_fd = envelope;
        with_fd.fd_count = 1;
        assert_eq!(
            decode_envelope(&record(&with_fd, &[]), 0, DecodeLimits::new(64)),
            Err(EnvelopeDecodeError::SizeMismatch)
        );
    }

    #[test]
    fn payload_limit_is_checked_before_record_size() {
        let envelope = Envelope::request(MessageType::PING, Sequence::new(1), 65);
        assert_eq!(
            decode_envelope(&record(&envelope, &[]), 0, DecodeLimits::new(64)),
            Err(EnvelopeDecodeError::LimitExceeded)
        );
    }

    #[test]
    fn unknown_message_type_is_preserved_for_later_validation() {
        let envelope = Envelope::request(MessageType::new(0xf001), Sequence::new(1), 0);
        assert_eq!(
            decode_envelope(&record(&envelope, &[]), 0, DecodeLimits::new(64))
                .unwrap()
                .message_type,
            MessageType::new(0xf001)
        );
    }
}
