use glasswyrm_x11::{
    ByteOrder, CoreClient, CoreDispatchState, FramedRequest, InitialCoreDispatch,
    InternAtomOutcome, WindowCreateOutcome, WindowCreateRequest, WindowDestroyOutcome,
    WindowGeometryReply, WindowTreeReply, dispatch_core_request_for_client,
};
use std::collections::BTreeMap;

#[derive(Clone)]
struct WindowRecord {
    parent: u32,
    geometry: WindowGeometryReply,
    children: Vec<u32>,
}

struct WindowState {
    windows: BTreeMap<u32, WindowRecord>,
}

impl Default for WindowState {
    fn default() -> Self {
        Self {
            windows: BTreeMap::from([(
                1,
                WindowRecord {
                    parent: 0,
                    geometry: WindowGeometryReply {
                        root: 1,
                        depth: 24,
                        x: 0,
                        y: 0,
                        width: 1024,
                        height: 768,
                        border_width: 0,
                    },
                    children: Vec::new(),
                },
            )]),
        }
    }
}

impl CoreDispatchState for WindowState {
    fn intern_atom(&mut self, _name: &[u8], _only_if_exists: bool) -> InternAtomOutcome {
        unreachable!("the window oracle does not dispatch atom requests")
    }

    fn atom_name(&self, _atom: u32) -> Option<&[u8]> {
        None
    }

    fn create_window(
        &mut self,
        _client: CoreClient,
        request: WindowCreateRequest,
    ) -> WindowCreateOutcome {
        if self.windows.contains_key(&request.xid) {
            return WindowCreateOutcome::BadIdChoice;
        }
        if !self.windows.contains_key(&request.parent) {
            return WindowCreateOutcome::BadWindow;
        }
        self.windows.insert(
            request.xid,
            WindowRecord {
                parent: request.parent,
                geometry: WindowGeometryReply {
                    root: 1,
                    depth: request.depth,
                    x: request.x,
                    y: request.y,
                    width: request.width,
                    height: request.height,
                    border_width: request.border_width,
                },
                children: Vec::new(),
            },
        );
        self.windows
            .get_mut(&request.parent)
            .expect("validated parent")
            .children
            .push(request.xid);
        WindowCreateOutcome::Success
    }

    fn destroy_window(&mut self, window: u32) -> WindowDestroyOutcome {
        if window == 1 {
            return WindowDestroyOutcome::RootPreserved;
        }
        let Some(record) = self.windows.remove(&window) else {
            return WindowDestroyOutcome::BadWindow;
        };
        self.windows
            .get_mut(&record.parent)
            .expect("non-root parent remains")
            .children
            .retain(|child| *child != window);
        WindowDestroyOutcome::Success
    }

    fn window_geometry(&self, window: u32) -> Option<WindowGeometryReply> {
        self.windows.get(&window).map(|record| record.geometry)
    }

    fn window_tree(&self, window: u32) -> Option<WindowTreeReply> {
        self.windows.get(&window).map(|record| WindowTreeReply {
            root: 1,
            parent: record.parent,
            children: record.children.clone(),
        })
    }
}

fn frame(order: ByteOrder, mut bytes: Vec<u8>) -> FramedRequest {
    let units = (bytes.len() / 4) as u16;
    bytes[2..4].copy_from_slice(&match order {
        ByteOrder::LittleEndian => units.to_le_bytes(),
        ByteOrder::BigEndian => units.to_be_bytes(),
    });
    FramedRequest {
        opcode: bytes[0],
        data: bytes[1],
        length_units: u32::from(units),
        header_size: 4,
        bytes,
    }
}

fn u16_bytes(order: ByteOrder, value: u16) -> [u8; 2] {
    match order {
        ByteOrder::LittleEndian => value.to_le_bytes(),
        ByteOrder::BigEndian => value.to_be_bytes(),
    }
}

fn u32_bytes(order: ByteOrder, value: u32) -> [u8; 4] {
    match order {
        ByteOrder::LittleEndian => value.to_le_bytes(),
        ByteOrder::BigEndian => value.to_be_bytes(),
    }
}

