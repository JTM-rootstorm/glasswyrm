use crate::{ByteOrder, CoreErrorCode, CoreRequestDecodeError, EventMask, FramedRequest};

pub const WINDOW_ATTRIBUTE_MASK: u32 = 0x0000_7fff;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct ChangeWindowAttributesRequest<'a> {
    pub window: u32,
    pub value_mask: u32,
    pub values: &'a [u8],
}

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct WindowAttributeValues {
    pub background_pixmap: Option<u32>,
    pub background_pixel: Option<u32>,
    pub border_pixmap: Option<u32>,
    pub border_pixel: Option<u32>,
    pub bit_gravity: Option<u8>,
    pub window_gravity: Option<u8>,
    pub backing_store: Option<u8>,
    pub backing_planes: Option<u32>,
    pub backing_pixel: Option<u32>,
    pub override_redirect: Option<bool>,
    pub save_under: Option<bool>,
    pub event_mask: Option<EventMask>,
    pub do_not_propagate_mask: Option<EventMask>,
    pub colormap: Option<u32>,
    pub cursor: Option<u32>,
}

pub fn decode_change_window_attributes(
    order: ByteOrder,
    request: &FramedRequest,
) -> Result<ChangeWindowAttributesRequest<'_>, CoreRequestDecodeError> {
    if request.core_size() < 12 || request.body().len() < 8 {
        return Err(CoreRequestDecodeError::bad_length());
    }
    let body = request.body();
    let window = read_u32(order, body, 0);
    let value_mask = read_u32(order, body, 4);
    if value_mask & !WINDOW_ATTRIBUTE_MASK != 0 {
        return Err(CoreRequestDecodeError {
            code: CoreErrorCode::BadValue,
            bad_value: value_mask,
        });
    }
    let value_bytes = usize::try_from(value_mask.count_ones())
        .ok()
        .and_then(|count| count.checked_mul(4))
        .ok_or_else(CoreRequestDecodeError::bad_length)?;
    let expected_core = 12_usize
        .checked_add(value_bytes)
        .ok_or_else(CoreRequestDecodeError::bad_length)?;
    let expected_body = 8_usize
        .checked_add(value_bytes)
        .ok_or_else(CoreRequestDecodeError::bad_length)?;
    if request.core_size() != expected_core || body.len() != expected_body {
        return Err(CoreRequestDecodeError::bad_length());
    }
    Ok(ChangeWindowAttributesRequest {
        window,
        value_mask,
        values: &body[8..],
    })
}

pub fn decode_window_attribute_values(
    order: ByteOrder,
    request: ChangeWindowAttributesRequest<'_>,
) -> Result<WindowAttributeValues, CoreRequestDecodeError> {
    let mut result = WindowAttributeValues::default();
    let mut offset = 0;
    for bit in 0..15 {
        if request.value_mask & (1_u32 << bit) == 0 {
            continue;
        }
        let value = read_u32(order, request.values, offset);
        offset += 4;
        match bit {
            0 => {
                if value > 1 {
                    return Err(error(CoreErrorCode::BadPixmap, value));
                }
                result.background_pixmap = Some(value);
            }
            1 => result.background_pixel = Some(value & 0x00ff_ffff),
            2 => {
                if value != 0 {
                    return Err(error(CoreErrorCode::BadPixmap, value));
                }
                result.border_pixmap = Some(value);
            }
            3 => result.border_pixel = Some(value),
            4 => {
                let gravity = u8::try_from(value)
                    .ok()
                    .filter(|value| *value <= 10)
                    .ok_or_else(|| error(CoreErrorCode::BadValue, value))?;
                result.bit_gravity = Some(gravity);
            }
            5 => {
                let gravity = u8::try_from(value)
                    .ok()
                    .filter(|value| *value <= 10)
                    .ok_or_else(|| error(CoreErrorCode::BadValue, value))?;
                result.window_gravity = Some(gravity);
            }
            6 => {
                let backing = u8::try_from(value)
                    .ok()
                    .filter(|value| *value <= 2)
                    .ok_or_else(|| error(CoreErrorCode::BadValue, value))?;
                result.backing_store = Some(backing);
            }
            7 => result.backing_planes = Some(value),
            8 => result.backing_pixel = Some(value),
            9 => {
                if value > 1 {
                    return Err(error(CoreErrorCode::BadValue, value));
                }
                result.override_redirect = Some(value != 0);
            }
            10 => {
                if value > 1 {
                    return Err(error(CoreErrorCode::BadValue, value));
                }
                result.save_under = Some(value != 0);
            }
            11 => {
                result.event_mask = EventMask::from_bits(value)
                    .ok_or_else(|| error(CoreErrorCode::BadValue, value))?
                    .into();
            }
            12 => {
                if value & !EventMask::DO_NOT_PROPAGATE.bits() != 0 {
                    return Err(error(CoreErrorCode::BadValue, value));
                }
                result.do_not_propagate_mask = EventMask::from_bits(value);
            }
            13 => result.colormap = Some(value),
            14 => result.cursor = Some(value),
            _ => unreachable!("the window attribute mask has fifteen bits"),
        }
    }
    debug_assert_eq!(offset, request.values.len());
    Ok(result)
}

