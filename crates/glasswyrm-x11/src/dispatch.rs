use crate::{
    ByteOrder, CoreError, CoreErrorCode, CoreOpcode, FramedRequest, ReplyBuilder, encode_core_error,
};

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum InitialCoreDispatch {
    NoReply,
    Packet(Vec<u8>),
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum InternAtomOutcome {
    Success(u32),
    Exhausted,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct CoreClient {
    pub identifier: u64,
    pub resource_base: u32,
    pub resource_mask: u32,
}

impl CoreClient {
    #[must_use]
    pub const fn new(identifier: u64, resource_base: u32, resource_mask: u32) -> Self {
        Self {
            identifier,
            resource_base,
            resource_mask,
        }
    }
}

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
pub enum WindowCreateOutcome {
    Success,
    BadIdChoice,
    BadWindow,
    BadValue,
    BadMatch,
    BadAlloc,
    Unsupported,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum WindowDestroyOutcome {
    Success,
    BadWindow,
    RootPreserved,
    Unsupported,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct WindowGeometryReply {
    pub root: u32,
    pub depth: u8,
    pub x: i16,
    pub y: i16,
    pub width: u16,
    pub height: u16,
    pub border_width: u16,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct WindowTreeReply {
    pub root: u32,
    pub parent: u32,
    pub children: Vec<u32>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum PropertyData {
    U8(Vec<u8>),
    U16(Vec<u16>),
    U32(Vec<u32>),
}

impl PropertyData {
    fn item_count(&self) -> usize {
        match self {
            Self::U8(values) => values.len(),
            Self::U16(values) => values.len(),
            Self::U32(values) => values.len(),
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum PropertyMode {
    Replace,
    Prepend,
    Append,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PropertyChangeRequest {
    pub window: u32,
    pub property: u32,
    pub property_type: u32,
    pub mode: PropertyMode,
    pub data: PropertyData,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum PropertyMutationOutcome {
    Success,
    BadMatch,
    BadAlloc,
    Unsupported,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct PropertyReadRequest {
    pub window: u32,
    pub property: u32,
    pub requested_type: Option<u32>,
    pub delete_after_read: bool,
    pub long_offset: u32,
    pub long_length: u32,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PropertyReadReply {
    pub present: bool,
    pub type_matched: bool,
    pub deleted: bool,
    pub property_type: u32,
    pub bytes_after: u32,
    pub data: PropertyData,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum PropertyReadOutcome {
    Success(PropertyReadReply),
    BadValue,
    Unsupported,
}

pub trait CoreDispatchState {
    fn intern_atom(&mut self, name: &[u8], only_if_exists: bool) -> InternAtomOutcome;
    fn atom_name(&self, atom: u32) -> Option<&[u8]>;

    fn create_window(
        &mut self,
        _client: CoreClient,
        _request: WindowCreateRequest,
    ) -> WindowCreateOutcome {
        WindowCreateOutcome::Unsupported
    }

    fn destroy_window(&mut self, _window: u32) -> WindowDestroyOutcome {
        WindowDestroyOutcome::Unsupported
    }

    fn window_geometry(&self, _window: u32) -> Option<WindowGeometryReply> {
        None
    }

    fn window_tree(&self, _window: u32) -> Option<WindowTreeReply> {
        None
    }

    fn window_exists(&self, window: u32) -> bool {
        self.window_geometry(window).is_some()
    }

    fn atom_is_valid(&self, atom: u32, allow_none: bool) -> bool {
        (allow_none && atom == 0) || self.atom_name(atom).is_some()
    }

    fn change_property(&mut self, _request: PropertyChangeRequest) -> PropertyMutationOutcome {
        PropertyMutationOutcome::Unsupported
    }

    fn delete_property(&mut self, _window: u32, _property: u32) -> PropertyMutationOutcome {
        PropertyMutationOutcome::Unsupported
    }

    fn get_property(&mut self, _request: PropertyReadRequest) -> PropertyReadOutcome {
        PropertyReadOutcome::Unsupported
    }

    fn list_properties(&self, _window: u32) -> Option<Vec<u32>> {
        None
    }
}

/// Dispatches the current state-bearing core requests with the default test
/// client identity in addition to the initial stateless profile.
#[must_use]
pub fn dispatch_core_request(
    order: ByteOrder,
    sequence: u64,
    focused_window: u32,
    state: &mut impl CoreDispatchState,
    request: &FramedRequest,
) -> InitialCoreDispatch {
    dispatch_core_request_for_client(
        order,
        sequence,
        focused_window,
        CoreClient::new(1, 0x0040_0000, 0x001f_ffff),
        state,
        request,
    )
}

/// Dispatches state-bearing core requests with the connection identity and
/// resource range needed for new-resource validation.
#[must_use]
pub fn dispatch_core_request_for_client(
    order: ByteOrder,
    sequence: u64,
    focused_window: u32,
    client: CoreClient,
    state: &mut impl CoreDispatchState,
    request: &FramedRequest,
) -> InitialCoreDispatch {
    match request.opcode {
        opcode if opcode == CoreOpcode::CreateWindow as u8 => {
            dispatch_create_window(order, sequence, client, state, request)
        }
        opcode if opcode == CoreOpcode::DestroyWindow as u8 => {
            dispatch_destroy_window(order, sequence, state, request)
        }
        opcode if opcode == CoreOpcode::GetGeometry as u8 => {
            dispatch_get_geometry(order, sequence, state, request)
        }
        opcode if opcode == CoreOpcode::QueryTree as u8 => {
            dispatch_query_tree(order, sequence, state, request)
        }
        opcode if opcode == CoreOpcode::ChangeProperty as u8 => {
            dispatch_change_property(order, sequence, state, request)
        }
        opcode if opcode == CoreOpcode::DeleteProperty as u8 => {
            dispatch_delete_property(order, sequence, state, request)
        }
        opcode if opcode == CoreOpcode::GetProperty as u8 => {
            dispatch_get_property(order, sequence, state, request)
        }
        opcode if opcode == CoreOpcode::ListProperties as u8 => {
            dispatch_list_properties(order, sequence, state, request)
        }
        opcode if opcode == CoreOpcode::InternAtom as u8 => {
            dispatch_intern_atom(order, sequence, state, request)
        }
        opcode if opcode == CoreOpcode::GetAtomName as u8 => {
            dispatch_get_atom_name(order, sequence, state, request)
        }
        _ => dispatch_initial_core_request(order, sequence, focused_window, request),
    }
}

const WINDOW_ATTRIBUTE_MASK: u32 = 0x0000_7fff;
const CORE_EVENT_MASK: u32 = 0x01ff_ffff;
const DO_NOT_PROPAGATE_MASK: u32 = 0x0000_204f;

fn dispatch_create_window(
    order: ByteOrder,
    sequence: u64,
    client: CoreClient,
    state: &mut impl CoreDispatchState,
    request: &FramedRequest,
) -> InitialCoreDispatch {
    if request.core_size() < 32 || request.body().len() < 28 {
        return protocol_error(order, sequence, request, CoreErrorCode::BadLength);
    }
    let body = request.body();
    let xid = read_u32(order, body, 0);
    let parent = read_u32(order, body, 4);
    let window_class = read_u16(order, body, 18);
    let attribute_mask = read_u32(order, body, 24);
    if window_class > 2 {
        return protocol_error_with_value(
            order,
            sequence,
            request,
            CoreErrorCode::BadValue,
            u32::from(window_class),
        );
    }
    if attribute_mask & !WINDOW_ATTRIBUTE_MASK != 0 {
        return protocol_error_with_value(
            order,
            sequence,
            request,
            CoreErrorCode::BadValue,
            attribute_mask,
        );
    }
    let value_count = attribute_mask.count_ones() as usize;
    if request.core_size() != 32 + value_count * 4 || body.len() != 28 + value_count * 4 {
        return protocol_error(order, sequence, request, CoreErrorCode::BadLength);
    }
    let override_redirect = match decode_create_attributes(order, body, attribute_mask) {
        Ok(value) => value,
        Err((code, value)) => {
            return protocol_error_with_value(order, sequence, request, code, value);
        }
    };
    let decoded = WindowCreateRequest {
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
    };
    match state.create_window(client, decoded) {
        WindowCreateOutcome::Success => InitialCoreDispatch::NoReply,
        WindowCreateOutcome::BadIdChoice => {
            protocol_error_with_value(order, sequence, request, CoreErrorCode::BadIdChoice, xid)
        }
        WindowCreateOutcome::BadWindow => {
            protocol_error_with_value(order, sequence, request, CoreErrorCode::BadWindow, parent)
        }
        WindowCreateOutcome::BadValue => {
            protocol_error(order, sequence, request, CoreErrorCode::BadValue)
        }
        WindowCreateOutcome::BadMatch => {
            protocol_error(order, sequence, request, CoreErrorCode::BadMatch)
        }
        WindowCreateOutcome::BadAlloc => {
            protocol_error(order, sequence, request, CoreErrorCode::BadAlloc)
        }
        WindowCreateOutcome::Unsupported => {
            protocol_error(order, sequence, request, CoreErrorCode::BadImplementation)
        }
    }
}

fn decode_create_attributes(
    order: ByteOrder,
    body: &[u8],
    attribute_mask: u32,
) -> Result<bool, (CoreErrorCode, u32)> {
    let mut offset = 28;
    let mut override_redirect = false;
    for bit in 0..15 {
        if attribute_mask & (1_u32 << bit) == 0 {
            continue;
        }
        let value = read_u32(order, body, offset);
        offset += 4;
        match bit {
            0 if value > 1 => return Err((CoreErrorCode::BadPixmap, value)),
            2 if value != 0 => return Err((CoreErrorCode::BadPixmap, value)),
            4 | 5 if value > 10 => return Err((CoreErrorCode::BadValue, value)),
            6 if value > 2 => return Err((CoreErrorCode::BadValue, value)),
            9 | 10 if value > 1 => return Err((CoreErrorCode::BadValue, value)),
            9 => override_redirect = value != 0,
            11 if value & !CORE_EVENT_MASK != 0 => {
                return Err((CoreErrorCode::BadValue, value));
            }
            12 if value & !DO_NOT_PROPAGATE_MASK != 0 => {
                return Err((CoreErrorCode::BadValue, value));
            }
            13 if value != 0 && value != 2 => {
                return Err((CoreErrorCode::BadColormap, value));
            }
            14 if value != 0 => return Err((CoreErrorCode::BadCursor, value)),
            _ => {}
        }
    }
    Ok(override_redirect)
}

fn dispatch_destroy_window(
    order: ByteOrder,
    sequence: u64,
    state: &mut impl CoreDispatchState,
    request: &FramedRequest,
) -> InitialCoreDispatch {
    let Some(window) = exact_window_request(order, request) else {
        return protocol_error(order, sequence, request, CoreErrorCode::BadLength);
    };
    match state.destroy_window(window) {
        WindowDestroyOutcome::Success | WindowDestroyOutcome::RootPreserved => {
            InitialCoreDispatch::NoReply
        }
        WindowDestroyOutcome::BadWindow => {
            protocol_error_with_value(order, sequence, request, CoreErrorCode::BadWindow, window)
        }
        WindowDestroyOutcome::Unsupported => {
            protocol_error(order, sequence, request, CoreErrorCode::BadImplementation)
        }
    }
}

fn dispatch_get_geometry(
    order: ByteOrder,
    sequence: u64,
    state: &impl CoreDispatchState,
    request: &FramedRequest,
) -> InitialCoreDispatch {
    let Some(drawable) = exact_window_request(order, request) else {
        return protocol_error(order, sequence, request, CoreErrorCode::BadLength);
    };
    let Some(geometry) = state.window_geometry(drawable) else {
        return protocol_error_with_value(
            order,
            sequence,
            request,
            CoreErrorCode::BadDrawable,
            drawable,
        );
    };
    let mut reply = ReplyBuilder::new(order, sequence, geometry.depth);
    reply
        .write_u32(geometry.root)
        .and_then(|()| reply.write_u16(geometry.x as u16))
        .and_then(|()| reply.write_u16(geometry.y as u16))
        .and_then(|()| reply.write_u16(geometry.width))
        .and_then(|()| reply.write_u16(geometry.height))
        .and_then(|()| reply.write_u16(geometry.border_width))
        .and_then(|()| reply.write_padding(2))
        .expect("GetGeometry fixed fields fit the core reply");
    InitialCoreDispatch::Packet(
        reply
            .finish()
            .expect("GetGeometry has no variable-length payload"),
    )
}

fn dispatch_query_tree(
    order: ByteOrder,
    sequence: u64,
    state: &impl CoreDispatchState,
    request: &FramedRequest,
) -> InitialCoreDispatch {
    let Some(window) = exact_window_request(order, request) else {
        return protocol_error(order, sequence, request, CoreErrorCode::BadLength);
    };
    let Some(tree) = state.window_tree(window) else {
        return protocol_error_with_value(
            order,
            sequence,
            request,
            CoreErrorCode::BadWindow,
            window,
        );
    };
    let Ok(child_count) = u16::try_from(tree.children.len()) else {
        return protocol_error(order, sequence, request, CoreErrorCode::BadAlloc);
    };
    let mut reply = ReplyBuilder::new(order, sequence, 0);
    reply
        .write_u32(tree.root)
        .and_then(|()| reply.write_u32(tree.parent))
        .and_then(|()| reply.write_u16(child_count))
        .and_then(|()| reply.write_padding(14))
        .expect("QueryTree fixed fields fit the core reply");
    for child in tree.children {
        reply.write_payload_u32(child);
    }
    InitialCoreDispatch::Packet(
        reply
            .finish()
            .expect("the bounded window table fits the reply length field"),
    )
}

fn dispatch_change_property(
    order: ByteOrder,
    sequence: u64,
    state: &mut impl CoreDispatchState,
    request: &FramedRequest,
) -> InitialCoreDispatch {
    if request.core_size() < 24 || request.body().len() < 20 {
        return protocol_error(order, sequence, request, CoreErrorCode::BadLength);
    }
    let mode = match request.data {
        0 => PropertyMode::Replace,
        1 => PropertyMode::Prepend,
        2 => PropertyMode::Append,
        value => {
            return protocol_error_with_value(
                order,
                sequence,
                request,
                CoreErrorCode::BadValue,
                u32::from(value),
            );
        }
    };
    let body = request.body();
    let window = read_u32(order, body, 0);
    let property = read_u32(order, body, 4);
    let property_type = read_u32(order, body, 8);
    let format = body[12];
    let item_count = read_u32(order, body, 16);
    if format != 8 && format != 16 && format != 32 {
        return protocol_error_with_value(
            order,
            sequence,
            request,
            CoreErrorCode::BadValue,
            u32::from(format),
        );
    }
    let Some(data_size) = usize::try_from(item_count)
        .ok()
        .and_then(|count| count.checked_mul(usize::from(format / 8)))
    else {
        return protocol_error(order, sequence, request, CoreErrorCode::BadLength);
    };
    let Some(padded_size) = data_size.checked_add(3).map(|size| size & !3) else {
        return protocol_error(order, sequence, request, CoreErrorCode::BadLength);
    };
    if request.core_size() != 24 + padded_size || body.len() != 20 + padded_size {
        return protocol_error(order, sequence, request, CoreErrorCode::BadLength);
    }
    if !state.window_exists(window) {
        return protocol_error_with_value(
            order,
            sequence,
            request,
            CoreErrorCode::BadWindow,
            window,
        );
    }
    if !state.atom_is_valid(property, false) {
        return protocol_error_with_value(
            order,
            sequence,
            request,
            CoreErrorCode::BadAtom,
            property,
        );
    }
    if !state.atom_is_valid(property_type, false) {
        return protocol_error_with_value(
            order,
            sequence,
            request,
            CoreErrorCode::BadAtom,
            property_type,
        );
    }
    let Some(data) = decode_property_data(order, format, item_count, &body[20..20 + data_size])
    else {
        return protocol_error(order, sequence, request, CoreErrorCode::BadAlloc);
    };
    match state.change_property(PropertyChangeRequest {
        window,
        property,
        property_type,
        mode,
        data,
    }) {
        PropertyMutationOutcome::Success => InitialCoreDispatch::NoReply,
        PropertyMutationOutcome::BadMatch => {
            protocol_error(order, sequence, request, CoreErrorCode::BadMatch)
        }
        PropertyMutationOutcome::BadAlloc => {
            protocol_error(order, sequence, request, CoreErrorCode::BadAlloc)
        }
        PropertyMutationOutcome::Unsupported => {
            protocol_error(order, sequence, request, CoreErrorCode::BadImplementation)
        }
    }
}

fn decode_property_data(
    order: ByteOrder,
    format: u8,
    item_count: u32,
    bytes: &[u8],
) -> Option<PropertyData> {
    let count = usize::try_from(item_count).ok()?;
    match format {
        8 => {
            let mut values = Vec::new();
            values.try_reserve_exact(count).ok()?;
            values.extend_from_slice(bytes);
            Some(PropertyData::U8(values))
        }
        16 => {
            let mut values = Vec::new();
            values.try_reserve_exact(count).ok()?;
            values.extend(
                bytes
                    .chunks_exact(2)
                    .map(|value| order.read_u16([value[0], value[1]])),
            );
            Some(PropertyData::U16(values))
        }
        32 => {
            let mut values = Vec::new();
            values.try_reserve_exact(count).ok()?;
            values.extend(
                bytes
                    .chunks_exact(4)
                    .map(|value| order.read_u32([value[0], value[1], value[2], value[3]])),
            );
            Some(PropertyData::U32(values))
        }
        _ => None,
    }
}

fn dispatch_delete_property(
    order: ByteOrder,
    sequence: u64,
    state: &mut impl CoreDispatchState,
    request: &FramedRequest,
) -> InitialCoreDispatch {
    if request.core_size() != 12 || request.body().len() != 8 {
        return protocol_error(order, sequence, request, CoreErrorCode::BadLength);
    }
    let window = read_u32(order, request.body(), 0);
    let property = read_u32(order, request.body(), 4);
    if !state.window_exists(window) {
        return protocol_error_with_value(
            order,
            sequence,
            request,
            CoreErrorCode::BadWindow,
            window,
        );
    }
    if !state.atom_is_valid(property, false) {
        return protocol_error_with_value(
            order,
            sequence,
            request,
            CoreErrorCode::BadAtom,
            property,
        );
    }
    match state.delete_property(window, property) {
        PropertyMutationOutcome::Success => InitialCoreDispatch::NoReply,
        PropertyMutationOutcome::BadAlloc => {
            protocol_error(order, sequence, request, CoreErrorCode::BadAlloc)
        }
        PropertyMutationOutcome::BadMatch => {
            protocol_error(order, sequence, request, CoreErrorCode::BadMatch)
        }
        PropertyMutationOutcome::Unsupported => {
            protocol_error(order, sequence, request, CoreErrorCode::BadImplementation)
        }
    }
}

fn dispatch_get_property(
    order: ByteOrder,
    sequence: u64,
    state: &mut impl CoreDispatchState,
    request: &FramedRequest,
) -> InitialCoreDispatch {
    if request.core_size() != 24 || request.body().len() != 20 {
        return protocol_error(order, sequence, request, CoreErrorCode::BadLength);
    }
    if request.data > 1 {
        return protocol_error_with_value(
            order,
            sequence,
            request,
            CoreErrorCode::BadValue,
            u32::from(request.data),
        );
    }
    let body = request.body();
    let window = read_u32(order, body, 0);
    let property = read_u32(order, body, 4);
    let requested_type = read_u32(order, body, 8);
    let long_offset = read_u32(order, body, 12);
    let long_length = read_u32(order, body, 16);
    if !state.window_exists(window) {
        return protocol_error_with_value(
            order,
            sequence,
            request,
            CoreErrorCode::BadWindow,
            window,
        );
    }
    if !state.atom_is_valid(property, false) {
        return protocol_error_with_value(
            order,
            sequence,
            request,
            CoreErrorCode::BadAtom,
            property,
        );
    }
    if !state.atom_is_valid(requested_type, true) {
        return protocol_error_with_value(
            order,
            sequence,
            request,
            CoreErrorCode::BadAtom,
            requested_type,
        );
    }
    let reply = match state.get_property(PropertyReadRequest {
        window,
        property,
        requested_type: (requested_type != 0).then_some(requested_type),
        delete_after_read: request.data != 0,
        long_offset,
        long_length,
    }) {
        PropertyReadOutcome::Success(reply) => reply,
        PropertyReadOutcome::BadValue => {
            return protocol_error_with_value(
                order,
                sequence,
                request,
                CoreErrorCode::BadValue,
                long_offset,
            );
        }
        PropertyReadOutcome::Unsupported => {
            return protocol_error(order, sequence, request, CoreErrorCode::BadImplementation);
        }
    };
    let format = if reply.present {
        match reply.data {
            PropertyData::U8(_) => 8,
            PropertyData::U16(_) => 16,
            PropertyData::U32(_) => 32,
        }
    } else {
        0
    };
    let item_count = if reply.present && reply.type_matched {
        match u32::try_from(reply.data.item_count()) {
            Ok(count) => count,
            Err(_) => return protocol_error(order, sequence, request, CoreErrorCode::BadAlloc),
        }
    } else {
        0
    };
    let mut encoded = ReplyBuilder::new(order, sequence, format);
    encoded
        .write_u32(if reply.present {
            reply.property_type
        } else {
            0
        })
        .and_then(|()| encoded.write_u32(if reply.present { reply.bytes_after } else { 0 }))
        .and_then(|()| encoded.write_u32(item_count))
        .and_then(|()| encoded.write_padding(12))
        .expect("GetProperty fixed fields fit the core reply");
    if reply.present && reply.type_matched {
        match reply.data {
            PropertyData::U8(values) => encoded.write_payload(&values),
            PropertyData::U16(values) => {
                for value in values {
                    encoded.write_payload_u16(value);
                }
            }
            PropertyData::U32(values) => {
                for value in values {
                    encoded.write_payload_u32(value);
                }
            }
        }
    }
    match encoded.finish() {
        Ok(packet) => InitialCoreDispatch::Packet(packet),
        Err(_) => protocol_error(order, sequence, request, CoreErrorCode::BadAlloc),
    }
}

fn dispatch_list_properties(
    order: ByteOrder,
    sequence: u64,
    state: &impl CoreDispatchState,
    request: &FramedRequest,
) -> InitialCoreDispatch {
    let Some(window) = exact_window_request(order, request) else {
        return protocol_error(order, sequence, request, CoreErrorCode::BadLength);
    };
    if !state.window_exists(window) {
        return protocol_error_with_value(
            order,
            sequence,
            request,
            CoreErrorCode::BadWindow,
            window,
        );
    }
    let Some(atoms) = state.list_properties(window) else {
        return protocol_error(order, sequence, request, CoreErrorCode::BadImplementation);
    };
    let Ok(count) = u16::try_from(atoms.len()) else {
        return protocol_error(order, sequence, request, CoreErrorCode::BadAlloc);
    };
    let mut reply = ReplyBuilder::new(order, sequence, 0);
    reply
        .write_u16(count)
        .and_then(|()| reply.write_padding(22))
        .expect("ListProperties fixed fields fit the core reply");
    for atom in atoms {
        reply.write_payload_u32(atom);
    }
    match reply.finish() {
        Ok(packet) => InitialCoreDispatch::Packet(packet),
        Err(_) => protocol_error(order, sequence, request, CoreErrorCode::BadAlloc),
    }
}

fn exact_window_request(order: ByteOrder, request: &FramedRequest) -> Option<u32> {
    (request.core_size() == 8 && request.body().len() == 4)
        .then(|| read_u32(order, request.body(), 0))
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

/// Dispatches the deliberately small core profile used by the first Rust
/// request loop. State-bearing requests remain outside this boundary.
#[must_use]
pub fn dispatch_initial_core_request(
    order: ByteOrder,
    sequence: u64,
    focused_window: u32,
    request: &FramedRequest,
) -> InitialCoreDispatch {
    match request.opcode {
        opcode if opcode == CoreOpcode::NoOperation as u8 => InitialCoreDispatch::NoReply,
        opcode if opcode == CoreOpcode::GetInputFocus as u8 => {
            if request.core_size() != 4 {
                return protocol_error(order, sequence, request, CoreErrorCode::BadLength);
            }
            let mut reply = ReplyBuilder::new(order, sequence, 0);
            reply
                .write_u32(focused_window)
                .expect("GetInputFocus fixed fields fit the core reply");
            InitialCoreDispatch::Packet(
                reply
                    .finish()
                    .expect("GetInputFocus has no variable-length payload"),
            )
        }
        _ => protocol_error(order, sequence, request, CoreErrorCode::BadRequest),
    }
}

fn protocol_error(
    order: ByteOrder,
    sequence: u64,
    request: &FramedRequest,
    code: CoreErrorCode,
) -> InitialCoreDispatch {
    protocol_error_with_value(order, sequence, request, code, 0)
}

fn protocol_error_with_value(
    order: ByteOrder,
    sequence: u64,
    request: &FramedRequest,
    code: CoreErrorCode,
    bad_value: u32,
) -> InitialCoreDispatch {
    InitialCoreDispatch::Packet(encode_core_error(
        order,
        CoreError {
            code,
            sequence,
            bad_value,
            major_opcode: request.opcode,
            minor_opcode: if request.opcode >= 128 {
                u16::from(request.data)
            } else {
                0
            },
        },
    ))
}

fn dispatch_intern_atom(
    order: ByteOrder,
    sequence: u64,
    state: &mut impl CoreDispatchState,
    request: &FramedRequest,
) -> InitialCoreDispatch {
    if request.data > 1 {
        return protocol_error_with_value(
            order,
            sequence,
            request,
            CoreErrorCode::BadValue,
            u32::from(request.data),
        );
    }
    let body = request.body();
    if body.len() < 4 {
        return protocol_error(order, sequence, request, CoreErrorCode::BadLength);
    }
    let name_length = usize::from(order.read_u16([body[0], body[1]]));
    let Some(padded_name_length) = name_length.checked_add(3).map(|value| value & !3) else {
        return protocol_error(order, sequence, request, CoreErrorCode::BadLength);
    };
    if request.core_size() != 8 + padded_name_length || body.len() < 4 + name_length {
        return protocol_error(order, sequence, request, CoreErrorCode::BadLength);
    }
    let name = &body[4..4 + name_length];
    let atom = match state.intern_atom(name, request.data != 0) {
        InternAtomOutcome::Success(atom) => atom,
        InternAtomOutcome::Exhausted => {
            return protocol_error(order, sequence, request, CoreErrorCode::BadAlloc);
        }
    };
    let mut reply = ReplyBuilder::new(order, sequence, 0);
    reply
        .write_u32(atom)
        .expect("InternAtom fixed fields fit the core reply");
    InitialCoreDispatch::Packet(
        reply
            .finish()
            .expect("InternAtom has no variable-length payload"),
    )
}

fn dispatch_get_atom_name(
    order: ByteOrder,
    sequence: u64,
    state: &impl CoreDispatchState,
    request: &FramedRequest,
) -> InitialCoreDispatch {
    if request.core_size() != 8 {
        return protocol_error(order, sequence, request, CoreErrorCode::BadLength);
    }
    let body = request.body();
    if body.len() != 4 {
        return protocol_error(order, sequence, request, CoreErrorCode::BadLength);
    }
    let atom = order.read_u32([body[0], body[1], body[2], body[3]]);
    let Some(name) = state.atom_name(atom) else {
        return protocol_error_with_value(order, sequence, request, CoreErrorCode::BadAtom, atom);
    };
    let Ok(name_length) = u16::try_from(name.len()) else {
        return protocol_error(order, sequence, request, CoreErrorCode::BadAlloc);
    };
    let mut reply = ReplyBuilder::new(order, sequence, 0);
    reply
        .write_u16(name_length)
        .expect("GetAtomName length fits the core reply");
    reply
        .write_padding(22)
        .expect("GetAtomName padding fills the fixed reply fields");
    reply.write_payload(name);
    InitialCoreDispatch::Packet(
        reply
            .finish()
            .expect("GetAtomName's u16-sized payload fits the reply length field"),
    )
}

#[cfg(test)]
mod tests {
    use super::*;

    fn request_with_data(order: ByteOrder, opcode: u8, data: u8, units: u16) -> FramedRequest {
        let mut bytes = vec![opcode, data];
        let encoded_units = match order {
            ByteOrder::LittleEndian => units.to_le_bytes(),
            ByteOrder::BigEndian => units.to_be_bytes(),
        };
        bytes.extend_from_slice(&encoded_units);
        bytes.resize(usize::from(units) * 4, 0);
        FramedRequest {
            opcode,
            data,
            length_units: u32::from(units),
            header_size: 4,
            bytes,
        }
    }

    fn request(order: ByteOrder, opcode: u8, units: u16) -> FramedRequest {
        request_with_data(order, opcode, 0, units)
    }

    fn u16_at(order: ByteOrder, bytes: &[u8], offset: usize) -> u16 {
        match order {
            ByteOrder::LittleEndian => u16::from_le_bytes([bytes[offset], bytes[offset + 1]]),
            ByteOrder::BigEndian => u16::from_be_bytes([bytes[offset], bytes[offset + 1]]),
        }
    }

    fn u32_at(order: ByteOrder, bytes: &[u8], offset: usize) -> u32 {
        match order {
            ByteOrder::LittleEndian => u32::from_le_bytes([
                bytes[offset],
                bytes[offset + 1],
                bytes[offset + 2],
                bytes[offset + 3],
            ]),
            ByteOrder::BigEndian => u32::from_be_bytes([
                bytes[offset],
                bytes[offset + 1],
                bytes[offset + 2],
                bytes[offset + 3],
            ]),
        }
    }

    #[test]
    fn initial_profile_matches_core_reply_and_error_semantics() {
        for order in [ByteOrder::LittleEndian, ByteOrder::BigEndian] {
            assert_eq!(
                dispatch_initial_core_request(
                    order,
                    1,
                    1,
                    &request(order, CoreOpcode::NoOperation as u8, 8),
                ),
                InitialCoreDispatch::NoReply
            );

            let InitialCoreDispatch::Packet(focus) = dispatch_initial_core_request(
                order,
                0x1_0000,
                1,
                &request(order, CoreOpcode::GetInputFocus as u8, 1),
            ) else {
                panic!("GetInputFocus must reply");
            };
            assert_eq!(focus.len(), 32);
            assert_eq!(focus[0], 1);
            assert_eq!(u16_at(order, &focus, 2), 0);
            assert_eq!(u32_at(order, &focus, 8), 1);

            let InitialCoreDispatch::Packet(bad_length) = dispatch_initial_core_request(
                order,
                2,
                1,
                &request(order, CoreOpcode::GetInputFocus as u8, 2),
            ) else {
                panic!("invalid GetInputFocus must error");
            };
            assert_eq!(bad_length[0], 0);
            assert_eq!(bad_length[1], CoreErrorCode::BadLength as u8);
            assert_eq!(bad_length[10], CoreOpcode::GetInputFocus as u8);

            let InitialCoreDispatch::Packet(unsupported) =
                dispatch_initial_core_request(order, 3, 1, &request(order, 250, 1))
            else {
                panic!("unsupported opcode must error");
            };
            assert_eq!(unsupported[1], CoreErrorCode::BadRequest as u8);
            assert_eq!(u16_at(order, &unsupported, 2), 3);
            assert_eq!(unsupported[10], 250);

            let InitialCoreDispatch::Packet(unsupported_extension) =
                dispatch_initial_core_request(order, 4, 1, &request_with_data(order, 200, 7, 1))
            else {
                panic!("unsupported extension request must error");
            };
            assert_eq!(unsupported_extension[1], CoreErrorCode::BadRequest as u8);
            assert_eq!(u16_at(order, &unsupported_extension, 8), 7);
            assert_eq!(unsupported_extension[10], 200);
        }
    }
}
