use core::fmt;

/// Failure while decoding a primitive wire value.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum PrimitiveDecodeError {
    Truncated,
    TrailingData,
    LimitExceeded,
    InvalidUtf8,
}

impl fmt::Display for PrimitiveDecodeError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(match self {
            Self::Truncated => "wire value is truncated",
            Self::TrailingData => "wire value has trailing data",
            Self::LimitExceeded => "wire value exceeds the configured limit",
            Self::InvalidUtf8 => "wire string is not valid UTF-8",
        })
    }
}

impl std::error::Error for PrimitiveDecodeError {}

/// Failure while encoding a length-prefixed wire value.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum PrimitiveEncodeError {
    LengthOverflow,
}

impl fmt::Display for PrimitiveEncodeError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str("wire value length cannot be represented as u32")
    }
}

impl std::error::Error for PrimitiveEncodeError {}

/// A bounds-checked reader for little-endian GWIPC payloads.
#[derive(Clone, Copy, Debug)]
pub struct ByteReader<'a> {
    bytes: &'a [u8],
    offset: usize,
}

impl<'a> ByteReader<'a> {
    #[must_use]
    pub const fn new(bytes: &'a [u8]) -> Self {
        Self { bytes, offset: 0 }
    }

    #[must_use]
    pub const fn remaining(&self) -> usize {
        self.bytes.len() - self.offset
    }

    #[must_use]
    pub const fn is_empty(&self) -> bool {
        self.remaining() == 0
    }

    pub fn finish(self) -> Result<(), PrimitiveDecodeError> {
        if self.is_empty() {
            Ok(())
        } else {
            Err(PrimitiveDecodeError::TrailingData)
        }
    }

    pub fn read_u8(&mut self) -> Result<u8, PrimitiveDecodeError> {
        Ok(self.read_array::<1>()?[0])
    }

    pub fn read_u16(&mut self) -> Result<u16, PrimitiveDecodeError> {
        Ok(u16::from_le_bytes(self.read_array()?))
    }

    pub fn read_u32(&mut self) -> Result<u32, PrimitiveDecodeError> {
        Ok(u32::from_le_bytes(self.read_array()?))
    }

    pub fn read_i32(&mut self) -> Result<i32, PrimitiveDecodeError> {
        Ok(i32::from_le_bytes(self.read_array()?))
    }

    pub fn read_u64(&mut self) -> Result<u64, PrimitiveDecodeError> {
        Ok(u64::from_le_bytes(self.read_array()?))
    }

    pub fn read_bytes(&mut self, length: usize) -> Result<&'a [u8], PrimitiveDecodeError> {
        let end = self
            .offset
            .checked_add(length)
            .ok_or(PrimitiveDecodeError::Truncated)?;
        let value = self
            .bytes
            .get(self.offset..end)
            .ok_or(PrimitiveDecodeError::Truncated)?;
        self.offset = end;
        Ok(value)
    }

    /// Reads a `u32` byte length followed by the bytes, checking `maximum`
    /// before slicing or allocating.
    pub fn read_sized_bytes(&mut self, maximum: usize) -> Result<&'a [u8], PrimitiveDecodeError> {
        let length =
            usize::try_from(self.read_u32()?).map_err(|_| PrimitiveDecodeError::LimitExceeded)?;
        if length > maximum {
            return Err(PrimitiveDecodeError::LimitExceeded);
        }
        self.read_bytes(length)
    }

    pub fn read_sized_str(&mut self, maximum: usize) -> Result<&'a str, PrimitiveDecodeError> {
        std::str::from_utf8(self.read_sized_bytes(maximum)?)
            .map_err(|_| PrimitiveDecodeError::InvalidUtf8)
    }

    fn read_array<const N: usize>(&mut self) -> Result<[u8; N], PrimitiveDecodeError> {
        self.read_bytes(N)?
            .try_into()
            .map_err(|_| PrimitiveDecodeError::Truncated)
    }
}

/// A writer for explicit little-endian GWIPC payloads.
#[derive(Clone, Debug, Default, Eq, PartialEq)]
pub struct ByteWriter {
    bytes: Vec<u8>,
}

impl ByteWriter {
    #[must_use]
    pub const fn new() -> Self {
        Self { bytes: Vec::new() }
    }

    #[must_use]
    pub fn with_capacity(capacity: usize) -> Self {
        Self {
            bytes: Vec::with_capacity(capacity),
        }
    }

    pub fn write_u8(&mut self, value: u8) {
        self.bytes.push(value);
    }

    pub fn write_u16(&mut self, value: u16) {
        self.bytes.extend_from_slice(&value.to_le_bytes());
    }

    pub fn write_u32(&mut self, value: u32) {
        self.bytes.extend_from_slice(&value.to_le_bytes());
    }

    pub fn write_i32(&mut self, value: i32) {
        self.bytes.extend_from_slice(&value.to_le_bytes());
    }

    pub fn write_u64(&mut self, value: u64) {
        self.bytes.extend_from_slice(&value.to_le_bytes());
    }

    pub fn write_bytes(&mut self, value: &[u8]) {
        self.bytes.extend_from_slice(value);
    }

    pub fn write_sized_bytes(&mut self, value: &[u8]) -> Result<(), PrimitiveEncodeError> {
        let length =
            u32::try_from(value.len()).map_err(|_| PrimitiveEncodeError::LengthOverflow)?;
        self.write_u32(length);
        self.write_bytes(value);
        Ok(())
    }

    #[must_use]
    pub fn into_bytes(self) -> Vec<u8> {
        self.bytes
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn primitives_round_trip_in_little_endian_order() {
        let mut writer = ByteWriter::new();
        writer.write_u8(0x12);
        writer.write_u16(0x3456);
        writer.write_u32(0x789a_bcde);
        writer.write_i32(-42);
        writer.write_u64(0x0123_4567_89ab_cdef);
        writer.write_sized_bytes(b"gw").unwrap();

        let bytes = writer.into_bytes();
        assert_eq!(&bytes[..3], &[0x12, 0x56, 0x34]);
        let mut reader = ByteReader::new(&bytes);
        assert_eq!(reader.read_u8(), Ok(0x12));
        assert_eq!(reader.read_u16(), Ok(0x3456));
        assert_eq!(reader.read_u32(), Ok(0x789a_bcde));
        assert_eq!(reader.read_i32(), Ok(-42));
        assert_eq!(reader.read_u64(), Ok(0x0123_4567_89ab_cdef));
        assert_eq!(reader.read_sized_bytes(2), Ok(b"gw".as_slice()));
        assert_eq!(reader.finish(), Ok(()));
    }

    #[test]
    fn truncated_read_does_not_advance_reader() {
        let mut reader = ByteReader::new(&[1, 2, 3]);
        assert_eq!(reader.read_u32(), Err(PrimitiveDecodeError::Truncated));
        assert_eq!(reader.remaining(), 3);
        assert_eq!(reader.read_u16(), Ok(0x0201));
    }

    #[test]
    fn sized_value_checks_limit_before_payload() {
        let mut reader = ByteReader::new(&[5, 0, 0, 0]);
        assert_eq!(
            reader.read_sized_bytes(4),
            Err(PrimitiveDecodeError::LimitExceeded)
        );
    }

    #[test]
    fn sized_string_rejects_invalid_utf8() {
        let mut reader = ByteReader::new(&[1, 0, 0, 0, 0xff]);
        assert_eq!(
            reader.read_sized_str(8),
            Err(PrimitiveDecodeError::InvalidUtf8)
        );
    }
}
