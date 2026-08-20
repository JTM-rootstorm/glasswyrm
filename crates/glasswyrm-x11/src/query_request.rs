use crate::{ByteOrder, CoreErrorCode, CoreRequestDecodeError, FramedRequest};

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct SetInputFocusRequest {
    pub revert_to: u8,
    pub focus: u32,
    pub time: u32,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct TranslateCoordinatesRequest {
    pub source: u32,
    pub destination: u32,
    pub source_x: i16,
    pub source_y: i16,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct WarpPointerRequest {
    pub source: u32,
    pub destination: u32,
    pub source_x: i16,
    pub source_y: i16,
    pub source_width: u16,
    pub source_height: u16,
    pub destination_x: i16,
    pub destination_y: i16,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct GetKeyboardMappingRequest {
    pub first_keycode: u8,
    pub count: u8,
}

pub fn decode_set_input_focus(
    order: ByteOrder,
    request: &FramedRequest,
) -> Result<SetInputFocusRequest, CoreRequestDecodeError> {
    exact(request, 12, 8)?;
    if request.data > 2 {
        return Err(error(CoreErrorCode::BadValue, u32::from(request.data)));
    }
    Ok(SetInputFocusRequest {
        revert_to: request.data,
        focus: read_u32(order, request.body(), 0),
        time: read_u32(order, request.body(), 4),
    })
}

pub fn decode_query_pointer(
    order: ByteOrder,
    request: &FramedRequest,
) -> Result<u32, CoreRequestDecodeError> {
    exact(request, 8, 4)?;
    Ok(read_u32(order, request.body(), 0))
}

pub fn decode_translate_coordinates(
    order: ByteOrder,
    request: &FramedRequest,
) -> Result<TranslateCoordinatesRequest, CoreRequestDecodeError> {
    exact(request, 16, 12)?;
    let body = request.body();
    Ok(TranslateCoordinatesRequest {
        source: read_u32(order, body, 0),
        destination: read_u32(order, body, 4),
        source_x: read_u16(order, body, 8) as i16,
        source_y: read_u16(order, body, 10) as i16,
    })
}

pub fn decode_warp_pointer(
    order: ByteOrder,
    request: &FramedRequest,
) -> Result<WarpPointerRequest, CoreRequestDecodeError> {
    exact(request, 24, 20)?;
    let body = request.body();
    Ok(WarpPointerRequest {
        source: read_u32(order, body, 0),
        destination: read_u32(order, body, 4),
        source_x: read_u16(order, body, 8) as i16,
        source_y: read_u16(order, body, 10) as i16,
        source_width: read_u16(order, body, 12),
        source_height: read_u16(order, body, 14),
        destination_x: read_u16(order, body, 16) as i16,
        destination_y: read_u16(order, body, 18) as i16,
    })
}

pub fn decode_get_keyboard_mapping(
    request: &FramedRequest,
) -> Result<GetKeyboardMappingRequest, CoreRequestDecodeError> {
    exact(request, 8, 4)?;
    let first_keycode = request.body()[0];
    let count = request.body()[1];
    if first_keycode < 8 || count == 0 || u16::from(first_keycode) + u16::from(count) > 256 {
        return Err(error(CoreErrorCode::BadValue, u32::from(first_keycode)));
    }
    Ok(GetKeyboardMappingRequest {
        first_keycode,
        count,
    })
}

pub fn decode_get_pointer_mapping(request: &FramedRequest) -> Result<(), CoreRequestDecodeError> {
    exact(request, 4, 0)
}

pub fn decode_get_modifier_mapping(request: &FramedRequest) -> Result<(), CoreRequestDecodeError> {
    exact(request, 4, 0)
}

fn exact(
    request: &FramedRequest,
    core_size: usize,
    body_size: usize,
) -> Result<(), CoreRequestDecodeError> {
    if request.core_size() == core_size && request.body().len() == body_size {
        Ok(())
    } else {
        Err(CoreRequestDecodeError::bad_length())
    }
}

fn error(code: CoreErrorCode, bad_value: u32) -> CoreRequestDecodeError {
    CoreRequestDecodeError { code, bad_value }
}

fn read_u16(order: ByteOrder, bytes: &[u8], offset: usize) -> u16 {
    order.read_u16([bytes[offset], bytes[offset + 1]])
}

fn read_u32(order: ByteOrder, bytes: &[u8], offset: usize) -> u32 {
    order.read_u32([
        bytes[offset],
        bytes[offset + 1],
        bytes[offset + 2],
        bytes[offset + 3],
    ])
}

#[cfg(test)]
mod tests {
    use super::*;

    fn framed(order: ByteOrder, opcode: u8, data: u8, body: &[u8]) -> FramedRequest {
        let mut bytes = vec![opcode, data, 0, 0];
        bytes.extend_from_slice(body);
        let units = u16::try_from(bytes.len() / 4).unwrap();
        bytes[2..4].copy_from_slice(&order.u16_bytes(units));
        FramedRequest {
            opcode,
            data,
            length_units: u32::from(units),
            header_size: 4,
            bytes,
        }
    }

    #[test]
    fn focus_pointer_and_coordinate_requests_match_both_byte_orders() {
        for order in [ByteOrder::LittleEndian, ByteOrder::BigEndian] {
            let mut focus = Vec::new();
            focus.extend_from_slice(&order.u32_bytes(0x1122_3344));
            focus.extend_from_slice(&order.u32_bytes(0x5566_7788));
            assert_eq!(
                decode_set_input_focus(order, &framed(order, 42, 2, &focus)).unwrap(),
                SetInputFocusRequest {
                    revert_to: 2,
                    focus: 0x1122_3344,
                    time: 0x5566_7788
                }
            );

            assert_eq!(
                decode_query_pointer(order, &framed(order, 38, 0, &order.u32_bytes(0xaabb_ccdd)))
                    .unwrap(),
                0xaabb_ccdd
            );

            let mut translate = Vec::new();
            translate.extend_from_slice(&order.u32_bytes(1));
            translate.extend_from_slice(&order.u32_bytes(2));
            translate.extend_from_slice(&order.u16_bytes((-3_i16) as u16));
            translate.extend_from_slice(&order.u16_bytes(4));
            assert_eq!(
                decode_translate_coordinates(order, &framed(order, 40, 0, &translate)).unwrap(),
                TranslateCoordinatesRequest {
                    source: 1,
                    destination: 2,
                    source_x: -3,
                    source_y: 4
                }
            );
        }
    }

    #[test]
    fn warp_pointer_preserves_signed_and_unsigned_fields() {
        for order in [ByteOrder::LittleEndian, ByteOrder::BigEndian] {
            let mut body = Vec::new();
            body.extend_from_slice(&order.u32_bytes(1));
            body.extend_from_slice(&order.u32_bytes(2));
            for value in [(-3_i16) as u16, 4, 640, 480, (-5_i16) as u16, 6] {
                body.extend_from_slice(&order.u16_bytes(value));
            }
            assert_eq!(
                decode_warp_pointer(order, &framed(order, 41, 0, &body)).unwrap(),
                WarpPointerRequest {
                    source: 1,
                    destination: 2,
                    source_x: -3,
                    source_y: 4,
                    source_width: 640,
                    source_height: 480,
                    destination_x: -5,
                    destination_y: 6
                }
            );
        }
    }

    #[test]
    fn keyboard_mapping_bounds_and_padding_match_legacy_behavior() {
        let order = ByteOrder::LittleEndian;
        let valid = framed(order, 101, 0, &[8, 248, 0xaa, 0xbb]);
        assert_eq!(
            decode_get_keyboard_mapping(&valid).unwrap(),
            GetKeyboardMappingRequest {
                first_keycode: 8,
                count: 248
            }
        );
        for (first, count) in [(7, 1), (8, 0), (255, 2)] {
            let request = framed(order, 101, 0, &[first, count, 0, 0]);
            assert_eq!(
                decode_get_keyboard_mapping(&request),
                Err(error(CoreErrorCode::BadValue, u32::from(first)))
            );
        }
    }

    #[test]
    fn fixed_query_lengths_and_focus_mode_fail_closed() {
        let order = ByteOrder::LittleEndian;
        let bad_focus = framed(order, 42, 3, &[0; 8]);
        assert_eq!(
            decode_set_input_focus(order, &bad_focus),
            Err(error(CoreErrorCode::BadValue, 3))
        );
        let empty = framed(order, 117, 0, &[]);
        assert_eq!(decode_get_pointer_mapping(&empty), Ok(()));
        assert_eq!(decode_get_modifier_mapping(&empty), Ok(()));
        let wrong = framed(order, 117, 0, &[0; 4]);
        assert_eq!(
            decode_get_pointer_mapping(&wrong),
            Err(CoreRequestDecodeError::bad_length())
        );
    }
}
