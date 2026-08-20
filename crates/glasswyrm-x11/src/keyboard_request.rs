use crate::{ByteOrder, CoreErrorCode, CoreRequestDecodeError, FramedRequest};

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct ChangeKeyboardControlRequest<'a> {
    pub value_mask: u32,
    values: &'a [u8],
}

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct KeyboardControlValues {
    pub key_click_percent: Option<i32>,
    pub bell_percent: Option<i32>,
    pub bell_pitch: Option<i32>,
    pub bell_duration: Option<i32>,
    pub global_auto_repeat: Option<i32>,
}

pub fn decode_query_keymap(request: &FramedRequest) -> Result<(), CoreRequestDecodeError> {
    exact_empty(request)
}

pub fn decode_change_keyboard_control(
    order: ByteOrder,
    request: &FramedRequest,
) -> Result<ChangeKeyboardControlRequest<'_>, CoreRequestDecodeError> {
    if request.bytes.len() < 8 || request.body().len() < 4 {
        return Err(CoreRequestDecodeError::bad_length());
    }
    let body = request.body();
    let mask = read_u32(order, body, 0);
    let values = &body[4..];
    if mask & !0xff != 0
        || usize::try_from(mask.count_ones()).ok() != Some(values.len() / 4)
        || values.len() & 3 != 0
    {
        return Err(CoreRequestDecodeError::bad_length());
    }
    if mask & 0x70 != 0 {
        return Err(error(CoreErrorCode::BadImplementation, 0));
    }
    Ok(ChangeKeyboardControlRequest {
        value_mask: mask,
        values,
    })
}

pub fn decode_keyboard_control_values(
    order: ByteOrder,
    request: ChangeKeyboardControlRequest<'_>,
) -> Result<KeyboardControlValues, CoreRequestDecodeError> {
    let mut result = KeyboardControlValues::default();
    let mut offset = 0;
    for bit in 0..8 {
        if request.value_mask & (1_u32 << bit) == 0 {
            continue;
        }
        let wire = read_u32(order, request.values, offset);
        offset += 4;
        let value = wire as i32;
        match bit {
            0 => {
                if !(-1..=100).contains(&value) {
                    return Err(error(CoreErrorCode::BadValue, wire));
                }
                result.key_click_percent = Some(value);
            }
            1 => {
                if !(-1..=100).contains(&value) {
                    return Err(error(CoreErrorCode::BadValue, wire));
                }
                result.bell_percent = Some(value);
            }
            2 => {
                if value < -1 || value > i32::from(u16::MAX) {
                    return Err(error(CoreErrorCode::BadValue, wire));
                }
                result.bell_pitch = Some(value);
            }
            3 => {
                if value < -1 || value > i32::from(u16::MAX) {
                    return Err(error(CoreErrorCode::BadValue, wire));
                }
                result.bell_duration = Some(value);
            }
            7 => {
                if !(0..=2).contains(&value) {
                    return Err(error(CoreErrorCode::BadValue, wire));
                }
                result.global_auto_repeat = Some(value);
            }
            4..=6 => unreachable!("unsupported bits are rejected before value decoding"),
            _ => unreachable!("keyboard control mask is eight bits"),
        }
    }
    debug_assert_eq!(offset, request.values.len());
    Ok(result)
}

pub fn decode_get_keyboard_control(request: &FramedRequest) -> Result<(), CoreRequestDecodeError> {
    exact_empty(request)
}

pub fn decode_bell(request: &FramedRequest) -> Result<i8, CoreRequestDecodeError> {
    exact_empty(request)?;
    let percent = request.data as i8;
    if !(-100..=100).contains(&percent) {
        return Err(error(CoreErrorCode::BadValue, u32::from(request.data)));
    }
    Ok(percent)
}

pub fn decode_force_screen_saver(request: &FramedRequest) -> Result<bool, CoreRequestDecodeError> {
    exact_empty(request)?;
    match request.data {
        0 => Ok(false),
        1 => Ok(true),
        value => Err(error(CoreErrorCode::BadValue, u32::from(value))),
    }
}

fn exact_empty(request: &FramedRequest) -> Result<(), CoreRequestDecodeError> {
    if request.core_size() == 4 && request.body().is_empty() {
        Ok(())
    } else {
        Err(CoreRequestDecodeError::bad_length())
    }
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
    fn change_keyboard_control_preserves_mask_and_value_semantics() {
        for order in [ByteOrder::LittleEndian, ByteOrder::BigEndian] {
            let mask = 0x8f;
            let mut body = Vec::new();
            body.extend_from_slice(&order.u32_bytes(mask));
            for value in [-1_i32, 100, 400, 100, 2] {
                body.extend_from_slice(&order.u32_bytes(value as u32));
            }
            let framed_request = framed(order, 102, 0, &body);
            let wire = decode_change_keyboard_control(order, &framed_request).unwrap();
            let values = decode_keyboard_control_values(order, wire).unwrap();
            assert_eq!(values.key_click_percent, Some(-1));
            assert_eq!(values.bell_percent, Some(100));
            assert_eq!(values.bell_pitch, Some(400));
            assert_eq!(values.bell_duration, Some(100));
            assert_eq!(values.global_auto_repeat, Some(2));
        }
    }

    #[test]
    fn keyboard_control_mask_errors_match_legacy_categories() {
        let order = ByteOrder::LittleEndian;
        let mut unknown = Vec::new();
        unknown.extend_from_slice(&order.u32_bytes(0x100));
        unknown.extend_from_slice(&order.u32_bytes(0));
        assert_eq!(
            decode_change_keyboard_control(order, &framed(order, 102, 0, &unknown)),
            Err(CoreRequestDecodeError::bad_length())
        );

        let mut unsupported = Vec::new();
        unsupported.extend_from_slice(&order.u32_bytes(0x10));
        unsupported.extend_from_slice(&order.u32_bytes(0));
        assert_eq!(
            decode_change_keyboard_control(order, &framed(order, 102, 0, &unsupported)),
            Err(error(CoreErrorCode::BadImplementation, 0))
        );
    }

    #[test]
    fn keyboard_control_values_fail_with_raw_bad_value() {
        let order = ByteOrder::LittleEndian;
        let mut body = Vec::new();
        body.extend_from_slice(&order.u32_bytes(1));
        body.extend_from_slice(&order.u32_bytes((-2_i32) as u32));
        let framed_request = framed(order, 102, 0, &body);
        let wire = decode_change_keyboard_control(order, &framed_request).unwrap();
        assert_eq!(
            decode_keyboard_control_values(order, wire),
            Err(error(CoreErrorCode::BadValue, (-2_i32) as u32))
        );
    }

    #[test]
    fn empty_keyboard_queries_bell_and_saver_match_legacy() {
        let order = ByteOrder::LittleEndian;
        let empty = framed(order, 44, 0, &[]);
        assert_eq!(decode_query_keymap(&empty), Ok(()));
        assert_eq!(decode_get_keyboard_control(&empty), Ok(()));

        assert_eq!(decode_bell(&framed(order, 104, 100, &[])), Ok(100));
        assert_eq!(decode_bell(&framed(order, 104, 156, &[])), Ok(-100));
        assert_eq!(
            decode_bell(&framed(order, 104, 128, &[])),
            Err(error(CoreErrorCode::BadValue, 128))
        );
        assert_eq!(
            decode_force_screen_saver(&framed(order, 115, 1, &[])),
            Ok(true)
        );
        assert_eq!(
            decode_force_screen_saver(&framed(order, 115, 2, &[])),
            Err(error(CoreErrorCode::BadValue, 2))
        );
    }
}
