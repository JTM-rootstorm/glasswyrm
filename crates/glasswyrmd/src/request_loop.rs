use glasswyrm_core::atom::{AtomTable, InternAtomStatus};
use glasswyrm_core::property::{
    AtomId, Property, PropertyData as CorePropertyData, PropertyLimits,
    PropertyMode as CorePropertyMode, PropertyMutationStatus, PropertyReadStatus,
};
use glasswyrm_core::resource_id::{ClientResourceRange, ResourceBase, ResourceMask};
use glasswyrm_core::window::{
    ClientId, CreateWindowStatus, DestroyWindowStatus, ScreenModel, WindowAttributes, WindowClass,
    WindowCreateSpec, WindowGeometry, WindowId, WindowStore,
};
use glasswyrm_x11::{
    ByteOrder, CoreClient, CoreDispatchState, CoreError, CoreErrorCode, ExtensionAssignment,
    InitialCoreDispatch, InternAtomOutcome, PropertyChangeRequest, PropertyData as X11PropertyData,
    PropertyMode, PropertyMutationOutcome, PropertyReadOutcome, PropertyReadReply,
    PropertyReadRequest, RequestFrameStatus, RequestFramer, SCREEN_MODEL, WindowCreateOutcome,
    WindowCreateRequest, WindowDestroyOutcome, WindowGeometryReply, WindowTreeReply,
    dispatch_core_request_for_client, encode_core_error,
};
use std::collections::VecDeque;
use std::io::{self, Write};
use std::os::unix::net::UnixStream;
use std::sync::{Arc, Mutex};

pub(crate) const MAXIMUM_REQUESTS_PER_TURN: usize = 64;
pub(crate) const MAXIMUM_REQUEST_BYTES_PER_TURN: usize = 256 * 1024;
pub(crate) const MAXIMUM_QUEUED_OUTPUT: usize = 1024 * 1024;

#[derive(Debug)]
pub(crate) struct ServerState {
    atoms: AtomTable,
    windows: WindowStore,
}

impl Default for ServerState {
    fn default() -> Self {
        Self {
            atoms: AtomTable::default(),
            windows: WindowStore::new(
                ScreenModel {
                    root_window: WindowId::new(SCREEN_MODEL.root_window),
                    root_width: SCREEN_MODEL.width_pixels,
                    root_height: SCREEN_MODEL.height_pixels,
                    root_depth: SCREEN_MODEL.root_depth,
                    root_visual: SCREEN_MODEL.root_visual,
                },
                PropertyLimits::default(),
            ),
        }
    }
}

impl ServerState {
    fn cleanup_client(&mut self, client: CoreClient) {
        let _ = self
            .windows
            .destroy_all_owned(ClientId::new(client.identifier));
    }
}

impl CoreDispatchState for ServerState {
    fn intern_atom(&mut self, name: &[u8], only_if_exists: bool) -> InternAtomOutcome {
        let result = self.atoms.intern(name, only_if_exists);
        match result.status {
            InternAtomStatus::Success => InternAtomOutcome::Success(result.atom),
            InternAtomStatus::Exhausted => InternAtomOutcome::Exhausted,
        }
    }

    fn atom_name(&self, atom: u32) -> Option<&[u8]> {
        self.atoms.name(atom)
    }

    fn query_extension(&self, _name: &[u8]) -> Option<ExtensionAssignment> {
        None
    }