fn create_request(order: ByteOrder) -> FramedRequest {
    let mut bytes = vec![1, 24, 0, 0];
    bytes.extend_from_slice(&u32_bytes(order, 0x0040_0001));
    bytes.extend_from_slice(&u32_bytes(order, 1));
    bytes.extend_from_slice(&u16_bytes(order, (-5_i16) as u16));
    bytes.extend_from_slice(&u16_bytes(order, 7));
    bytes.extend_from_slice(&u16_bytes(order, 320));
    bytes.extend_from_slice(&u16_bytes(order, 200));
    bytes.extend_from_slice(&u16_bytes(order, 2));
    bytes.extend_from_slice(&u16_bytes(order, 1));
    bytes.extend_from_slice(&u32_bytes(order, 3));
    bytes.extend_from_slice(&u32_bytes(order, 0));
    frame(order, bytes)
}

fn window_request(order: ByteOrder, opcode: u8, window: u32) -> FramedRequest {
    let mut bytes = vec![opcode, 0, 0, 0];
    bytes.extend_from_slice(&u32_bytes(order, window));
    frame(order, bytes)
}

fn replace_u16(order: ByteOrder, request: &mut FramedRequest, offset: usize, value: u16) {
    request.bytes[offset..offset + 2].copy_from_slice(&u16_bytes(order, value));
}

fn replace_u32(order: ByteOrder, request: &mut FramedRequest, offset: usize, value: u32) {
    request.bytes[offset..offset + 4].copy_from_slice(&u32_bytes(order, value));
}

fn packet(dispatch: InitialCoreDispatch) -> Vec<u8> {
    let InitialCoreDispatch::Packet(packet) = dispatch else {
        panic!("query and error requests must produce a packet");
    };
    packet
}

#[test]
fn little_endian_window_packets_match_the_frozen_legacy_oracle() {
    // These full packets were frozen from the accepted native core-request
    // oracle before the Rust window dispatcher was implemented.
    let mut state = WindowState::default();
    let client = CoreClient::new(7, 0x0040_0000, 0x001f_ffff);
    assert_eq!(
        dispatch_core_request_for_client(
            ByteOrder::LittleEndian,
            0x1234,
            1,
            client,
            &mut state,
            &create_request(ByteOrder::LittleEndian),
        ),
        InitialCoreDispatch::NoReply
    );

    assert_eq!(
        packet(dispatch_core_request_for_client(
            ByteOrder::LittleEndian,
            0x1235,
            1,
            client,
            &mut state,
            &window_request(ByteOrder::LittleEndian, 14, 0x0040_0001),
        )),
        [
            1, 24, 0x35, 0x12, 0, 0, 0, 0, 1, 0, 0, 0, 0xfb, 0xff, 7, 0, 0x40, 1, 0xc8, 0, 2, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0,
        ]
    );
    assert_eq!(
        packet(dispatch_core_request_for_client(
            ByteOrder::LittleEndian,
            0x1236,
            1,
            client,
            &mut state,
            &window_request(ByteOrder::LittleEndian, 15, 1),
        )),
        [
            1, 0, 0x36, 0x12, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 1, 0, 0x40, 0,
        ]
    );
    assert_eq!(
        dispatch_core_request_for_client(
            ByteOrder::LittleEndian,
            0x1237,
            1,
            client,
            &mut state,
            &window_request(ByteOrder::LittleEndian, 4, 0x0040_0001),
        ),
        InitialCoreDispatch::NoReply
    );
    assert_eq!(
        packet(dispatch_core_request_for_client(
            ByteOrder::LittleEndian,
            0x1238,
            1,
            client,
            &mut state,
            &window_request(ByteOrder::LittleEndian, 15, 1),
        )),
        [
            1, 0, 0x38, 0x12, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0,
        ]
    );
    assert_eq!(
        packet(dispatch_core_request_for_client(
            ByteOrder::LittleEndian,
            0x1239,
            1,
            client,
            &mut state,
            &window_request(ByteOrder::LittleEndian, 8, 1),
        )),
        [
            0, 1, 0x39, 0x12, 0, 0, 0, 0, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0,
        ]
    );
}

