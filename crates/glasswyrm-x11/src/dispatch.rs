use crate::{
    ByteOrder, CoreError, CoreErrorCode, CoreOpcode, FramedRequest, ReplyBuilder, encode_core_error,
};

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum InitialCoreDispatch {
    NoReply,
    Packet(Vec<u8>),
}

/// Dispatches the deliberately small core profile used by the first Rust
/// request loop. State-bearing requests remain outside this boundary.
#[must_use]
pub fn dispatch_initial_core_request(
    order: ByteOrder,
    sequence: u64,
    focused_window: u32,
    request: &FramedRequest,
) -> InitialCoreDispatch {
    match request.opcode {
        opcode if opcode == CoreOpcode::NoOperation as u8 => InitialCoreDispatch::NoReply,
        opcode if opcode == CoreOpcode::GetInputFocus as u8 => {
            if request.core_size() != 4 {
                return protocol_error(order, sequence, request, CoreErrorCode::BadLength);
            }
            let mut reply = ReplyBuilder::new(order, sequence, 0);
            reply
                .write_u32(focused_window)
                .expect("GetInputFocus fixed fields fit the core reply");
            InitialCoreDispatch::Packet(
                reply
                    .finish()
                    .expect("GetInputFocus has no variable-length payload"),
            )
        }
        _ => protocol_error(order, sequence, request, CoreErrorCode::BadRequest),
    }
}

fn protocol_error(
    order: ByteOrder,
    sequence: u64,
    request: &FramedRequest,
    code: CoreErrorCode,
) -> InitialCoreDispatch {
    InitialCoreDispatch::Packet(encode_core_error(
        order,
        CoreError {
            code,
            sequence,
            bad_value: 0,
            major_opcode: request.opcode,
            minor_opcode: if request.opcode >= 128 {
                u16::from(request.data)
            } else {
                0
            },
        },
    ))
}

#[cfg(test)]
mod tests {
    use super::*;

    fn request_with_data(order: ByteOrder, opcode: u8, data: u8, units: u16) -> FramedRequest {
        let mut bytes = vec![opcode, data];
        let encoded_units = match order {
            ByteOrder::LittleEndian => units.to_le_bytes(),
            ByteOrder::BigEndian => units.to_be_bytes(),
        };
        bytes.extend_from_slice(&encoded_units);
        bytes.resize(usize::from(units) * 4, 0);
        FramedRequest {
            opcode,
            data,
            length_units: u32::from(units),
            header_size: 4,
            bytes,
        }
    }

    fn request(order: ByteOrder, opcode: u8, units: u16) -> FramedRequest {
        request_with_data(order, opcode, 0, units)
    }

    fn u16_at(order: ByteOrder, bytes: &[u8], offset: usize) -> u16 {
        match order {
            ByteOrder::LittleEndian => u16::from_le_bytes([bytes[offset], bytes[offset + 1]]),
            ByteOrder::BigEndian => u16::from_be_bytes([bytes[offset], bytes[offset + 1]]),
        }
    }

    fn u32_at(order: ByteOrder, bytes: &[u8], offset: usize) -> u32 {
        match order {
            ByteOrder::LittleEndian => u32::from_le_bytes([
                bytes[offset],
                bytes[offset + 1],
                bytes[offset + 2],
                bytes[offset + 3],
            ]),
            ByteOrder::BigEndian => u32::from_be_bytes([
                bytes[offset],
                bytes[offset + 1],
                bytes[offset + 2],
                bytes[offset + 3],
            ]),
        }
    }

    #[test]
    fn initial_profile_matches_core_reply_and_error_semantics() {
        for order in [ByteOrder::LittleEndian, ByteOrder::BigEndian] {
            assert_eq!(
                dispatch_initial_core_request(
                    order,
                    1,
                    1,
                    &request(order, CoreOpcode::NoOperation as u8, 8),
                ),
                InitialCoreDispatch::NoReply
            );

            let InitialCoreDispatch::Packet(focus) = dispatch_initial_core_request(
                order,
                0x1_0000,
                1,
                &request(order, CoreOpcode::GetInputFocus as u8, 1),
            ) else {
                panic!("GetInputFocus must reply");
            };
            assert_eq!(focus.len(), 32);
            assert_eq!(focus[0], 1);
            assert_eq!(u16_at(order, &focus, 2), 0);
            assert_eq!(u32_at(order, &focus, 8), 1);

            let InitialCoreDispatch::Packet(bad_length) = dispatch_initial_core_request(
                order,
                2,
                1,
                &request(order, CoreOpcode::GetInputFocus as u8, 2),
            ) else {
                panic!("invalid GetInputFocus must error");
            };
            assert_eq!(bad_length[0], 0);
            assert_eq!(bad_length[1], CoreErrorCode::BadLength as u8);
            assert_eq!(bad_length[10], CoreOpcode::GetInputFocus as u8);

            let InitialCoreDispatch::Packet(unsupported) =
                dispatch_initial_core_request(order, 3, 1, &request(order, 250, 1))
            else {
                panic!("unsupported opcode must error");
            };
            assert_eq!(unsupported[1], CoreErrorCode::BadRequest as u8);
            assert_eq!(u16_at(order, &unsupported, 2), 3);
            assert_eq!(unsupported[10], 250);

            let InitialCoreDispatch::Packet(unsupported_extension) =
                dispatch_initial_core_request(order, 4, 1, &request_with_data(order, 200, 7, 1))
            else {
                panic!("unsupported extension request must error");
            };
            assert_eq!(unsupported_extension[1], CoreErrorCode::BadRequest as u8);
            assert_eq!(u16_at(order, &unsupported_extension, 8), 7);
            assert_eq!(unsupported_extension[10], 200);
        }
    }
}
