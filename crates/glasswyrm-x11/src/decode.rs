use crate::wire::align_four;
use crate::{ByteOrder, CoreErrorCode, FramedRequest};

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct CoreRequestDecodeError {
    pub code: CoreErrorCode,
    pub bad_value: u32,
}

impl CoreRequestDecodeError {
    #[must_use]
    pub const fn bad_length() -> Self {
        Self {
            code: CoreErrorCode::BadLength,
            bad_value: 0,
        }
    }

    #[must_use]
    pub const fn bad_value(value: u32) -> Self {
        Self {
            code: CoreErrorCode::BadValue,
            bad_value: value,
        }
    }
}

const WINDOW_ATTRIBUTE_MASK: u32 = 0x0000_7fff;
const CORE_EVENT_MASK: u32 = 0x01ff_ffff;
const DO_NOT_PROPAGATE_MASK: u32 = 0x0000_204f;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct WindowCreateRequest {
    pub xid: u32,
    pub parent: u32,
    pub x: i16,
    pub y: i16,
    pub width: u16,
    pub height: u16,
    pub border_width: u16,
    pub window_class: u16,
    pub depth: u8,
    pub visual: u32,
    pub attribute_mask: u32,
    pub override_redirect: bool,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum PropertyMode {
    Replace,
    Prepend,
    Append,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct DecodedPropertyChangeRequest<'a> {
    pub window: u32,
    pub property: u32,
    pub property_type: u32,
    pub mode: PropertyMode,
    pub format: u8,
    pub item_count: u32,
    pub data: &'a [u8],
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct InternAtomRequest<'a> {
    pub only_if_exists: bool,
    pub name: &'a [u8],
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct QueryExtensionRequest<'a> {
    pub name: &'a [u8],
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct DeletePropertyRequest {
    pub window: u32,
    pub property: u32,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct GetPropertyRequest {
    pub window: u32,
    pub property: u32,
    pub requested_type: u32,
    pub delete_after_read: bool,
    pub long_offset: u32,
    pub long_length: u32,
}

pub fn decode_create_window(
    order: ByteOrder,
    request: &FramedRequest,
) -> Result<WindowCreateRequest, CoreRequestDecodeError> {
    if request.core_size() < 32 || request.body().len() < 28 {
        return Err(CoreRequestDecodeError::bad_length());
    }
    let body = request.body();
    let xid = read_u32(order, body, 0);
    let parent = read_u32(order, body, 4);
    let window_class = read_u16(order, body, 18);
    let attribute_mask = read_u32(order, body, 24);
    if window_class > 2 {
        return Err(CoreRequestDecodeError::bad_value(u32::from(window_class)));
    }
    if attribute_mask & !WINDOW_ATTRIBUTE_MASK != 0 {
        return Err(CoreRequestDecodeError::bad_value(attribute_mask));
    }
    let value_count = attribute_mask.count_ones() as usize;
    if request.core_size() != 32 + value_count * 4 || body.len() != 28 + value_count * 4 {
        return Err(CoreRequestDecodeError::bad_length());
    }
    let override_redirect = decode_create_attributes(order, body, attribute_mask)?;
    Ok(WindowCreateRequest {
        xid,
        parent,
        x: read_u16(order, body, 8) as i16,
        y: read_u16(order, body, 10) as i16,
        width: read_u16(order, body, 12),
        height: read_u16(order, body, 14),
        border_width: read_u16(order, body, 16),
        window_class,
        depth: request.data,
        visual: read_u32(order, body, 20),
        attribute_mask,
        override_redirect,
    })
}

fn decode_create_attributes(
    order: ByteOrder,
    body: &[u8],
    attribute_mask: u32,
) -> Result<bool, CoreRequestDecodeError> {
    let mut offset = 28;
    let mut override_redirect = false;
    for bit in 0..15 {
        if attribute_mask & (1_u32 << bit) == 0 {
            continue;
        }
        let value = read_u32(order, body, offset);
        offset += 4;
        match bit {
            0 if value > 1 => {
                return Err(CoreRequestDecodeError {
                    code: CoreErrorCode::BadPixmap,
                    bad_value: value,
                });
            }
            2 if value != 0 => {
                return Err(CoreRequestDecodeError {
                    code: CoreErrorCode::BadPixmap,
                    bad_value: value,
                });
            }
            4 | 5 if value > 10 => return Err(CoreRequestDecodeError::bad_value(value)),
            6 if value > 2 => return Err(CoreRequestDecodeError::bad_value(value)),
            9 | 10 if value > 1 => return Err(CoreRequestDecodeError::bad_value(value)),
            9 => override_redirect = value != 0,
            11 if value & !CORE_EVENT_MASK != 0 => {
                return Err(CoreRequestDecodeError::bad_value(value));
            }
            12 if value & !DO_NOT_PROPAGATE_MASK != 0 => {
                return Err(CoreRequestDecodeError::bad_value(value));
            }
            13 if value != 0 && value != 2 => {
                return Err(CoreRequestDecodeError {
                    code: CoreErrorCode::BadColormap,
                    bad_value: value,
                });
            }
            14 if value != 0 => {
                return Err(CoreRequestDecodeError {
                    code: CoreErrorCode::BadCursor,
                    bad_value: value,
                });
            }
            _ => {}
        }
    }
    Ok(override_redirect)
}

pub fn decode_change_property<'a>(
    order: ByteOrder,
    request: &'a FramedRequest,
) -> Result<DecodedPropertyChangeRequest<'a>, CoreRequestDecodeError> {
    if request.core_size() < 24 || request.body().len() < 20 {
        return Err(CoreRequestDecodeError::bad_length());
    }
    let mode = match request.data {
        0 => PropertyMode::Replace,
        1 => PropertyMode::Prepend,
        2 => PropertyMode::Append,
        value => return Err(CoreRequestDecodeError::bad_value(u32::from(value))),
    };
    let body = request.body();
    let format = body[12];
    let item_count = read_u32(order, body, 16);
    if !matches!(format, 8 | 16 | 32) {
        return Err(CoreRequestDecodeError::bad_value(u32::from(format)));
    }
    let data_size = usize::try_from(item_count)
        .ok()
        .and_then(|count| count.checked_mul(usize::from(format / 8)))
        .ok_or_else(CoreRequestDecodeError::bad_length)?;
    let padded_size = align_four(data_size).ok_or_else(CoreRequestDecodeError::bad_length)?;
    if request.core_size() != 24 + padded_size || body.len() != 20 + padded_size {
        return Err(CoreRequestDecodeError::bad_length());
    }
    Ok(DecodedPropertyChangeRequest {
        window: read_u32(order, body, 0),
        property: read_u32(order, body, 4),
        property_type: read_u32(order, body, 8),
        mode,
        format,
        item_count,
        data: &body[20..20 + data_size],
    })
}

pub fn decode_empty_request(request: &FramedRequest) -> Result<(), CoreRequestDecodeError> {
    if request.core_size() == 4 && request.body().is_empty() {
        Ok(())
    } else {
        Err(CoreRequestDecodeError::bad_length())
    }
}

pub fn decode_single_u32_request(
    order: ByteOrder,
    request: &FramedRequest,
) -> Result<u32, CoreRequestDecodeError> {
    if request.core_size() != 8 || request.body().len() != 4 {
        return Err(CoreRequestDecodeError::bad_length());
    }
    Ok(read_u32(order, request.body(), 0))
}

pub fn decode_intern_atom<'a>(
    order: ByteOrder,
    request: &'a FramedRequest,
) -> Result<InternAtomRequest<'a>, CoreRequestDecodeError> {
    if request.data > 1 {
        return Err(CoreRequestDecodeError::bad_value(u32::from(request.data)));
    }
    let body = request.body();
    if body.len() < 4 {
        return Err(CoreRequestDecodeError::bad_length());
    }
    let name_length = usize::from(read_u16(order, body, 0));
    let padded_name_length =
        align_four(name_length).ok_or_else(CoreRequestDecodeError::bad_length)?;
    if request.core_size() != 8 + padded_name_length || body.len() != 4 + padded_name_length {
        return Err(CoreRequestDecodeError::bad_length());
    }
    Ok(InternAtomRequest {
        only_if_exists: request.data != 0,
        name: &body[4..4 + name_length],
    })
}

