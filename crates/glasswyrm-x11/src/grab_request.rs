use crate::{ByteOrder, CoreErrorCode, CoreRequestDecodeError, FramedRequest};

pub const ANY_BUTTON: u8 = 0;
pub const ANY_MODIFIER: u16 = 0x8000;
pub const CORE_MODIFIER_MASK: u16 = 0x00ff;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u8)]
pub enum GrabMode {
    Synchronous = 0,
    Asynchronous = 1,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u8)]
pub enum AllowEventsMode {
    AsyncPointer = 0,
    SyncPointer = 1,
    ReplayPointer = 2,
    AsyncKeyboard = 3,
    SyncKeyboard = 4,
    ReplayKeyboard = 5,
    AsyncBoth = 6,
    SyncBoth = 7,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct GrabPointerRequest {
    pub owner_events: bool,
    pub window: u32,
    pub event_mask: u16,
    pub pointer_mode: GrabMode,
    pub keyboard_mode: GrabMode,
    pub confine_to: u32,
    pub cursor: u32,
    pub time: u32,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct GrabButtonRequest {
    pub owner_events: bool,
    pub window: u32,
    pub event_mask: u16,
    pub pointer_mode: GrabMode,
    pub keyboard_mode: GrabMode,
    pub confine_to: u32,
    pub cursor: u32,
    pub button: u8,
    pub modifiers: u16,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct ChangeActivePointerGrabRequest {
    pub cursor: u32,
    pub time: u32,
    pub event_mask: u16,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct GrabKeyboardRequest {
    pub owner_events: bool,
    pub window: u32,
    pub time: u32,
    pub pointer_mode: GrabMode,
    pub keyboard_mode: GrabMode,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct AllowEventsRequest {
    pub mode: AllowEventsMode,
    pub time: u32,
}

pub fn decode_grab_pointer(
    order: ByteOrder,
    request: &FramedRequest,
) -> Result<GrabPointerRequest, CoreRequestDecodeError> {
    exact(request, 24, 20)?;
    let owner_events = decode_owner_events(request.data)?;
    let body = request.body();
    Ok(GrabPointerRequest {
        owner_events,
        window: read_u32(order, body, 0),
        event_mask: read_u16(order, body, 4),
        pointer_mode: decode_mode(body[6])?,
        keyboard_mode: decode_mode(body[7])?,
        confine_to: read_u32(order, body, 8),
        cursor: read_u32(order, body, 12),
        time: read_u32(order, body, 16),
    })
}

pub fn decode_ungrab_pointer(
    order: ByteOrder,
    request: &FramedRequest,
) -> Result<u32, CoreRequestDecodeError> {
    exact(request, 8, 4)?;
    Ok(read_u32(order, request.body(), 0))
}

pub fn decode_grab_button(
    order: ByteOrder,
    request: &FramedRequest,
) -> Result<GrabButtonRequest, CoreRequestDecodeError> {
    exact(request, 24, 20)?;
    let owner_events = decode_owner_events(request.data)?;
    let body = request.body();
    Ok(GrabButtonRequest {
        owner_events,
        window: read_u32(order, body, 0),
        event_mask: read_u16(order, body, 4),
        pointer_mode: decode_mode(body[6])?,
        keyboard_mode: decode_mode(body[7])?,
        confine_to: read_u32(order, body, 8),
        cursor: read_u32(order, body, 12),
        button: body[16],
        modifiers: read_u16(order, body, 18),
    })
}

pub fn validate_grab_button_fields(
    request: GrabButtonRequest,
) -> Result<GrabButtonRequest, CoreRequestDecodeError> {
    let button_invalid = request.button != ANY_BUTTON && !(1..=9).contains(&request.button);
    let modifiers_invalid =
        request.modifiers != ANY_MODIFIER && request.modifiers & !CORE_MODIFIER_MASK != 0;
    if button_invalid || modifiers_invalid {
        let bad_value = if request.button > 9 {
            u32::from(request.button)
        } else {
            u32::from(request.modifiers)
        };
        return Err(error(CoreErrorCode::BadValue, bad_value));
    }
    Ok(request)
}

pub fn decode_change_active_pointer_grab(
    order: ByteOrder,
    request: &FramedRequest,
) -> Result<ChangeActivePointerGrabRequest, CoreRequestDecodeError> {
    exact(request, 16, 12)?;
    let body = request.body();
    Ok(ChangeActivePointerGrabRequest {
        cursor: read_u32(order, body, 0),
        time: read_u32(order, body, 4),
        event_mask: read_u16(order, body, 8),
    })
}

pub fn decode_grab_keyboard(
    order: ByteOrder,
    request: &FramedRequest,
) -> Result<GrabKeyboardRequest, CoreRequestDecodeError> {
    exact(request, 16, 12)?;
    let owner_events = decode_owner_events(request.data)?;
    let body = request.body();
    Ok(GrabKeyboardRequest {
        owner_events,
        window: read_u32(order, body, 0),
        time: read_u32(order, body, 4),
        pointer_mode: decode_mode(body[8])?,
        keyboard_mode: decode_mode(body[9])?,
    })
}

pub fn decode_ungrab_keyboard(
    order: ByteOrder,
    request: &FramedRequest,
) -> Result<u32, CoreRequestDecodeError> {
    decode_ungrab_pointer(order, request)
}

pub fn decode_allow_events(
    order: ByteOrder,
    request: &FramedRequest,
) -> Result<AllowEventsRequest, CoreRequestDecodeError> {
    if request.core_size() != 8 || request.body().len() != 4 || request.data > 7 {
        return Err(error(CoreErrorCode::BadValue, u32::from(request.data)));
    }
    let mode = match request.data {
        0 => AllowEventsMode::AsyncPointer,
        1 => AllowEventsMode::SyncPointer,
        2 => AllowEventsMode::ReplayPointer,
        3 => AllowEventsMode::AsyncKeyboard,
        4 => AllowEventsMode::SyncKeyboard,
        5 => AllowEventsMode::ReplayKeyboard,
        6 => AllowEventsMode::AsyncBoth,
        7 => AllowEventsMode::SyncBoth,
        _ => unreachable!("mode was bounded above"),
    };
    Ok(AllowEventsRequest {
        mode,
        time: read_u32(order, request.body(), 0),
    })
}

fn decode_owner_events(value: u8) -> Result<bool, CoreRequestDecodeError> {
    match value {
        0 => Ok(false),
        1 => Ok(true),
        _ => Err(error(CoreErrorCode::BadValue, u32::from(value))),
    }
}

fn decode_mode(value: u8) -> Result<GrabMode, CoreRequestDecodeError> {
    match value {
        0 => Ok(GrabMode::Synchronous),
        1 => Ok(GrabMode::Asynchronous),
        _ => Err(error(CoreErrorCode::BadValue, u32::from(value))),
    }
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
    fn pointer_and_keyboard_grabs_decode_in_both_byte_orders() {
        for order in [ByteOrder::LittleEndian, ByteOrder::BigEndian] {
            let mut pointer = Vec::new();
            pointer.extend_from_slice(&order.u32_bytes(1));
            pointer.extend_from_slice(&order.u16_bytes(0x1234));
            pointer.extend_from_slice(&[1, 0]);
            pointer.extend_from_slice(&order.u32_bytes(2));
            pointer.extend_from_slice(&order.u32_bytes(3));
            pointer.extend_from_slice(&order.u32_bytes(4));
            assert_eq!(
                decode_grab_pointer(order, &framed(order, 26, 1, &pointer)).unwrap(),
                GrabPointerRequest {
                    owner_events: true,
                    window: 1,
                    event_mask: 0x1234,
                    pointer_mode: GrabMode::Asynchronous,
                    keyboard_mode: GrabMode::Synchronous,
                    confine_to: 2,
                    cursor: 3,
                    time: 4
                }
            );

            let mut keyboard = Vec::new();
            keyboard.extend_from_slice(&order.u32_bytes(5));
            keyboard.extend_from_slice(&order.u32_bytes(6));
            keyboard.extend_from_slice(&[1, 1, 0xaa, 0xbb]);
            assert_eq!(
                decode_grab_keyboard(order, &framed(order, 31, 0, &keyboard)).unwrap(),
                GrabKeyboardRequest {
                    owner_events: false,
                    window: 5,
                    time: 6,
                    pointer_mode: GrabMode::Asynchronous,
                    keyboard_mode: GrabMode::Asynchronous
                }
            );
        }
    }

    #[test]
    fn grab_button_defers_button_and_modifier_checks_until_after_window_lookup() {
        let order = ByteOrder::LittleEndian;
        let mut body = Vec::new();
        body.extend_from_slice(&order.u32_bytes(7));
        body.extend_from_slice(&order.u16_bytes(0x55));
        body.extend_from_slice(&[1, 1]);
        body.extend_from_slice(&order.u32_bytes(0));
        body.extend_from_slice(&order.u32_bytes(0));
        body.extend_from_slice(&[10, 0xff]);
        body.extend_from_slice(&order.u16_bytes(0x8100));
        let decoded = decode_grab_button(order, &framed(order, 28, 0, &body)).unwrap();
        assert_eq!(decoded.window, 7);
        assert_eq!(decoded.button, 10);
        assert_eq!(decoded.modifiers, 0x8100);
        assert_eq!(
            validate_grab_button_fields(decoded),
            Err(error(CoreErrorCode::BadValue, 10))
        );

        let mut modifier_only = decoded;
        modifier_only.button = ANY_BUTTON;
        assert_eq!(
            validate_grab_button_fields(modifier_only),
            Err(error(CoreErrorCode::BadValue, 0x8100))
        );
    }

    #[test]
    fn active_pointer_and_ungrab_requests_preserve_padding_and_time() {
        for order in [ByteOrder::LittleEndian, ByteOrder::BigEndian] {
            let mut active = Vec::new();
            active.extend_from_slice(&order.u32_bytes(1));
            active.extend_from_slice(&order.u32_bytes(2));
            active.extend_from_slice(&order.u16_bytes(3));
            active.extend_from_slice(&[0xaa, 0xbb]);
            assert_eq!(
                decode_change_active_pointer_grab(order, &framed(order, 30, 0, &active)).unwrap(),
                ChangeActivePointerGrabRequest {
                    cursor: 1,
                    time: 2,
                    event_mask: 3
                }
            );
            let time = framed(order, 27, 0, &order.u32_bytes(0x1122_3344));
            assert_eq!(decode_ungrab_pointer(order, &time).unwrap(), 0x1122_3344);
            assert_eq!(decode_ungrab_keyboard(order, &time).unwrap(), 0x1122_3344);
        }
    }

    #[test]
    fn allow_events_preserves_legacy_bad_value_for_bad_length() {
        let order = ByteOrder::LittleEndian;
        let wrong_length = framed(order, 35, 4, &[]);
        assert_eq!(
            decode_allow_events(order, &wrong_length),
            Err(error(CoreErrorCode::BadValue, 4))
        );
        let bad_mode = framed(order, 35, 8, &order.u32_bytes(0));
        assert_eq!(
            decode_allow_events(order, &bad_mode),
            Err(error(CoreErrorCode::BadValue, 8))
        );
        let valid = framed(order, 35, 7, &order.u32_bytes(9));
        assert_eq!(
            decode_allow_events(order, &valid).unwrap(),
            AllowEventsRequest {
                mode: AllowEventsMode::SyncBoth,
                time: 9
            }
        );
    }

    #[test]
    fn invalid_owner_and_mode_fields_fail_before_state() {
        let order = ByteOrder::LittleEndian;
        let mut pointer = vec![0; 20];
        pointer[6] = 2;
        assert_eq!(
            decode_grab_pointer(order, &framed(order, 26, 0, &pointer)),
            Err(error(CoreErrorCode::BadValue, 2))
        );
        pointer[6] = 1;
        assert_eq!(
            decode_grab_pointer(order, &framed(order, 26, 2, &pointer)),
            Err(error(CoreErrorCode::BadValue, 2))
        );
    }
}
