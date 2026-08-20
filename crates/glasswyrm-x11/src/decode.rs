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
