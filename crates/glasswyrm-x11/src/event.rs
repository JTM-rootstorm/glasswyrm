use crate::wire::Writer;
use crate::{ByteOrder, CoreEventType, wire_sequence};

pub const CORE_EVENT_SIZE: usize = 32;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum EventEncodeError {
    InvalidEventType,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct DestroyNotifyEvent {
    pub event: u32,
    pub window: u32,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct UnmapNotifyEvent {
    pub event: u32,
    pub window: u32,
    pub from_configure: bool,
    pub synthetic: bool,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct MapNotifyEvent {
    pub event: u32,
    pub window: u32,
    pub override_redirect: bool,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct ConfigureNotifyEvent {
    pub event: u32,
    pub window: u32,
    pub above_sibling: u32,
    pub x: i16,
    pub y: i16,
    pub width: u16,
    pub height: u16,
    pub border_width: u16,
    pub override_redirect: bool,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u8)]
pub enum PropertyNotifyState {
    NewValue = 0,
    Deleted = 1,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct PropertyNotifyEvent {
    pub window: u32,
    pub atom: u32,
    pub time: u32,
    pub state: PropertyNotifyState,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct SelectionClearEvent {
    pub time: u32,
    pub owner: u32,
    pub selection: u32,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct SelectionRequestEvent {
    pub time: u32,
    pub owner: u32,
    pub requestor: u32,
    pub selection: u32,
    pub target: u32,
    pub property: u32,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct SelectionNotifyEvent {
    pub time: u32,
    pub requestor: u32,
    pub selection: u32,
    pub target: u32,
    pub property: u32,
    pub synthetic: bool,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum ClientMessageData {
    U8([u8; 20]),
    U16([u16; 10]),
    U32([u32; 5]),
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ClientMessageEvent {
    pub window: u32,
    pub message_type: u32,
    pub data: ClientMessageData,
    pub synthetic: bool,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct InputEvent {
    pub event_type: CoreEventType,
    pub detail: u8,
    pub time: u32,
    pub root: u32,
    pub event: u32,
    pub child: u32,
    pub root_x: i16,
    pub root_y: i16,
    pub event_x: i16,
    pub event_y: i16,
    pub state: u16,
    pub same_screen: bool,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u8)]
pub enum NotifyDetail {
    Ancestor = 0,
    Virtual = 1,
    Inferior = 2,
    Nonlinear = 3,
    NonlinearVirtual = 4,
    Pointer = 5,
    PointerRoot = 6,
    None = 7,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u8)]
pub enum NotifyMode {
    Normal = 0,
    Grab = 1,
    Ungrab = 2,
    WhileGrabbed = 3,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct CrossingEvent {
    pub event_type: CoreEventType,
    pub detail: NotifyDetail,
    pub time: u32,
    pub root: u32,
    pub event: u32,
    pub child: u32,
    pub root_x: i16,
    pub root_y: i16,
    pub event_x: i16,
    pub event_y: i16,
    pub state: u16,
    pub mode: NotifyMode,
    pub same_screen: bool,
    pub focus: bool,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct FocusEvent {
    pub event_type: CoreEventType,
    pub detail: NotifyDetail,
    pub event: u32,
    pub mode: NotifyMode,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct ExposeEvent {
    pub window: u32,
    pub x: u16,
    pub y: u16,
    pub width: u16,
    pub height: u16,
    pub count: u16,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct GraphicsExposeEvent {
    pub drawable: u32,
    pub x: u16,
    pub y: u16,
    pub width: u16,
    pub height: u16,
    pub minor_opcode: u16,
    pub count: u16,
    pub major_opcode: u8,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct NoExposeEvent {
    pub drawable: u32,
    pub minor_opcode: u16,
    pub major_opcode: u8,
}

fn event_header(
    order: ByteOrder,
    event_type: CoreEventType,
    sequence: u64,
    synthetic: bool,
) -> Writer {
    let mut writer = Writer::new(order);
    writer.write_u8(event_type as u8 | if synthetic { 0x80 } else { 0 });
    writer.write_u8(0);
    writer.write_u16(wire_sequence(sequence));
    writer
}

fn finish_event(mut writer: Writer) -> Vec<u8> {
    debug_assert!(writer.len() <= CORE_EVENT_SIZE);
    writer.write_padding(CORE_EVENT_SIZE - writer.len());
    writer.into_bytes()
}

#[must_use]
pub fn encode_destroy_notify(
    order: ByteOrder,
    sequence: u64,
    event: DestroyNotifyEvent,
) -> Vec<u8> {
    let mut writer = event_header(order, CoreEventType::DestroyNotify, sequence, false);
    writer.write_u32(event.event);
    writer.write_u32(event.window);
    finish_event(writer)
}

#[must_use]
pub fn encode_unmap_notify(order: ByteOrder, sequence: u64, event: UnmapNotifyEvent) -> Vec<u8> {
    let mut writer = event_header(order, CoreEventType::UnmapNotify, sequence, event.synthetic);
    writer.write_u32(event.event);
    writer.write_u32(event.window);
    writer.write_u8(u8::from(event.from_configure));
    writer.write_padding(3);
    finish_event(writer)
}

#[must_use]
pub fn encode_map_notify(order: ByteOrder, sequence: u64, event: MapNotifyEvent) -> Vec<u8> {
    let mut writer = event_header(order, CoreEventType::MapNotify, sequence, false);
    writer.write_u32(event.event);
    writer.write_u32(event.window);
    writer.write_u8(u8::from(event.override_redirect));
    writer.write_padding(3);
    finish_event(writer)
}

#[must_use]
pub fn encode_configure_notify(
    order: ByteOrder,
    sequence: u64,
    event: ConfigureNotifyEvent,
) -> Vec<u8> {
    let mut writer = event_header(order, CoreEventType::ConfigureNotify, sequence, false);
    writer.write_u32(event.event);
    writer.write_u32(event.window);
    writer.write_u32(event.above_sibling);
    writer.write_u16(event.x as u16);
    writer.write_u16(event.y as u16);
    writer.write_u16(event.width);
    writer.write_u16(event.height);
    writer.write_u16(event.border_width);
    writer.write_u8(u8::from(event.override_redirect));
    writer.write_u8(0);
    finish_event(writer)
}

#[must_use]
pub fn encode_property_notify(
    order: ByteOrder,
    sequence: u64,
    event: PropertyNotifyEvent,
) -> Vec<u8> {
    let mut writer = event_header(order, CoreEventType::PropertyNotify, sequence, false);
    writer.write_u32(event.window);
    writer.write_u32(event.atom);
    writer.write_u32(event.time);
    writer.write_u8(event.state as u8);
    finish_event(writer)
}

#[must_use]
pub fn encode_selection_clear(
    order: ByteOrder,
    sequence: u64,
    event: SelectionClearEvent,
) -> Vec<u8> {
    let mut writer = event_header(order, CoreEventType::SelectionClear, sequence, false);
    writer.write_u32(event.time);
    writer.write_u32(event.owner);
    writer.write_u32(event.selection);
    finish_event(writer)
}

#[must_use]
pub fn encode_selection_request(
    order: ByteOrder,
    sequence: u64,
    event: SelectionRequestEvent,
) -> Vec<u8> {
    let mut writer = event_header(order, CoreEventType::SelectionRequest, sequence, false);
    writer.write_u32(event.time);
    writer.write_u32(event.owner);
    writer.write_u32(event.requestor);
    writer.write_u32(event.selection);
    writer.write_u32(event.target);
    writer.write_u32(event.property);
    finish_event(writer)
}

#[must_use]
pub fn encode_selection_notify(
    order: ByteOrder,
    sequence: u64,
    event: SelectionNotifyEvent,
) -> Vec<u8> {
    let mut writer = event_header(
        order,
        CoreEventType::SelectionNotify,
        sequence,
        event.synthetic,
    );
    writer.write_u32(event.time);
    writer.write_u32(event.requestor);
    writer.write_u32(event.selection);
    writer.write_u32(event.target);
    writer.write_u32(event.property);
    finish_event(writer)
}

#[must_use]
pub fn encode_client_message(
    order: ByteOrder,
    sequence: u64,
    event: &ClientMessageEvent,
) -> Vec<u8> {
    let mut writer = Writer::new(order);
    writer.write_u8(CoreEventType::ClientMessage as u8 | if event.synthetic { 0x80 } else { 0 });
    match &event.data {
        ClientMessageData::U8(values) => {
            writer.write_u8(8);
            writer.write_u16(wire_sequence(sequence));
            writer.write_u32(event.window);
            writer.write_u32(event.message_type);
            writer.write_bytes(values);
        }
        ClientMessageData::U16(values) => {
            writer.write_u8(16);
            writer.write_u16(wire_sequence(sequence));
            writer.write_u32(event.window);
            writer.write_u32(event.message_type);
            for value in values {
                writer.write_u16(*value);
            }
        }
        ClientMessageData::U32(values) => {
            writer.write_u8(32);
            writer.write_u16(wire_sequence(sequence));
            writer.write_u32(event.window);
            writer.write_u32(event.message_type);
            for value in values {
                writer.write_u32(*value);
            }
        }
    }
    finish_event(writer)
}

pub fn encode_input_event(
    order: ByteOrder,
    sequence: u64,
    event: InputEvent,
) -> Result<Vec<u8>, EventEncodeError> {
    if !matches!(
        event.event_type,
        CoreEventType::KeyPress
            | CoreEventType::KeyRelease
            | CoreEventType::ButtonPress
            | CoreEventType::ButtonRelease
            | CoreEventType::MotionNotify
    ) {
        return Err(EventEncodeError::InvalidEventType);
    }
    let mut writer = Writer::new(order);
    writer.write_u8(event.event_type as u8);
    writer.write_u8(if event.event_type == CoreEventType::MotionNotify {
        0
    } else {
        event.detail
    });
    writer.write_u16(wire_sequence(sequence));
    writer.write_u32(event.time);
    writer.write_u32(event.root);
    writer.write_u32(event.event);
    writer.write_u32(event.child);
    writer.write_u16(event.root_x as u16);
    writer.write_u16(event.root_y as u16);
    writer.write_u16(event.event_x as u16);
    writer.write_u16(event.event_y as u16);
    writer.write_u16(event.state);
    writer.write_u8(u8::from(event.same_screen));
    writer.write_u8(0);
    debug_assert_eq!(writer.len(), CORE_EVENT_SIZE);
    Ok(writer.into_bytes())
}

pub fn encode_crossing_event(
    order: ByteOrder,
    sequence: u64,
    event: CrossingEvent,
) -> Result<Vec<u8>, EventEncodeError> {
    if !matches!(
        event.event_type,
        CoreEventType::EnterNotify | CoreEventType::LeaveNotify
    ) {
        return Err(EventEncodeError::InvalidEventType);
    }
    let mut writer = Writer::new(order);
    writer.write_u8(event.event_type as u8);
    writer.write_u8(event.detail as u8);
    writer.write_u16(wire_sequence(sequence));
    writer.write_u32(event.time);
    writer.write_u32(event.root);
    writer.write_u32(event.event);
    writer.write_u32(event.child);
    writer.write_u16(event.root_x as u16);
    writer.write_u16(event.root_y as u16);
    writer.write_u16(event.event_x as u16);
    writer.write_u16(event.event_y as u16);
    writer.write_u16(event.state);
    writer.write_u8(event.mode as u8);
    writer.write_u8(u8::from(event.same_screen) | (u8::from(event.focus) << 1));
    debug_assert_eq!(writer.len(), CORE_EVENT_SIZE);
    Ok(writer.into_bytes())
}

pub fn encode_focus_event(
    order: ByteOrder,
    sequence: u64,
    event: FocusEvent,
) -> Result<Vec<u8>, EventEncodeError> {
    if !matches!(
        event.event_type,
        CoreEventType::FocusIn | CoreEventType::FocusOut
    ) {
        return Err(EventEncodeError::InvalidEventType);
    }
    let mut writer = Writer::new(order);
    writer.write_u8(event.event_type as u8);
    writer.write_u8(event.detail as u8);
    writer.write_u16(wire_sequence(sequence));
    writer.write_u32(event.event);
    writer.write_u8(event.mode as u8);
    writer.write_padding(23);
    debug_assert_eq!(writer.len(), CORE_EVENT_SIZE);
    Ok(writer.into_bytes())
}

#[must_use]
pub fn encode_expose(order: ByteOrder, sequence: u64, event: ExposeEvent) -> Vec<u8> {
    let mut writer = event_header(order, CoreEventType::Expose, sequence, false);
    writer.write_u32(event.window);
    writer.write_u16(event.x);
    writer.write_u16(event.y);
    writer.write_u16(event.width);
    writer.write_u16(event.height);
    writer.write_u16(event.count);
    finish_event(writer)
}

#[must_use]
pub fn encode_graphics_expose(
    order: ByteOrder,
    sequence: u64,
    event: GraphicsExposeEvent,
) -> Vec<u8> {
    let mut writer = event_header(order, CoreEventType::GraphicsExpose, sequence, false);
    writer.write_u32(event.drawable);
    writer.write_u16(event.x);
    writer.write_u16(event.y);
    writer.write_u16(event.width);
    writer.write_u16(event.height);
    writer.write_u16(event.minor_opcode);
    writer.write_u16(event.count);
    writer.write_u8(event.major_opcode);
    writer.write_padding(3);
    finish_event(writer)
}

#[must_use]
pub fn encode_no_expose(order: ByteOrder, sequence: u64, event: NoExposeEvent) -> Vec<u8> {
    let mut writer = event_header(order, CoreEventType::NoExpose, sequence, false);
    writer.write_u32(event.drawable);
    writer.write_u16(event.minor_opcode);
    writer.write_u8(event.major_opcode);
    writer.write_padding(1);
    finish_event(writer)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn structural_property_selection_and_client_message_match_legacy_vectors() {
        assert_eq!(
            encode_destroy_notify(
                ByteOrder::LittleEndian,
                0x12345,
                DestroyNotifyEvent {
                    event: 0x0102_0304,
                    window: 0xa0b0_c0d0,
                },
            ),
            [17, 0, 0x45, 0x23, 4, 3, 2, 1, 0xd0, 0xc0, 0xb0, 0xa0]
                .into_iter()
                .chain([0; 20])
                .collect::<Vec<_>>()
        );
        assert_eq!(
            encode_unmap_notify(
                ByteOrder::BigEndian,
                0x12345,
                UnmapNotifyEvent {
                    event: 0x0102_0304,
                    window: 0xa0b0_c0d0,
                    from_configure: true,
                    synthetic: false,
                },
            ),
            [18, 0, 0x23, 0x45, 1, 2, 3, 4, 0xa0, 0xb0, 0xc0, 0xd0, 1]
                .into_iter()
                .chain([0; 19])
                .collect::<Vec<_>>()
        );
        assert_eq!(
            encode_configure_notify(
                ByteOrder::BigEndian,
                9,
                ConfigureNotifyEvent {
                    event: 0x0102_0304,
                    window: 0xa0b0_c0d0,
                    above_sibling: 0x1122_3344,
                    x: -2,
                    y: 3,
                    width: 640,
                    height: 480,
                    border_width: 5,
                    override_redirect: true,
                },
            ),
            vec![
                22, 0, 0, 9, 1, 2, 3, 4, 0xa0, 0xb0, 0xc0, 0xd0, 0x11, 0x22, 0x33, 0x44, 0xff,
                0xfe, 0, 3, 2, 0x80, 1, 0xe0, 0, 5, 1, 0, 0, 0, 0, 0,
            ]
        );
        assert_eq!(
            encode_property_notify(
                ByteOrder::LittleEndian,
                0x12345,
                PropertyNotifyEvent {
                    window: 0x0102_0304,
                    atom: 0xa0b0_c0d0,
                    time: 0x1122_3344,
                    state: PropertyNotifyState::Deleted,
                },
            ),
            [
                28, 0, 0x45, 0x23, 4, 3, 2, 1, 0xd0, 0xc0, 0xb0, 0xa0, 0x44, 0x33, 0x22, 0x11, 1,
            ]
            .into_iter()
            .chain([0; 15])
            .collect::<Vec<_>>()
        );
        assert_eq!(
            encode_selection_request(
                ByteOrder::BigEndian,
                10,
                SelectionRequestEvent {
                    time: 1,
                    owner: 2,
                    requestor: 3,
                    selection: 4,
                    target: 5,
                    property: 6,
                },
            ),
            [
                30, 0, 0, 10, 0, 0, 0, 1, 0, 0, 0, 2, 0, 0, 0, 3, 0, 0, 0, 4, 0, 0, 0, 5, 0, 0, 0,
                6, 0, 0, 0, 0,
            ]
        );
        let client = encode_client_message(
            ByteOrder::BigEndian,
            12,
            &ClientMessageEvent {
                window: 0x0102_0304,
                message_type: 0xa0b0_c0d0,
                data: ClientMessageData::U16([1, 2, 3, 4, 5, 6, 7, 8, 9, 10]),
                synthetic: true,
            },
        );
        assert_eq!(
            client,
            vec![
                0xa1, 16, 0, 12, 1, 2, 3, 4, 0xa0, 0xb0, 0xc0, 0xd0, 0, 1, 0, 2, 0, 3, 0, 4, 0, 5,
                0, 6, 0, 7, 0, 8, 0, 9, 0, 10,
            ]
        );
    }

    #[test]
    fn input_crossing_focus_and_exposure_match_legacy_vectors() {
        let input = encode_input_event(
            ByteOrder::LittleEndian,
            0x12345,
            InputEvent {
                event_type: CoreEventType::KeyPress,
                detail: 38,
                time: 0x0102_0304,
                root: 1,
                event: 2,
                child: 3,
                root_x: -2,
                root_y: 300,
                event_x: -12,
                event_y: 20,
                state: 0x0506,
                same_screen: true,
            },
        )
        .unwrap();
        assert_eq!(input.len(), 32);
        assert_eq!(&input[..8], &[2, 38, 0x45, 0x23, 4, 3, 2, 1]);
        assert_eq!(&input[20..22], &[0xfe, 0xff]);
        assert_eq!(input[30], 1);

        let motion = encode_input_event(
            ByteOrder::BigEndian,
            9,
            InputEvent {
                event_type: CoreEventType::MotionNotify,
                detail: 99,
                time: 0x0102_0304,
                root: 1,
                event: 2,
                child: 0,
                root_x: -2,
                root_y: 3,
                event_x: -4,
                event_y: 5,
                state: 6,
                same_screen: true,
            },
        )
        .unwrap();
        assert_eq!(&motion[..8], &[6, 0, 0, 9, 1, 2, 3, 4]);
        assert_eq!(&motion[20..22], &[0xff, 0xfe]);

        let crossing = encode_crossing_event(
            ByteOrder::LittleEndian,
            7,
            CrossingEvent {
                event_type: CoreEventType::LeaveNotify,
                detail: NotifyDetail::Nonlinear,
                time: 8,
                root: 1,
                event: 2,
                child: 3,
                root_x: 4,
                root_y: 5,
                event_x: -6,
                event_y: -7,
                state: 0x1234,
                mode: NotifyMode::Normal,
                same_screen: true,
                focus: true,
            },
        )
        .unwrap();
        assert_eq!(crossing[0], 8);
        assert_eq!(crossing[1], 3);
        assert_eq!(&crossing[28..], &[0x34, 0x12, 0, 3]);

        let focus = encode_focus_event(
            ByteOrder::BigEndian,
            0x12345,
            FocusEvent {
                event_type: CoreEventType::FocusOut,
                detail: NotifyDetail::Inferior,
                event: 0x0102_0304,
                mode: NotifyMode::Normal,
            },
        )
        .unwrap();
        assert_eq!(&focus[..9], &[10, 2, 0x23, 0x45, 1, 2, 3, 4, 0]);
        assert!(focus[9..].iter().all(|byte| *byte == 0));

        let expose = encode_expose(
            ByteOrder::LittleEndian,
            0x12345,
            ExposeEvent {
                window: 0x1122_3344,
                x: 1,
                y: 2,
                width: 3,
                height: 4,
                count: 5,
            },
        );
        assert_eq!(expose.len(), 32);
        assert_eq!(&expose[..8], &[12, 0, 0x45, 0x23, 0x44, 0x33, 0x22, 0x11]);
        assert_eq!(
            encode_graphics_expose(
                ByteOrder::LittleEndian,
                7,
                GraphicsExposeEvent {
                    drawable: 9,
                    x: 1,
                    y: 2,
                    width: 3,
                    height: 4,
                    minor_opcode: 0,
                    count: 2,
                    major_opcode: 62,
                },
            )[20],
            62
        );
        assert_eq!(
            encode_no_expose(
                ByteOrder::BigEndian,
                8,
                NoExposeEvent {
                    drawable: 9,
                    minor_opcode: 0,
                    major_opcode: 62,
                },
            )[10],
            62
        );
    }

    #[test]
    fn event_type_categories_fail_closed() {
        let invalid_input = InputEvent {
            event_type: CoreEventType::Expose,
            detail: 0,
            time: 0,
            root: 0,
            event: 0,
            child: 0,
            root_x: 0,
            root_y: 0,
            event_x: 0,
            event_y: 0,
            state: 0,
            same_screen: true,
        };
        assert_eq!(
            encode_input_event(ByteOrder::LittleEndian, 1, invalid_input),
            Err(EventEncodeError::InvalidEventType)
        );
        let invalid_crossing = CrossingEvent {
            event_type: CoreEventType::FocusIn,
            detail: NotifyDetail::Ancestor,
            time: 0,
            root: 0,
            event: 0,
            child: 0,
            root_x: 0,
            root_y: 0,
            event_x: 0,
            event_y: 0,
            state: 0,
            mode: NotifyMode::Normal,
            same_screen: true,
            focus: false,
        };
        assert_eq!(
            encode_crossing_event(ByteOrder::LittleEndian, 1, invalid_crossing),
            Err(EventEncodeError::InvalidEventType)
        );
        let invalid_focus = FocusEvent {
            event_type: CoreEventType::EnterNotify,
            detail: NotifyDetail::Ancestor,
            event: 0,
            mode: NotifyMode::Normal,
        };
        assert_eq!(
            encode_focus_event(ByteOrder::LittleEndian, 1, invalid_focus),
            Err(EventEncodeError::InvalidEventType)
        );
    }
}