    fn enabled_extension_names(&self) -> Vec<&'static [u8]> {
        Vec::new()
    }

    fn create_window(
        &mut self,
        client: CoreClient,
        request: WindowCreateRequest,
    ) -> WindowCreateOutcome {
        let window_class = match request.window_class {
            0 => WindowClass::CopyFromParent,
            1 => WindowClass::InputOutput,
            2 => WindowClass::InputOnly,
            _ => return WindowCreateOutcome::BadValue,
        };
        let status = self.windows.create_window(
            ClientId::new(client.identifier),
            ClientResourceRange::new(
                ResourceBase::new(client.resource_base),
                ResourceMask::new(client.resource_mask),
            ),
            WindowCreateSpec {
                xid: WindowId::new(request.xid),
                parent: WindowId::new(request.parent),
                geometry: WindowGeometry {
                    x: request.x,
                    y: request.y,
                    width: request.width,
                    height: request.height,
                    border_width: request.border_width,
                },
                depth: request.depth,
                window_class,
                visual: request.visual,
                attribute_mask: request.attribute_mask,
                attributes: WindowAttributes {
                    override_redirect: request.override_redirect,
                },
            },
        );
        match status {
            CreateWindowStatus::Success => WindowCreateOutcome::Success,
            CreateWindowStatus::BadIdChoice => WindowCreateOutcome::BadIdChoice,
            CreateWindowStatus::BadWindow => WindowCreateOutcome::BadWindow,
            CreateWindowStatus::BadValue => WindowCreateOutcome::BadValue,
            CreateWindowStatus::BadMatch => WindowCreateOutcome::BadMatch,
            CreateWindowStatus::BadAlloc => WindowCreateOutcome::BadAlloc,
        }
    }

    fn destroy_window(&mut self, window: u32) -> WindowDestroyOutcome {
        match self.windows.destroy_window(WindowId::new(window)).status {
            DestroyWindowStatus::Success => WindowDestroyOutcome::Success,
            DestroyWindowStatus::BadWindow => WindowDestroyOutcome::BadWindow,
            DestroyWindowStatus::RootPreserved => WindowDestroyOutcome::RootPreserved,
        }
    }

    fn window_geometry(&self, window: u32) -> Option<WindowGeometryReply> {
        self.windows.window(WindowId::new(window)).map(|window| {
            let geometry = window.geometry();
            WindowGeometryReply {
                root: self.windows.screen().root_window.get(),
                depth: window.depth(),
                x: geometry.x,
                y: geometry.y,
                width: geometry.width,
                height: geometry.height,
                border_width: geometry.border_width,
            }
        })
    }

    fn window_tree(&self, window: u32) -> Option<WindowTreeReply> {
        self.windows
            .window(WindowId::new(window))
            .map(|window| WindowTreeReply {
                root: self.windows.screen().root_window.get(),
                parent: window.parent().map_or(0, WindowId::get),
                children: window.children().iter().map(|child| child.get()).collect(),
            })
    }

    fn window_exists(&self, window: u32) -> bool {
        self.windows.window(WindowId::new(window)).is_some()
    }

    fn atom_is_valid(&self, atom: u32, allow_none: bool) -> bool {
        self.atoms.valid(atom, allow_none)
    }

    fn change_property(&mut self, request: PropertyChangeRequest) -> PropertyMutationOutcome {
        let data = match request.data {
            X11PropertyData::U8(values) => CorePropertyData::U8(values),
            X11PropertyData::U16(values) => CorePropertyData::U16(values),
            X11PropertyData::U32(values) => CorePropertyData::U32(values),
        };
        let mode = match request.mode {
            PropertyMode::Replace => CorePropertyMode::Replace,
            PropertyMode::Prepend => CorePropertyMode::Prepend,
            PropertyMode::Append => CorePropertyMode::Append,
        };
        match self.windows.change_property(
            WindowId::new(request.window),
            AtomId::new(request.property),
            Property {
                property_type: AtomId::new(request.property_type),
                data,
            },
            mode,
        ) {
            PropertyMutationStatus::Success => PropertyMutationOutcome::Success,
            PropertyMutationStatus::BadMatch => PropertyMutationOutcome::BadMatch,
            PropertyMutationStatus::BadAlloc => PropertyMutationOutcome::BadAlloc,
            PropertyMutationStatus::BadWindow => PropertyMutationOutcome::Unsupported,
        }
    }

    fn delete_property(&mut self, window: u32, property: u32) -> PropertyMutationOutcome {
        if self
            .windows
            .delete_property(WindowId::new(window), AtomId::new(property))
        {
            PropertyMutationOutcome::Success
        } else {
            PropertyMutationOutcome::Unsupported
        }
    }

    fn get_property(&mut self, request: PropertyReadRequest) -> PropertyReadOutcome {
        let result = self.windows.get_property(
            WindowId::new(request.window),
            AtomId::new(request.property),
            request.requested_type.map(AtomId::new),
            request.delete_after_read,
            request.long_offset,
            request.long_length,
        );
        match result.status {
            PropertyReadStatus::BadValue => PropertyReadOutcome::BadValue,
            PropertyReadStatus::BadWindow => PropertyReadOutcome::Unsupported,
            PropertyReadStatus::Success => {
                let (property_type, bytes_after, data) = result.value.map_or_else(
                    || (0, 0, X11PropertyData::U8(Vec::new())),
                    |value| {
                        let data = match value.data {
                            CorePropertyData::U8(values) => X11PropertyData::U8(values),
                            CorePropertyData::U16(values) => X11PropertyData::U16(values),
                            CorePropertyData::U32(values) => X11PropertyData::U32(values),
                        };
                        (value.property_type.get(), value.bytes_after, data)
                    },
                );
                PropertyReadOutcome::Success(PropertyReadReply {
                    present: result.present,
                    type_matched: result.type_matched,
                    deleted: result.deleted,
                    property_type,
                    bytes_after,
                    data,
                })
            }
        }
    }

    fn list_properties(&self, window: u32) -> Option<Vec<u32>> {
        self.window_exists(window).then(|| {
            self.windows
                .list_properties(WindowId::new(window))
                .into_iter()
                .map(AtomId::get)
                .collect()
        })
    }
}