fn error(code: CoreErrorCode, bad_value: u32) -> CoreRequestDecodeError {
    CoreRequestDecodeError { code, bad_value }
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

    fn request(order: ByteOrder, window: u32, mask: u32, values: &[u32]) -> FramedRequest {
        let mut bytes = vec![2, 0, 0, 0];
        bytes.extend_from_slice(&order.u32_bytes(window));
        bytes.extend_from_slice(&order.u32_bytes(mask));
        for value in values {
            bytes.extend_from_slice(&order.u32_bytes(*value));
        }
        let units = u16::try_from(bytes.len() / 4).unwrap();
        bytes[2..4].copy_from_slice(&order.u16_bytes(units));
        FramedRequest {
            opcode: 2,
            data: 0,
            length_units: u32::from(units),
            header_size: 4,
            bytes,
        }
    }

    #[test]
    fn shape_decode_precedes_value_validation_like_the_legacy_handler() {
        for order in [ByteOrder::LittleEndian, ByteOrder::BigEndian] {
            let invalid_late_value = request(order, 0x1234, (1 << 1) | (1 << 9), &[0xdead_beef, 2]);
            let shape = decode_change_window_attributes(order, &invalid_late_value).unwrap();
            assert_eq!(shape.window, 0x1234);
            assert_eq!(shape.value_mask, (1 << 1) | (1 << 9));
            assert_eq!(
                decode_window_attribute_values(order, shape),
                Err(error(CoreErrorCode::BadValue, 2))
            );
        }
    }

    #[test]
    fn all_attribute_bits_match_the_legacy_supported_subset() {
        const ALL: u32 = 0x7fff;
        let values = [
            1,
            0x1020_3040,
            0,
            0x5060_7080,
            10,
            10,
            2,
            0xa5a5_a5a5,
            0x1234_5678,
            1,
            1,
            EventMask::EXPOSURE.bits(),
            EventMask::DO_NOT_PROPAGATE.bits(),
            0xfeed_beef,
            0xdead_beef,
        ];
        for order in [ByteOrder::LittleEndian, ByteOrder::BigEndian] {
            let framed = request(order, 7, ALL, &values);
            let shape = decode_change_window_attributes(order, &framed).unwrap();
            let decoded = decode_window_attribute_values(order, shape).unwrap();
            assert_eq!(decoded.background_pixmap, Some(1));
            assert_eq!(decoded.background_pixel, Some(0x0020_3040));
            assert_eq!(decoded.bit_gravity, Some(10));
            assert_eq!(decoded.window_gravity, Some(10));
            assert_eq!(decoded.backing_store, Some(2));
            assert_eq!(decoded.override_redirect, Some(true));
            assert_eq!(decoded.save_under, Some(true));
            assert_eq!(decoded.event_mask, Some(EventMask::EXPOSURE));
            assert_eq!(
                decoded.do_not_propagate_mask,
                Some(EventMask::DO_NOT_PROPAGATE)
            );
            assert_eq!(decoded.colormap, Some(0xfeed_beef));
            assert_eq!(decoded.cursor, Some(0xdead_beef));
        }
    }

    #[test]
    fn mask_and_length_fail_before_value_decode() {
        let order = ByteOrder::LittleEndian;
        let unknown = request(order, 1, 1 << 15, &[0]);
        assert_eq!(
            decode_change_window_attributes(order, &unknown),
            Err(error(CoreErrorCode::BadValue, 1 << 15))
        );

        let mut short = request(order, 1, 1 << 1, &[0]);
        short.bytes.truncate(12);
        short.length_units = 3;
        short.bytes[2..4].copy_from_slice(&3_u16.to_le_bytes());
        assert_eq!(
            decode_change_window_attributes(order, &short),
            Err(CoreRequestDecodeError::bad_length())
        );
    }

    #[test]
    fn resource_ids_are_left_for_state_validation() {
        let order = ByteOrder::BigEndian;
        let framed = request(order, 1, (1 << 13) | (1 << 14), &[0x1111_2222, 0x3333_4444]);
        let shape = decode_change_window_attributes(order, &framed).unwrap();
        let decoded = decode_window_attribute_values(order, shape).unwrap();
        assert_eq!(decoded.colormap, Some(0x1111_2222));
        assert_eq!(decoded.cursor, Some(0x3333_4444));
    }
}
