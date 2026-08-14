#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u8)]
pub enum ByteOrder {
    LittleEndian = b'l',
    BigEndian = b'B',
}

impl ByteOrder {
    #[must_use]
    pub const fn from_marker(marker: u8) -> Option<Self> {
        match marker {
            b'l' => Some(Self::LittleEndian),
            b'B' => Some(Self::BigEndian),
            _ => None,
        }
    }

    #[must_use]
    pub const fn marker(self) -> u8 {
        self as u8
    }

    pub(crate) const fn read_u16(self, bytes: [u8; 2]) -> u16 {
        match self {
            Self::LittleEndian => u16::from_le_bytes(bytes),
            Self::BigEndian => u16::from_be_bytes(bytes),
        }
    }

    pub(crate) const fn read_u32(self, bytes: [u8; 4]) -> u32 {
        match self {
            Self::LittleEndian => u32::from_le_bytes(bytes),
            Self::BigEndian => u32::from_be_bytes(bytes),
        }
    }

    pub(crate) const fn u16_bytes(self, value: u16) -> [u8; 2] {
        match self {
            Self::LittleEndian => value.to_le_bytes(),
            Self::BigEndian => value.to_be_bytes(),
        }
    }

    pub(crate) const fn u32_bytes(self, value: u32) -> [u8; 4] {
        match self {
            Self::LittleEndian => value.to_le_bytes(),
            Self::BigEndian => value.to_be_bytes(),
        }
    }
}