#[test]
fn big_endian_window_packets_and_errors_match_the_frozen_legacy_oracle() {
    let mut state = WindowState::default();
    let client = CoreClient::new(8, 0x0040_0000, 0x001f_ffff);
    assert_eq!(
        dispatch_core_request_for_client(
            ByteOrder::BigEndian,
            0x1234,
            1,
            client,
            &mut state,
            &create_request(ByteOrder::BigEndian),
        ),
        InitialCoreDispatch::NoReply
    );
    assert_eq!(
        packet(dispatch_core_request_for_client(
            ByteOrder::BigEndian,
            0x1235,
            1,
            client,
            &mut state,
            &window_request(ByteOrder::BigEndian, 14, 0x0040_0001),
        )),
        [
            1, 24, 0x12, 0x35, 0, 0, 0, 0, 0, 0, 0, 1, 0xff, 0xfb, 0, 7, 1, 0x40, 0, 0xc8, 0, 2, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0,
        ]
    );

    assert_eq!(
        packet(dispatch_core_request_for_client(
            ByteOrder::BigEndian,
            0x1236,
            1,
            client,
            &mut state,
            &create_request(ByteOrder::BigEndian),
        )),
        [
            0, 14, 0x12, 0x36, 0, 0x40, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0,
        ]
    );
    assert_eq!(
        packet(dispatch_core_request_for_client(
            ByteOrder::BigEndian,
            0x1237,
            1,
            client,
            &mut state,
            &window_request(ByteOrder::BigEndian, 15, 0x1020_3040),
        )),
        [
            0, 3, 0x12, 0x37, 0x10, 0x20, 0x30, 0x40, 0, 0, 15, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0,
        ]
    );
    assert_eq!(
        packet(dispatch_core_request_for_client(
            ByteOrder::BigEndian,
            0x1238,
            1,
            client,
            &mut state,
            &window_request(ByteOrder::BigEndian, 14, 0x1020_3040),
        )),
        [
            0, 9, 0x12, 0x38, 0x10, 0x20, 0x30, 0x40, 0, 0, 14, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0,
        ]
    );
}

#[test]
fn create_validation_order_matches_the_frozen_legacy_oracle() {
    for order in [ByteOrder::LittleEndian, ByteOrder::BigEndian] {
        let client = CoreClient::new(9, 0x0040_0000, 0x001f_ffff);
        let mut state = WindowState::default();

        let mut invalid_class = create_request(order);
        replace_u16(order, &mut invalid_class, 22, 3);
        let error = packet(dispatch_core_request_for_client(
            order,
            1,
            1,
            client,
            &mut state,
            &invalid_class,
        ));
        assert_eq!(error[1], 2);
        assert_eq!(&error[4..8], &u32_bytes(order, 3));

        let mut invalid_mask = create_request(order);
        replace_u32(order, &mut invalid_mask, 28, 0x8000_0000);
        let error = packet(dispatch_core_request_for_client(
            order,
            2,
            1,
            client,
            &mut state,
            &invalid_mask,
        ));
        assert_eq!(error[1], 2);
        assert_eq!(&error[4..8], &u32_bytes(order, 0x8000_0000));

        let mut invalid_event = create_request(order);
        replace_u32(order, &mut invalid_event, 28, 1 << 11);
        invalid_event
            .bytes
            .extend_from_slice(&u32_bytes(order, 0x8000_0000));
        invalid_event.length_units = 9;
        replace_u16(order, &mut invalid_event, 2, 9);
        let error = packet(dispatch_core_request_for_client(
            order,
            3,
            1,
            client,
            &mut state,
            &invalid_event,
        ));
        assert_eq!(error[1], 2);
        assert_eq!(&error[4..8], &u32_bytes(order, 0x8000_0000));
        assert_eq!(state.windows.len(), 1);
    }
}
