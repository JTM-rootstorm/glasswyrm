use crate::wire::{Writer, padding_for};
use crate::{ByteOrder, CoreErrorCode, wire_sequence};

pub const CORE_REPLY_SIZE: usize = 32;
pub const CORE_ERROR_SIZE: usize = 32;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ReplyBuildError {
    FixedFieldsTooLarge,
    PayloadTooLarge,
}

pub struct ReplyBuilder {
    order: ByteOrder,
    sequence: u64,
    response_data: u8,
    fixed: Writer,
    payload: Writer,
}

impl ReplyBuilder {
    #[must_use]
    pub fn new(order: ByteOrder, sequence: u64, response_data: u8) -> Self {
        Self {
            order,
            sequence,
            response_data,
            fixed: Writer::new(order),
            payload: Writer::new(order),
        }
    }

    pub fn write_u8(&mut self, value: u8) -> Result<(), ReplyBuildError> {
        self.ensure_fixed_capacity(1)?;
        self.fixed.write_u8(value);
        Ok(())
    }

    pub fn write_u16(&mut self, value: u16) -> Result<(), ReplyBuildError> {
        self.ensure_fixed_capacity(2)?;
        self.fixed.write_u16(value);
        Ok(())
    }

    pub fn write_u32(&mut self, value: u32) -> Result<(), ReplyBuildError> {
        self.ensure_fixed_capacity(4)?;
        self.fixed.write_u32(value);
        Ok(())
    }

    pub fn write_padding(&mut self, count: usize) -> Result<(), ReplyBuildError> {
        self.ensure_fixed_capacity(count)?;
        self.fixed.write_padding(count);
        Ok(())
    }

    pub fn write_payload(&mut self, bytes: &[u8]) {
        self.payload.write_bytes(bytes);
    }

    pub fn write_payload_u16(&mut self, value: u16) {
        self.payload.write_u16(value);
    }

    pub fn write_payload_u32(&mut self, value: u32) {
        self.payload.write_u32(value);
    }

    pub fn finish(mut self) -> Result<Vec<u8>, ReplyBuildError> {
        self.fixed.write_padding(24 - self.fixed.len());
        let padding = padding_for(self.payload.len());
        let padded_payload_size = self
            .payload
            .len()
            .checked_add(padding)
            .ok_or(ReplyBuildError::PayloadTooLarge)?;
        let payload_units =
            u32::try_from(padded_payload_size / 4).map_err(|_| ReplyBuildError::PayloadTooLarge)?;
        self.payload.write_padding(padding);

        let mut reply = Writer::new(self.order);
        reply.write_u8(1);
        reply.write_u8(self.response_data);
        reply.write_u16(wire_sequence(self.sequence));
        reply.write_u32(payload_units);
        reply.write_bytes(&self.fixed.into_bytes());
        reply.write_bytes(&self.payload.into_bytes());
        Ok(reply.into_bytes())
    }

    fn ensure_fixed_capacity(&self, count: usize) -> Result<(), ReplyBuildError> {
        if count > 24 || self.fixed.len() > 24 - count {
            Err(ReplyBuildError::FixedFieldsTooLarge)
        } else {
            Ok(())
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct CoreError {
    pub code: CoreErrorCode,
    pub sequence: u64,
    pub bad_value: u32,
    pub major_opcode: u8,
    pub minor_opcode: u16,
}

#[must_use]
pub fn encode_core_error(order: ByteOrder, error: CoreError) -> Vec<u8> {
    let mut packet = Writer::new(order);
    packet.write_u8(0);
    packet.write_u8(error.code as u8);
    packet.write_u16(wire_sequence(error.sequence));
    packet.write_u32(error.bad_value);
    packet.write_u16(error.minor_opcode);
    packet.write_u8(error.major_opcode);
    packet.write_padding(21);
    packet.into_bytes()
}
