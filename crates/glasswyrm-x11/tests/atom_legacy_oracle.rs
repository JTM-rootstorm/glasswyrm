use glasswyrm_x11::{
    ByteOrder, CoreDispatchState, FramedRequest, InitialCoreDispatch, InternAtomOutcome,
    dispatch_core_request,
};
use std::collections::HashMap;

#[derive(Default)]
struct AtomState {
    ids_by_name: HashMap<Vec<u8>, u32>,
    names_by_id: HashMap<u32, Vec<u8>>,
    next: u32,
}

impl AtomState {
    fn with_next(next: u32) -> Self {
        Self {
            next,
            ..Self::default()
        }
    }
}

impl CoreDispatchState for AtomState {
    fn intern_atom(&mut self, name: &[u8], only_if_exists: bool) -> InternAtomOutcome {
        if let Some(atom) = self.ids_by_name.get(name) {
            return InternAtomOutcome::Success(*atom);
        }
        if only_if_exists {
            return InternAtomOutcome::Success(0);
        }
        let atom = self.next;
        self.next += 1;
        self.ids_by_name.insert(name.to_vec(), atom);
        self.names_by_id.insert(atom, name.to_vec());
        InternAtomOutcome::Success(atom)
    }

    fn atom_name(&self, atom: u32) -> Option<&[u8]> {
        self.names_by_id.get(&atom).map(Vec::as_slice)
    }
}

fn frame(bytes: &[u8]) -> FramedRequest {
    FramedRequest {
        opcode: bytes[0],
        data: bytes[1],
        length_units: u32::from(u16::from_le_bytes([bytes[2], bytes[3]])),
        header_size: 4,
        bytes: bytes.to_vec(),
    }
}

fn packet(dispatch: InitialCoreDispatch) -> Vec<u8> {
    let InitialCoreDispatch::Packet(packet) = dispatch else {
        panic!("atom requests must produce a reply or error");
    };
    packet
}

#[test]
fn little_endian_packets_match_the_frozen_legacy_oracle() {
    // These full packets were frozen after the native glasswyrmd core-request
    // oracle passed. Keep them literal so codec changes cannot move the oracle.
    let mut state = AtomState::with_next(69);
    let intern = frame(&[
        16, 0, 3, 0, 7, 0, 0, 0, b'G', b'W', b'_', b'A', b'T', b'O', b'M', 0,
    ]);
    assert_eq!(
        packet(dispatch_core_request(
            ByteOrder::LittleEndian,
            0x1_2345,
            1,
            &mut state,
            &intern,
        )),
        [
            1, 0, 0x45, 0x23, 0, 0, 0, 0, 69, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0,
        ]
    );

    let get_name = frame(&[17, 0, 2, 0, 69, 0, 0, 0]);
    assert_eq!(
        packet(dispatch_core_request(
            ByteOrder::LittleEndian,
            0x1_2346,
            1,
            &mut state,
            &get_name,
        )),
        [
            1, 0, 0x46, 0x23, 2, 0, 0, 0, 7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, b'G', b'W', b'_', b'A', b'T', b'O', b'M', 0,
        ]
    );
}

#[test]
fn big_endian_errors_match_the_frozen_legacy_oracle() {
    let mut state = AtomState::with_next(69);
    let invalid_data = FramedRequest {
        opcode: 16,
        data: 2,
        length_units: 1,
        header_size: 4,
        bytes: vec![16, 2, 0, 1],
    };
    assert_eq!(
        packet(dispatch_core_request(
            ByteOrder::BigEndian,
            0x1_2347,
            1,
            &mut state,
            &invalid_data,
        )),
        [
            0, 2, 0x23, 0x47, 0, 0, 0, 2, 0, 0, 16, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0,
        ]
    );

    let unknown_atom = FramedRequest {
        opcode: 17,
        data: 0,
        length_units: 2,
        header_size: 4,
        bytes: vec![17, 0, 0, 2, 0x10, 0x20, 0x30, 0x40],
    };
    assert_eq!(
        packet(dispatch_core_request(
            ByteOrder::BigEndian,
            0x1_2348,
            1,
            &mut state,
            &unknown_atom,
        )),
        [
            0, 5, 0x23, 0x48, 0x10, 0x20, 0x30, 0x40, 0, 0, 17, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0,
        ]
    );
}

#[test]
fn validation_order_and_only_if_exists_match_the_legacy_oracle() {
    for order in [ByteOrder::LittleEndian, ByteOrder::BigEndian] {
        let mut state = AtomState::with_next(69);
        let malformed_intern = FramedRequest {
            opcode: 16,
            data: 0,
            length_units: 2,
            header_size: 4,
            bytes: match order {
                ByteOrder::LittleEndian => vec![16, 0, 2, 0, 1, 0, 0, 0],
                ByteOrder::BigEndian => vec![16, 0, 0, 2, 0, 1, 0, 0],
            },
        };
        let bad_length = packet(dispatch_core_request(
            order,
            0xffff,
            1,
            &mut state,
            &malformed_intern,
        ));
        assert_eq!(bad_length[1], 16);
        assert_eq!(&bad_length[2..4], &[0xff, 0xff]);
        assert_eq!(bad_length[10], 16);

        let missing = match order {
            ByteOrder::LittleEndian => frame(&[16, 1, 3, 0, 4, 0, 0, 0, b'N', b'O', b'P', b'E']),
            ByteOrder::BigEndian => FramedRequest {
                opcode: 16,
                data: 1,
                length_units: 3,
                header_size: 4,
                bytes: vec![16, 1, 0, 3, 0, 4, 0, 0, b'N', b'O', b'P', b'E'],
            },
        };
        let reply = packet(dispatch_core_request(
            order, 0x1_0000, 1, &mut state, &missing,
        ));
        assert_eq!(&reply[2..4], &[0, 0]);
        assert_eq!(&reply[8..12], &[0, 0, 0, 0]);
        assert!(state.ids_by_name.is_empty());
    }
}
