use crate::{ByteOrder, CoreOpcode, FramedRequest};

pub const CONFIGURE_X: u16 = 1 << 0;
pub const CONFIGURE_Y: u16 = 1 << 1;
pub const CONFIGURE_WIDTH: u16 = 1 << 2;
pub const CONFIGURE_HEIGHT: u16 = 1 << 3;
pub const CONFIGURE_BORDER_WIDTH: u16 = 1 << 4;
pub const CONFIGURE_SIBLING: u16 = 1 << 5;
pub const CONFIGURE_STACK_MODE: u16 = 1 << 6;
pub const KNOWN_CONFIGURE_MASK: u16 = 0x007f;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum LifecycleDecodeStatus {
    Complete,
    BadLength,
    BadValue,
    BadMatch,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct WindowLifecycleRequest {
    pub window: u32,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u8)]
pub enum CoreStackMode {
    Above = 0,
    Below = 1,
    TopIf = 2,
    BottomIf = 3,
    Opposite = 4,
}

impl CoreStackMode {
    fn from_u32(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Above),
            1 => Some(Self::Below),
            2 => Some(Self::TopIf),
            3 => Some(Self::BottomIf),
            4 => Some(Self::Opposite),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct ConfigureWindowRequest {
    pub window: u32,
    pub value_mask: u16,
    pub x: Option<i32>,
    pub y: Option<i32>,
    pub width: Option<u32>,
    pub height: Option<u32>,
    pub border_width: Option<u32>,
    pub sibling: Option<u32>,
    pub stack_mode: Option<CoreStackMode>,
}

pub fn decode_map_window(
    request: &FramedRequest,
    order: ByteOrder,
) -> Result<WindowLifecycleRequest, LifecycleDecodeStatus> {
    decode_window(request, order, CoreOpcode::MapWindow)
}

pub fn decode_map_subwindows(
    request: &FramedRequest,
    order: ByteOrder,
) -> Result<WindowLifecycleRequest, LifecycleDecodeStatus> {
    decode_window(request, order, CoreOpcode::MapSubwindows)
}

pub fn decode_unmap_window(
    request: &FramedRequest,
    order: ByteOrder,
) -> Result<WindowLifecycleRequest, LifecycleDecodeStatus> {
    decode_window(request, order, CoreOpcode::UnmapWindow)
}

pub fn decode_unmap_subwindows(
    request: &FramedRequest,
    order: ByteOrder,
) -> Result<WindowLifecycleRequest, LifecycleDecodeStatus> {
    decode_window(request, order, CoreOpcode::UnmapSubwindows)
}

fn decode_window(
    request: &FramedRequest,
    order: ByteOrder,
    expected_opcode: CoreOpcode,
) -> Result<WindowLifecycleRequest, LifecycleDecodeStatus> {
    if request.opcode != expected_opcode as u8
        || request.core_size() != 8
        || request.body().len() != 4
    {
        return Err(LifecycleDecodeStatus::BadLength);
    }
    Ok(WindowLifecycleRequest {
        window: read_u32(order, request.body(), 0),
    })
}

pub fn decode_configure_window(
    request: &FramedRequest,
    order: ByteOrder,
) -> Result<ConfigureWindowRequest, LifecycleDecodeStatus> {
    if request.opcode != CoreOpcode::ConfigureWindow as u8
        || request.core_size() < 12
        || request.body().len() < 8
    {
        return Err(LifecycleDecodeStatus::BadLength);
    }
    let body = request.body();
    let window = read_u32(order, body, 0);
    let mask = read_u16(order, body, 4);
    if mask & !KNOWN_CONFIGURE_MASK != 0 {
        return Err(LifecycleDecodeStatus::BadValue);
    }
    let value_count = mask.count_ones() as usize;
    let expected_units = 3_u32
        .checked_add(u32::try_from(value_count).map_err(|_| LifecycleDecodeStatus::BadLength)?)
        .ok_or(LifecycleDecodeStatus::BadLength)?;
    let expected_size = usize::try_from(expected_units)
        .ok()
        .and_then(|units| units.checked_mul(4))
        .ok_or(LifecycleDecodeStatus::BadLength)?;
    if request.core_size() != expected_size || body.len() != 8 + value_count * 4 {
        return Err(LifecycleDecodeStatus::BadLength);
    }

    let mut decoded = ConfigureWindowRequest {
        window,
        value_mask: mask,
        ..Default::default()
    };
    let mut offset = 8;
    let mut next = || {
        let value = read_u32(order, body, offset);
        offset += 4;
        value
    };
    if mask & CONFIGURE_X != 0 {
        decoded.x = Some(next() as i32);
    }
    if mask & CONFIGURE_Y != 0 {
        decoded.y = Some(next() as i32);
    }
    if mask & CONFIGURE_WIDTH != 0 {
        let value = next();
        if value == 0 {
            return Err(LifecycleDecodeStatus::BadValue);
        }
        decoded.width = Some(value);
    }
    if mask & CONFIGURE_HEIGHT != 0 {
        let value = next();
        if value == 0 {
            return Err(LifecycleDecodeStatus::BadValue);
        }
        decoded.height = Some(value);
    }
    if mask & CONFIGURE_BORDER_WIDTH != 0 {
        decoded.border_width = Some(next());
    }
    if mask & CONFIGURE_SIBLING != 0 {
        decoded.sibling = Some(next());
    }
    if mask & CONFIGURE_STACK_MODE != 0 {
        decoded.stack_mode =
            Some(CoreStackMode::from_u32(next()).ok_or(LifecycleDecodeStatus::BadValue)?);
    }
    if decoded.sibling.is_some() && decoded.stack_mode.is_none() {
        return Err(LifecycleDecodeStatus::BadMatch);
    }
    if decoded.sibling == Some(decoded.window) {
        return Err(LifecycleDecodeStatus::BadMatch);
    }
    debug_assert_eq!(offset, body.len());
    Ok(decoded)
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

    fn simple(order: ByteOrder, opcode: CoreOpcode, window: u32) -> FramedRequest {
        let mut bytes = vec![opcode as u8, 0];
        bytes.extend_from_slice(&order.u16_bytes(2));
        bytes.extend_from_slice(&order.u32_bytes(window));
        FramedRequest {
            opcode: opcode as u8,
            data: 0,
            length_units: 2,
            header_size: 4,
            bytes,
        }
    }

    fn configure(order: ByteOrder, mask: u16, values: &[u32]) -> FramedRequest {
        let units = 3 + values.len() as u16;
        let mut bytes = vec![CoreOpcode::ConfigureWindow as u8, 0];
        bytes.extend_from_slice(&order.u16_bytes(units));
        bytes.extend_from_slice(&order.u32_bytes(0x1020_3040));
        bytes.extend_from_slice(&order.u16_bytes(mask));
        bytes.extend_from_slice(&[0, 0]);
        for value in values {
            bytes.extend_from_slice(&order.u32_bytes(*value));
        }
        FramedRequest {
            opcode: CoreOpcode::ConfigureWindow as u8,
            data: 0,
            length_units: u32::from(units),
            header_size: 4,
            bytes,
        }
    }

    fn as_big_request(order: ByteOrder, request: &FramedRequest) -> FramedRequest {
        let mut bytes = request.bytes[..2].to_vec();
        bytes.extend_from_slice(&[0, 0]);
        bytes.extend_from_slice(&order.u32_bytes(request.length_units + 1));
        bytes.extend_from_slice(request.body());
        FramedRequest {
            opcode: request.opcode,
            data: request.data,
            length_units: request.length_units + 1,
            header_size: 8,
            bytes,
        }
    }

    #[test]
    fn simple_lifecycle_requests_match_legacy_decoder() {
        for order in [ByteOrder::LittleEndian, ByteOrder::BigEndian] {
            assert_eq!(
                decode_map_window(&simple(order, CoreOpcode::MapWindow, 0x1234_5678), order),
                Ok(WindowLifecycleRequest {
                    window: 0x1234_5678
                })
            );
            assert_eq!(
                decode_map_subwindows(
                    &simple(order, CoreOpcode::MapSubwindows, 0x1234_5678),
                    order,
                ),
                Ok(WindowLifecycleRequest {
                    window: 0x1234_5678
                })
            );
            assert_eq!(
                decode_unmap_window(&simple(order, CoreOpcode::UnmapWindow, 9), order),
                Ok(WindowLifecycleRequest { window: 9 })
            );
            assert_eq!(
                decode_unmap_subwindows(&simple(order, CoreOpcode::UnmapSubwindows, 9), order),
                Ok(WindowLifecycleRequest { window: 9 })
            );

            let mut truncated = simple(order, CoreOpcode::UnmapWindow, 9);
            truncated.bytes.pop();
            assert_eq!(
                decode_unmap_window(&truncated, order),
                Err(LifecycleDecodeStatus::BadLength)
            );

            let mut big_header = simple(order, CoreOpcode::MapWindow, 9);
            big_header.header_size = 8;
            assert_eq!(
                decode_map_window(&big_header, order),
                Err(LifecycleDecodeStatus::BadLength)
            );
        }
    }

    #[test]
    fn lifecycle_decoders_accept_big_requests_normalized_to_the_core_shape() {
        for order in [ByteOrder::LittleEndian, ByteOrder::BigEndian] {
            let simple = simple(order, CoreOpcode::MapWindow, 0x1234_5678);
            assert_eq!(
                decode_map_window(&as_big_request(order, &simple), order),
                Ok(WindowLifecycleRequest {
                    window: 0x1234_5678
                })
            );

            let configure = configure(order, CONFIGURE_X | CONFIGURE_Y, &[7, 9]);
            assert_eq!(
                decode_configure_window(&as_big_request(order, &configure), order),
                decode_configure_window(&configure, order)
            );
        }
    }

    #[test]
    fn configure_window_matches_legacy_order_and_negative_matrix() {
        let mask = CONFIGURE_X
            | CONFIGURE_Y
            | CONFIGURE_WIDTH
            | CONFIGURE_HEIGHT
            | CONFIGURE_BORDER_WIDTH
            | CONFIGURE_SIBLING
            | CONFIGURE_STACK_MODE;
        for order in [ByteOrder::LittleEndian, ByteOrder::BigEndian] {
            let decoded = decode_configure_window(
                &configure(order, mask, &[0xffff_fffe, 7, 640, 480, 3, 99, 1]),
                order,
            )
            .unwrap();
            assert_eq!(decoded.x, Some(-2));
            assert_eq!(decoded.y, Some(7));
            assert_eq!(decoded.width, Some(640));
            assert_eq!(decoded.stack_mode, Some(CoreStackMode::Below));
        }
        let order = ByteOrder::LittleEndian;
        assert_eq!(
            decode_configure_window(&configure(order, CONFIGURE_WIDTH, &[0]), order),
            Err(LifecycleDecodeStatus::BadValue)
        );
        assert_eq!(
            decode_configure_window(&configure(order, 0x80, &[1]), order),
            Err(LifecycleDecodeStatus::BadValue)
        );
        assert_eq!(
            decode_configure_window(&configure(order, CONFIGURE_SIBLING, &[99]), order),
            Err(LifecycleDecodeStatus::BadMatch)
        );
        assert_eq!(
            decode_configure_window(&configure(order, CONFIGURE_STACK_MODE, &[5]), order),
            Err(LifecycleDecodeStatus::BadValue)
        );
        let mut wrong_opcode = configure(order, CONFIGURE_WIDTH, &[640]);
        wrong_opcode.opcode = CoreOpcode::MapWindow as u8;
        wrong_opcode.bytes[0] = CoreOpcode::MapWindow as u8;
        assert_eq!(
            decode_configure_window(&wrong_opcode, order),
            Err(LifecycleDecodeStatus::BadLength)
        );
        assert_eq!(
            decode_configure_window(
                &configure(
                    order,
                    CONFIGURE_SIBLING | CONFIGURE_STACK_MODE,
                    &[0x1020_3040, 0],
                ),
                order,
            ),
            Err(LifecycleDecodeStatus::BadMatch)
        );
    }

    #[test]
    fn configure_reserved_padding_matches_legacy_ignored_behavior() {
        let order = ByteOrder::LittleEndian;
        let mut request = configure(order, CONFIGURE_WIDTH, &[640]);
        request.bytes[10] = 0xaa;
        request.bytes[11] = 0x55;
        assert_eq!(
            decode_configure_window(&request, order).unwrap().width,
            Some(640)
        );
    }
}