pub fn decode_query_extension<'a>(
    order: ByteOrder,
    request: &'a FramedRequest,
) -> Result<QueryExtensionRequest<'a>, CoreRequestDecodeError> {
    let body = request.body();
    if body.len() < 4 {
        return Err(CoreRequestDecodeError::bad_length());
    }
    let name_length = usize::from(read_u16(order, body, 0));
    let padded_name_length =
        align_four(name_length).ok_or_else(CoreRequestDecodeError::bad_length)?;
    if request.core_size() != 8 + padded_name_length || body.len() != 4 + padded_name_length {
        return Err(CoreRequestDecodeError::bad_length());
    }
    Ok(QueryExtensionRequest {
        name: &body[4..4 + name_length],
    })
}

pub fn decode_delete_property(
    order: ByteOrder,
    request: &FramedRequest,
) -> Result<DeletePropertyRequest, CoreRequestDecodeError> {
    if request.core_size() != 12 || request.body().len() != 8 {
        return Err(CoreRequestDecodeError::bad_length());
    }
    Ok(DeletePropertyRequest {
        window: read_u32(order, request.body(), 0),
        property: read_u32(order, request.body(), 4),
    })
}

pub fn decode_get_property(
    order: ByteOrder,
    request: &FramedRequest,
) -> Result<GetPropertyRequest, CoreRequestDecodeError> {
    if request.core_size() != 24 || request.body().len() != 20 {
        return Err(CoreRequestDecodeError::bad_length());
    }
    if request.data > 1 {
        return Err(CoreRequestDecodeError::bad_value(u32::from(request.data)));
    }
    let body = request.body();
    Ok(GetPropertyRequest {
        window: read_u32(order, body, 0),
        property: read_u32(order, body, 4),
        requested_type: read_u32(order, body, 8),
        delete_after_read: request.data != 0,
        long_offset: read_u32(order, body, 12),
        long_length: read_u32(order, body, 16),
    })
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
    use crate::CoreOpcode;

    fn framed(order: ByteOrder, opcode: CoreOpcode, data: u8, body: &[u8]) -> FramedRequest {
        let size = 4 + body.len();
        assert_eq!(size & 3, 0);
        let units = u16::try_from(size / 4).unwrap();
        let mut bytes = vec![opcode as u8, data];
        bytes.extend_from_slice(&order.u16_bytes(units));
        bytes.extend_from_slice(body);
        FramedRequest {
            opcode: opcode as u8,
            data,
            length_units: u32::from(units),
            header_size: 4,
            bytes,
        }
    }

    fn name_request(order: ByteOrder, opcode: CoreOpcode, data: u8, name: &[u8]) -> FramedRequest {
        let padded = (name.len() + 3) & !3;
        let mut body = Vec::with_capacity(4 + padded);
        body.extend_from_slice(&order.u16_bytes(name.len() as u16));
        body.extend_from_slice(&[0, 0]);
        body.extend_from_slice(name);
        body.resize(4 + padded, 0xa5);
        framed(order, opcode, data, &body)
    }

    #[test]
    fn name_decoders_are_binary_safe_and_preserve_validation_order() {
        for order in [ByteOrder::LittleEndian, ByteOrder::BigEndian] {
            let request = name_request(order, CoreOpcode::InternAtom, 1, &[b'G', 0xff]);
            assert_eq!(
                decode_intern_atom(order, &request).unwrap(),
                InternAtomRequest {
                    only_if_exists: true,
                    name: &[b'G', 0xff],
                }
            );
            let extension = name_request(order, CoreOpcode::QueryExtension, 0, &[b'R', 0xff]);
            assert_eq!(
                decode_query_extension(order, &extension).unwrap().name,
                &[b'R', 0xff]
            );

            let invalid_mode = name_request(order, CoreOpcode::InternAtom, 2, b"");
            assert_eq!(
                decode_intern_atom(order, &invalid_mode),
                Err(CoreRequestDecodeError::bad_value(2))
            );
            let truncated = framed(order, CoreOpcode::InternAtom, 0, &[]);
            assert_eq!(
                decode_intern_atom(order, &truncated),
                Err(CoreRequestDecodeError::bad_length())
            );
        }
    }

    #[test]
    fn fixed_decoders_reject_wrong_lengths_before_exposing_fields() {
        for order in [ByteOrder::LittleEndian, ByteOrder::BigEndian] {
            let value = 0x1122_3344;
            let exact = framed(order, CoreOpcode::GetAtomName, 0, &order.u32_bytes(value));
            assert_eq!(decode_single_u32_request(order, &exact), Ok(value));
            assert_eq!(
                decode_empty_request(&framed(order, CoreOpcode::ListExtensions, 0, &[])),
                Ok(())
            );
            assert_eq!(
                decode_empty_request(&exact),
                Err(CoreRequestDecodeError::bad_length())
            );

            let mut pair = Vec::new();
            pair.extend_from_slice(&order.u32_bytes(7));
            pair.extend_from_slice(&order.u32_bytes(8));
            let delete = framed(order, CoreOpcode::DeleteProperty, 0, &pair);
            assert_eq!(
                decode_delete_property(order, &delete).unwrap(),
                DeletePropertyRequest {
                    window: 7,
                    property: 8,
                }
            );
        }
    }

    #[test]
    fn create_window_decoder_preserves_legacy_value_mask_order() {
        for order in [ByteOrder::LittleEndian, ByteOrder::BigEndian] {
            let mut body = vec![0; 28];
            body[0..4].copy_from_slice(&order.u32_bytes(0x0040_0001));
            body[4..8].copy_from_slice(&order.u32_bytes(1));
            body[8..10].copy_from_slice(&order.u16_bytes((-2_i16) as u16));
            body[10..12].copy_from_slice(&order.u16_bytes(3));
            body[12..14].copy_from_slice(&order.u16_bytes(640));
            body[14..16].copy_from_slice(&order.u16_bytes(480));
            body[16..18].copy_from_slice(&order.u16_bytes(1));
            body[18..20].copy_from_slice(&order.u16_bytes(1));
            body[20..24].copy_from_slice(&order.u32_bytes(3));
            body[24..28].copy_from_slice(&order.u32_bytes(1 << 9));
            body.extend_from_slice(&order.u32_bytes(1));
            let request = framed(order, CoreOpcode::CreateWindow, 24, &body);
            let decoded = decode_create_window(order, &request).unwrap();
            assert_eq!(decoded.x, -2);
            assert_eq!(decoded.width, 640);
            assert!(decoded.override_redirect);

            let mut invalid_class = body.clone();
            invalid_class[18..20].copy_from_slice(&order.u16_bytes(3));
            let request = framed(order, CoreOpcode::CreateWindow, 24, &invalid_class);
            assert_eq!(
                decode_create_window(order, &request),
                Err(CoreRequestDecodeError::bad_value(3))
            );

            let mut invalid_mask = body[..28].to_vec();
            invalid_mask[24..28].copy_from_slice(&order.u32_bytes(1 << 20));
            let request = framed(order, CoreOpcode::CreateWindow, 24, &invalid_mask);
            assert_eq!(
                decode_create_window(order, &request),
                Err(CoreRequestDecodeError::bad_value(1 << 20))
            );
        }
    }

    #[test]
    fn change_property_decoder_borrows_payload_and_checks_arithmetic_before_state() {
        for order in [ByteOrder::LittleEndian, ByteOrder::BigEndian] {
            let mut body = vec![0; 20];
            body[0..4].copy_from_slice(&order.u32_bytes(7));
            body[4..8].copy_from_slice(&order.u32_bytes(8));
            body[8..12].copy_from_slice(&order.u32_bytes(9));
            body[12] = 16;
            body[16..20].copy_from_slice(&order.u32_bytes(2));
            body.extend_from_slice(&order.u16_bytes(0x1122));
            body.extend_from_slice(&order.u16_bytes(0x3344));
            let request = framed(order, CoreOpcode::ChangeProperty, 2, &body);
            let decoded = decode_change_property(order, &request).unwrap();
            assert_eq!(decoded.window, 7);
            assert_eq!(decoded.property, 8);
            assert_eq!(decoded.property_type, 9);
            assert_eq!(decoded.mode, PropertyMode::Append);
            assert_eq!(decoded.format, 16);
            assert_eq!(decoded.item_count, 2);
            assert_eq!(decoded.data.len(), 4);

            let invalid_mode = framed(order, CoreOpcode::ChangeProperty, 3, &body);
            assert_eq!(
                decode_change_property(order, &invalid_mode),
                Err(CoreRequestDecodeError::bad_value(3))
            );
            let mut invalid_format_body = body.clone();
            invalid_format_body[12] = 24;
            let invalid_format = framed(order, CoreOpcode::ChangeProperty, 0, &invalid_format_body);
            assert_eq!(
                decode_change_property(order, &invalid_format),
                Err(CoreRequestDecodeError::bad_value(24))
            );
            let short = framed(order, CoreOpcode::ChangeProperty, 0, &body[..20]);
            assert_eq!(
                decode_change_property(order, &short),
                Err(CoreRequestDecodeError::bad_length())
            );
        }
    }

    #[test]
    fn get_property_decoder_matches_legacy_field_order_and_delete_validation() {
        for order in [ByteOrder::LittleEndian, ByteOrder::BigEndian] {
            let mut body = Vec::new();
            for value in [1, 2, 3, 4, 5] {
                body.extend_from_slice(&order.u32_bytes(value));
            }
            let request = framed(order, CoreOpcode::GetProperty, 1, &body);
            assert_eq!(
                decode_get_property(order, &request).unwrap(),
                GetPropertyRequest {
                    window: 1,
                    property: 2,
                    requested_type: 3,
                    delete_after_read: true,
                    long_offset: 4,
                    long_length: 5,
                }
            );
            let invalid = framed(order, CoreOpcode::GetProperty, 2, &body);
            assert_eq!(
                decode_get_property(order, &invalid),
                Err(CoreRequestDecodeError::bad_value(2))
            );
            let wrong_length = framed(order, CoreOpcode::GetProperty, 2, &body[..16]);
            assert_eq!(
                decode_get_property(order, &wrong_length),
                Err(CoreRequestDecodeError::bad_length())
            );
        }
    }
}