#[derive(Debug, Default)]
pub(crate) struct RequestWorkBudget {
    requests: usize,
    bytes: usize,
}

impl RequestWorkBudget {
    pub(crate) fn available(&self) -> bool {
        self.requests < MAXIMUM_REQUESTS_PER_TURN && self.bytes < MAXIMUM_REQUEST_BYTES_PER_TURN
    }

    fn record(&mut self, bytes: usize) {
        self.requests += 1;
        self.bytes += bytes;
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
enum SessionState {
    Established,
    DrainThenClose,
    Closed,
}

#[derive(Debug)]
struct OutputPacket {
    bytes: Vec<u8>,
    offset: usize,
}

#[derive(Debug, Default)]
struct OutputQueue {
    packets: VecDeque<OutputPacket>,
    queued_bytes: usize,
}

impl OutputQueue {
    fn enqueue(&mut self, bytes: Vec<u8>) -> bool {
        if bytes.len() > MAXIMUM_QUEUED_OUTPUT - self.queued_bytes {
            return false;
        }
        self.queued_bytes += bytes.len();
        self.packets.push_back(OutputPacket { bytes, offset: 0 });
        true
    }

    fn write_to(&mut self, stream: &mut UnixStream) -> io::Result<bool> {
        let mut wrote_any = false;
        while let Some(packet) = self.packets.front_mut() {
            match stream.write(&packet.bytes[packet.offset..]) {
                Ok(0) => {
                    return Err(io::Error::new(
                        io::ErrorKind::WriteZero,
                        "zero-length X11 socket write",
                    ));
                }
                Ok(count) => {
                    packet.offset += count;
                    self.queued_bytes -= count;
                    wrote_any = true;
                    if packet.offset == packet.bytes.len() {
                        self.packets.pop_front();
                    }
                }
                Err(error) if error.kind() == io::ErrorKind::Interrupted => {}
                Err(error) if error.kind() == io::ErrorKind::WouldBlock => return Ok(wrote_any),
                Err(error) => return Err(error),
            }
        }
        Ok(wrote_any)
    }

    fn clear(&mut self) {
        self.packets.clear();
        self.queued_bytes = 0;
    }

    fn is_empty(&self) -> bool {
        self.packets.is_empty()
    }
}

pub(crate) struct RequestLoop {
    order: ByteOrder,
    client: CoreClient,
    focused_window: u32,
    framer: RequestFramer,
    request_sequence: u64,
    pending_input: Vec<u8>,
    output: OutputQueue,
    state: SessionState,
    server_state: Arc<Mutex<ServerState>>,
}

impl RequestLoop {
    #[cfg(test)]
    pub(crate) fn new(
        order: ByteOrder,
        maximum_request_length: u16,
        focused_window: u32,
        setup_reply: Vec<u8>,
        server_state: Arc<Mutex<ServerState>>,
    ) -> Self {
        Self::new_for_client(
            order,
            maximum_request_length,
            focused_window,
            setup_reply,
            CoreClient::new(1, 0x0040_0000, SCREEN_MODEL.resource_id_mask),
            server_state,
        )
    }

    pub(crate) fn new_for_client(
        order: ByteOrder,
        maximum_request_length: u16,
        focused_window: u32,
        setup_reply: Vec<u8>,
        client: CoreClient,
        server_state: Arc<Mutex<ServerState>>,
    ) -> Self {
        let mut output = OutputQueue::default();
        if !setup_reply.is_empty() {
            let enqueued = output.enqueue(setup_reply);
            debug_assert!(enqueued, "the bounded setup reply fits the output queue");
        }
        Self {
            order,
            client,
            focused_window,
            framer: RequestFramer::new(order, maximum_request_length)
                .expect("the advertised setup request limit is nonzero"),
            request_sequence: 0,
            pending_input: Vec::new(),
            output,
            state: SessionState::Established,
            server_state,
        }
    }

    pub(crate) fn accepts_input(&self) -> bool {
        self.state == SessionState::Established
    }

    pub(crate) fn is_closed(&self) -> bool {
        self.state == SessionState::Closed
    }

    pub(crate) fn has_pending_input(&self) -> bool {
        !self.pending_input.is_empty()
    }

    pub(crate) fn feed(&mut self, input: &[u8], budget: &mut RequestWorkBudget) -> usize {
        if !self.accepts_input() {
            return 0;
        }
        self.pending_input.extend_from_slice(input);
        self.process_pending(budget)
    }

    pub(crate) fn process_pending(&mut self, budget: &mut RequestWorkBudget) -> usize {
        if !self.accepts_input() {
            return 0;
        }
        let mut consumed = 0;
        let mut completed = 0;
        while consumed < self.pending_input.len() && budget.available() && self.accepts_input() {
            let result = self.framer.consume(&self.pending_input[consumed..]);
            consumed += result.consumed;
            match result.status {
                RequestFrameStatus::NeedMore => break,
                RequestFrameStatus::Complete => {
                    self.request_sequence = self.request_sequence.wrapping_add(1);
                    let request_size = self.framer.request().bytes.len();
                    budget.record(request_size);
                    completed += 1;
                    let packet = {
                        let mut state = self
                            .server_state
                            .lock()
                            .unwrap_or_else(|poisoned| poisoned.into_inner());
                        dispatch_core_request_for_client(
                            self.order,
                            self.request_sequence,
                            self.focused_window,
                            self.client,
                            &mut *state,
                            self.framer.request(),
                        )
                    };
                    if let InitialCoreDispatch::Packet(bytes) = packet
                        && !self.output.enqueue(bytes)
                    {
                        self.close_now();
                        break;
                    }
                    self.framer.reset();
                }
                RequestFrameStatus::ZeroLength | RequestFrameStatus::TooLarge => {
                    self.reject_bad_length();
                    break;
                }
                RequestFrameStatus::TruncatedInput => {
                    self.close_now();
                    break;
                }
            }
        }

        if self.state == SessionState::Established {
            self.pending_input.drain(..consumed);
        } else {
            self.pending_input.clear();
        }
        completed
    }

    pub(crate) fn end_of_input(&mut self) {
        if !self.accepts_input() {
            return;
        }
        if self.framer.eof() == RequestFrameStatus::TruncatedInput || !self.pending_input.is_empty()
        {
            self.close_now();
        } else {
            self.state = SessionState::DrainThenClose;
            self.finish_draining();
        }
    }

    pub(crate) fn write_output(&mut self, stream: &mut UnixStream) -> io::Result<bool> {
        let wrote = self.output.write_to(stream)?;
        self.finish_draining();
        Ok(wrote)
    }

    pub(crate) fn close_now(&mut self) {
        self.state = SessionState::Closed;
        self.pending_input.clear();
        self.output.clear();
    }

    fn reject_bad_length(&mut self) {
        self.request_sequence = self.request_sequence.wrapping_add(1);
        let opcode = self.framer.request().opcode;
        let error = encode_core_error(
            self.order,
            CoreError {
                code: CoreErrorCode::BadLength,
                sequence: self.request_sequence,
                bad_value: 0,
                major_opcode: opcode,
                minor_opcode: 0,
            },
        );
        if self.output.enqueue(error) {
            self.state = SessionState::DrainThenClose;
        } else {
            self.close_now();
        }
    }

    fn finish_draining(&mut self) {
        if self.state == SessionState::DrainThenClose && self.output.is_empty() {
            self.state = SessionState::Closed;
        }
    }
}

impl Drop for RequestLoop {
    fn drop(&mut self) {
        self.server_state
            .lock()
            .unwrap_or_else(|poisoned| poisoned.into_inner())
            .cleanup_client(self.client);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use glasswyrm_x11::CoreOpcode;

    fn request(order: ByteOrder, opcode: u8, units: u16) -> Vec<u8> {
        let mut bytes = vec![opcode, 0];
        let encoded_units = match order {
            ByteOrder::LittleEndian => units.to_le_bytes(),
            ByteOrder::BigEndian => units.to_be_bytes(),
        };
        bytes.extend_from_slice(&encoded_units);
        bytes.resize(usize::from(units) * 4, 0);
        bytes
    }

    fn intern_atom_request(order: ByteOrder, only_if_exists: bool, name: &[u8]) -> Vec<u8> {
        let padded_name_length = (name.len() + 3) & !3;
        let mut bytes = vec![CoreOpcode::InternAtom as u8, u8::from(only_if_exists), 0, 0];
        bytes.extend_from_slice(&match order {
            ByteOrder::LittleEndian => (name.len() as u16).to_le_bytes(),
            ByteOrder::BigEndian => (name.len() as u16).to_be_bytes(),
        });
        bytes.extend_from_slice(&[0, 0]);
        bytes.extend_from_slice(name);
        bytes.resize(8 + padded_name_length, 0);
        let units = (bytes.len() / 4) as u16;
        bytes[2..4].copy_from_slice(&match order {
            ByteOrder::LittleEndian => units.to_le_bytes(),
            ByteOrder::BigEndian => units.to_be_bytes(),
        });
        bytes
    }

    fn get_atom_name_request(order: ByteOrder, atom: u32) -> Vec<u8> {
        let mut bytes = request(order, CoreOpcode::GetAtomName as u8, 2);
        bytes[4..8].copy_from_slice(&match order {
            ByteOrder::LittleEndian => atom.to_le_bytes(),
            ByteOrder::BigEndian => atom.to_be_bytes(),
        });
        bytes
    }

    fn session(order: ByteOrder) -> RequestLoop {
        RequestLoop::new(
            order,
            u16::MAX,
            1,
            Vec::new(),
            Arc::new(Mutex::new(ServerState::default())),
        )
    }

    fn packets(loop_: &RequestLoop) -> Vec<&[u8]> {
        loop_
            .output
            .packets
            .iter()
            .map(|packet| packet.bytes.as_slice())
            .collect()
    }

    fn sequence(order: ByteOrder, packet: &[u8]) -> u16 {
        match order {
            ByteOrder::LittleEndian => u16::from_le_bytes([packet[2], packet[3]]),
            ByteOrder::BigEndian => u16::from_be_bytes([packet[2], packet[3]]),
        }
    }

    fn u32_at(order: ByteOrder, packet: &[u8], offset: usize) -> u32 {
        match order {
            ByteOrder::LittleEndian => u32::from_le_bytes(
                packet[offset..offset + 4]
                    .try_into()
                    .expect("four-byte field"),
            ),
            ByteOrder::BigEndian => u32::from_be_bytes(
                packet[offset..offset + 4]
                    .try_into()
                    .expect("four-byte field"),
            ),
        }
    }

    #[test]
    fn atom_state_is_shared_across_clients_and_preserves_wire_bytes() {
        let shared = Arc::new(Mutex::new(ServerState::default()));
        let mut little = RequestLoop::new(
            ByteOrder::LittleEndian,
            u16::MAX,
            1,
            Vec::new(),
            Arc::clone(&shared),
        );
        let mut big = RequestLoop::new(ByteOrder::BigEndian, u16::MAX, 1, Vec::new(), shared);
        let name = b"GW_\xff_ATOM";

        assert_eq!(
            little.feed(
                &intern_atom_request(ByteOrder::LittleEndian, false, name),
                &mut RequestWorkBudget::default(),
            ),
            1
        );
        assert_eq!(u32_at(ByteOrder::LittleEndian, packets(&little)[0], 8), 69);

        assert_eq!(
            big.feed(
                &intern_atom_request(ByteOrder::BigEndian, true, name),
                &mut RequestWorkBudget::default(),
            ),
            1
        );
        assert_eq!(u32_at(ByteOrder::BigEndian, packets(&big)[0], 8), 69);

        assert_eq!(
            big.feed(
                &get_atom_name_request(ByteOrder::BigEndian, 69),
                &mut RequestWorkBudget::default(),
            ),
            1
        );
        let reply = packets(&big)[1];
        assert_eq!(&reply[32..32 + name.len()], name);
    }

    #[test]
    fn atom_exhaustion_is_reported_as_bad_alloc_without_mutation() {
        let state = ServerState {
            atoms: AtomTable::new(68),
            ..ServerState::default()
        };
        let mut loop_ = RequestLoop::new(
            ByteOrder::LittleEndian,
            u16::MAX,
            1,
            Vec::new(),
            Arc::new(Mutex::new(state)),
        );
        let request = intern_atom_request(ByteOrder::LittleEndian, false, b"too-many");
        assert_eq!(loop_.feed(&request, &mut RequestWorkBudget::default()), 1);
        assert_eq!(packets(&loop_)[0][1], CoreErrorCode::BadAlloc as u8);

        let only_if_exists = intern_atom_request(ByteOrder::LittleEndian, true, b"too-many");
        assert_eq!(
            loop_.feed(&only_if_exists, &mut RequestWorkBudget::default()),
            1
        );
        assert_eq!(u32_at(ByteOrder::LittleEndian, packets(&loop_)[1], 8), 0);
    }

    #[test]
    fn fragmented_pipeline_dispatches_in_order_for_both_byte_orders() {
        for order in [ByteOrder::LittleEndian, ByteOrder::BigEndian] {
            let mut bytes = request(order, CoreOpcode::NoOperation as u8, 1);
            bytes.extend_from_slice(&request(order, 250, 1));
            bytes.extend_from_slice(&request(order, CoreOpcode::GetInputFocus as u8, 1));
            let mut loop_ = session(order);
            let mut budget = RequestWorkBudget::default();
            assert_eq!(loop_.feed(&bytes[..3], &mut budget), 0);
            assert_eq!(loop_.feed(&bytes[3..], &mut budget), 3);
            assert_eq!(loop_.request_sequence, 3);
            let packets = packets(&loop_);
            assert_eq!(packets.len(), 2);
            assert_eq!(packets[0][1], CoreErrorCode::BadRequest as u8);
            assert_eq!(sequence(order, packets[0]), 2);
            assert_eq!(packets[1][0], 1);
            assert_eq!(sequence(order, packets[1]), 3);
        }
    }

    #[test]
    fn request_sequence_wraps_only_on_the_wire() {
        for order in [ByteOrder::LittleEndian, ByteOrder::BigEndian] {
            let mut loop_ = session(order);
            loop_.request_sequence = u64::from(u16::MAX) - 1;
            let mut bytes = request(order, 250, 1);
            bytes.extend_from_slice(&request(order, CoreOpcode::GetInputFocus as u8, 1));
            assert_eq!(loop_.feed(&bytes, &mut RequestWorkBudget::default()), 2);
            let packets = packets(&loop_);
            assert_eq!(sequence(order, packets[0]), u16::MAX);
            assert_eq!(sequence(order, packets[1]), 0);
            assert_eq!(loop_.request_sequence, u64::from(u16::MAX) + 1);
        }
    }

    #[test]
    fn per_turn_request_and_byte_budgets_preserve_pending_work() {
        let order = ByteOrder::LittleEndian;
        let mut loop_ = session(order);
        let mut pipeline = Vec::new();
        for _ in 0..=MAXIMUM_REQUESTS_PER_TURN {
            pipeline.extend_from_slice(&request(order, CoreOpcode::NoOperation as u8, 1));
        }
        let mut first = RequestWorkBudget::default();
        assert_eq!(loop_.feed(&pipeline, &mut first), MAXIMUM_REQUESTS_PER_TURN);
        assert!(!first.available());
        assert!(loop_.has_pending_input());
        assert_eq!(loop_.process_pending(&mut RequestWorkBudget::default()), 1);

        let mut loop_ = session(order);
        let mut large = request(order, CoreOpcode::NoOperation as u8, u16::MAX);
        large.extend_from_slice(&request(order, CoreOpcode::NoOperation as u8, 1));
        large.extend_from_slice(&request(order, CoreOpcode::NoOperation as u8, 1));
        let mut bytes = RequestWorkBudget::default();
        assert_eq!(loop_.feed(&large, &mut bytes), 2);
        assert_eq!(bytes.bytes, MAXIMUM_REQUEST_BYTES_PER_TURN);
        assert!(loop_.has_pending_input());
    }

    #[test]
    fn malformed_framing_sends_bad_length_then_closes() {
        let order = ByteOrder::LittleEndian;
        let mut zero = session(order);
        assert_eq!(
            zero.feed(&[43, 0, 0, 0], &mut RequestWorkBudget::default()),
            0
        );
        assert_eq!(zero.state, SessionState::DrainThenClose);
        let zero_packets = packets(&zero);
        assert_eq!(zero_packets.len(), 1);
        assert_eq!(zero_packets[0][1], CoreErrorCode::BadLength as u8);
        assert_eq!(zero_packets[0][10], CoreOpcode::GetInputFocus as u8);

        let mut oversized = RequestLoop::new(
            order,
            2,
            1,
            Vec::new(),
            Arc::new(Mutex::new(ServerState::default())),
        );
        assert_eq!(
            oversized.feed(
                &request(order, CoreOpcode::NoOperation as u8, 3),
                &mut RequestWorkBudget::default(),
            ),
            0
        );
        assert_eq!(oversized.state, SessionState::DrainThenClose);
        assert_eq!(packets(&oversized)[0][1], CoreErrorCode::BadLength as u8);
    }

    #[test]
    fn complete_bad_length_is_recoverable_and_partial_eof_is_clean() {
        let order = ByteOrder::LittleEndian;
        let mut loop_ = session(order);
        let mut bytes = request(order, CoreOpcode::GetInputFocus as u8, 2);
        bytes.extend_from_slice(&request(order, CoreOpcode::GetInputFocus as u8, 1));
        assert_eq!(loop_.feed(&bytes, &mut RequestWorkBudget::default()), 2);
        let packets = packets(&loop_);
        assert_eq!(packets[0][1], CoreErrorCode::BadLength as u8);
        assert_eq!(packets[1][0], 1);
        assert!(loop_.accepts_input());

        let mut truncated = session(order);
        assert_eq!(
            truncated.feed(&[43, 0, 1], &mut RequestWorkBudget::default()),
            0
        );
        truncated.end_of_input();
        assert!(truncated.is_closed());
        assert_eq!(truncated.output.queued_bytes, 0);
    }

    #[test]
    fn output_cap_fails_closed_without_retaining_queued_data() {
        let order = ByteOrder::LittleEndian;
        let mut loop_ = session(order);
        let unsupported = request(order, 250, 1);
        for _ in 0..=(MAXIMUM_QUEUED_OUTPUT / 32) {
            let mut budget = RequestWorkBudget::default();
            for _ in 0..MAXIMUM_REQUESTS_PER_TURN {
                if loop_.is_closed() {
                    break;
                }
                loop_.feed(&unsupported, &mut budget);
            }
            if loop_.is_closed() {
                break;
            }
        }
        assert!(loop_.is_closed());
        assert_eq!(loop_.output.queued_bytes, 0);
        assert!(loop_.output.is_empty());
    }
}
