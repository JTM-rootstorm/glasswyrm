use crate::ByteOrder;

pub(crate) const fn padding_for(size: usize) -> usize {
    (4 - (size & 3)) & 3
}

pub(crate) fn align_four(size: usize) -> Option<usize> {
    size.checked_add(3).map(|value| value & !3)
}

pub(crate) struct Writer {
    order: ByteOrder,
    bytes: Vec<u8>,
}

impl Writer {
    pub(crate) fn new(order: ByteOrder) -> Self {
        Self {
            order,
            bytes: Vec::new(),
        }
    }

    pub(crate) fn len(&self) -> usize {
        self.bytes.len()
    }

    pub(crate) fn write_u8(&mut self, value: u8) {
        self.bytes.push(value);
    }

    pub(crate) fn write_u16(&mut self, value: u16) {
        self.bytes.extend_from_slice(&self.order.u16_bytes(value));
    }

    pub(crate) fn write_u32(&mut self, value: u32) {
        self.bytes.extend_from_slice(&self.order.u32_bytes(value));
    }

    pub(crate) fn write_bytes(&mut self, bytes: &[u8]) {
        self.bytes.extend_from_slice(bytes);
    }

    pub(crate) fn write_padding(&mut self, count: usize) {
        self.bytes.resize(self.bytes.len() + count, 0);
    }

    pub(crate) fn into_bytes(self) -> Vec<u8> {
        self.bytes
    }
}
