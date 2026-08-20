use crate::{
    ByteOrder, CoreError, CoreErrorCode, CoreOpcode, CoreRequestDecodeError, ExtensionAssignment,
    ExtensionDescriptor, ExtensionKind, FramedRequest, MAXIMUM_BIG_REQUEST_LENGTH_UNITS,
    PropertyMode, ReplyBuilder, WindowCreateRequest, decode_change_property, decode_create_window,
    decode_delete_property, decode_empty_request, decode_get_property, decode_intern_atom,
    decode_query_extension, decode_single_u32_request, encode_core_error,
};

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum CoreDispatchAction {
    EnableBigRequests { maximum_length_units: u32 },
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum InitialCoreDispatch {
    NoReply,
    Packet(Vec<u8>),
    PacketWithAction {
        packet: Vec<u8>,
        action: CoreDispatchAction,
    },
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

    fn query_extension(&self, _name: &[u8]) -> Option<ExtensionAssignment> {
        None
    }

    fn extension_by_opcode(&self, _major_opcode: u8) -> Option<ExtensionDescriptor> {
        None
    }

    fn enabled_extension_names(&self) -> Vec<&'static [u8]> {
        Vec::new()
    }

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
    if request.opcode >= 128 {
        return dispatch_extension_request(order, sequence, state, request);
    }
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
        opcode if opcode == CoreOpcode::QueryExtension as u8 => {
            dispatch_query_extension(order, sequence, state, request)
        }
        opcode if opcode == CoreOpcode::ListExtensions as u8 => {
            dispatch_list_extensions(order, sequence, state, request)
        }
        _ => dispatch_initial_core_request(order, sequence, focused_window, request),
    }
}

fn dispatch_extension_request(
    order: ByteOrder,
    sequence: u64,
    state: &impl CoreDispatchState,
    request: &FramedRequest,
) -> InitialCoreDispatch {
    let Some(extension) = state.extension_by_opcode(request.opcode) else {
        return protocol_error(order, sequence, request, CoreErrorCode::BadRequest);
    };
    match extension.kind {
        ExtensionKind::BigRequests => {
            if request.data != 0 {
                return protocol_error(order, sequence, request, CoreErrorCode::BadRequest);
            }
            if request.core_size() != 4 || !request.body().is_empty() {
                return protocol_error(order, sequence, request, CoreErrorCode::BadLength);
            }
            let mut reply = ReplyBuilder::new(order, sequence, 0);
            reply
                .write_u32(MAXIMUM_BIG_REQUEST_LENGTH_UNITS)
                .expect("BIG-REQUESTS fixed fields fit the core reply");
            InitialCoreDispatch::PacketWithAction {
                packet: reply
                    .finish()
                    .expect("BIG-REQUESTS has no variable-length payload"),
                action: CoreDispatchAction::EnableBigRequests {
                    maximum_length_units: MAXIMUM_BIG_REQUEST_LENGTH_UNITS,
                },
            }
        }
        ExtensionKind::MitShm
        | ExtensionKind::XFixes
        | ExtensionKind::Damage
        | ExtensionKind::Render
        | ExtensionKind::Composite
        | ExtensionKind::RandR
        | ExtensionKind::GwScale
        | ExtensionKind::GwVrr => {
            protocol_error(order, sequence, request, CoreErrorCode::BadImplementation)
        }
    }
}

fn dispatch_create_window(
    order: ByteOrder,
    sequence: u64,
    client: CoreClient,
    state: &mut impl CoreDispatchState,
    request: &FramedRequest,
) -> InitialCoreDispatch {
    let decoded = match decode_create_window(order, request) {
        Ok(decoded) => decoded,
        Err(error) => return protocol_decode_error(order, sequence, request, error),
    };
    let xid = decoded.xid;
    let parent = decoded.parent;
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

fn dispatch_destroy_window(
    order: ByteOrder,
    sequence: u64,
    state: &mut impl CoreDispatchState,
    request: &FramedRequest,
) -> InitialCoreDispatch {
    let window = match decode_single_u32_request(order, request) {
        Ok(window) => window,
        Err(error) => return protocol_decode_error(order, sequence, request, error),
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
    let drawable = match decode_single_u32_request(order, request) {
        Ok(drawable) => drawable,
        Err(error) => return protocol_decode_error(order, sequence, request, error),
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
    let window = match decode_single_u32_request(order, request) {
        Ok(window) => window,
        Err(error) => return protocol_decode_error(order, sequence, request, error),
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
    let decoded = match decode_change_property(order, request) {
        Ok(decoded) => decoded,
        Err(error) => return protocol_decode_error(order, sequence, request, error),
    };
    if !state.window_exists(decoded.window) {
        return protocol_error_with_value(
            order,
            sequence,
            request,
            CoreErrorCode::BadWindow,
            decoded.window,
        );
    }
    if !state.atom_is_valid(decoded.property, false) {
        return protocol_error_with_value(
            order,
            sequence,
            request,
            CoreErrorCode::BadAtom,
            decoded.property,
        );
    }
    if !state.atom_is_valid(decoded.property_type, false) {
        return protocol_error_with_value(
            order,
            sequence,
            request,
            CoreErrorCode::BadAtom,
            decoded.property_type,
        );
    }
    let Some(data) = decode_property_data(order, decoded.format, decoded.item_count, decoded.data)
    else {
        return protocol_error(order, sequence, request, CoreErrorCode::BadAlloc);
    };
    match state.change_property(PropertyChangeRequest {
        window: decoded.window,
        property: decoded.property,
        property_type: decoded.property_type,
        mode: decoded.mode,
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
    let decoded = match decode_delete_property(order, request) {
        Ok(decoded) => decoded,
        Err(error) => return protocol_decode_error(order, sequence, request, error),
    };
    if !state.window_exists(decoded.window) {
        return protocol_error_with_value(
            order,
            sequence,
            request,
            CoreErrorCode::BadWindow,
            decoded.window,
        );
    }
    if !state.atom_is_valid(decoded.property, false) {
        return protocol_error_with_value(
            order,
            sequence,
            request,
            CoreErrorCode::BadAtom,
            decoded.property,
        );
    }
    match state.delete_property(decoded.window, decoded.property) {
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
    let decoded = match decode_get_property(order, request) {
        Ok(decoded) => decoded,
        Err(error) => return protocol_decode_error(order, sequence, request, error),
    };
    if !state.window_exists(decoded.window) {
        return protocol_error_with_value(
            order,
            sequence,
            request,
            CoreErrorCode::BadWindow,
            decoded.window,
        );
    }
    if !state.atom_is_valid(decoded.property, false) {
        return protocol_error_with_value(
            order,
            sequence,
            request,
            CoreErrorCode::BadAtom,
            decoded.property,
        );
    }
    if !state.atom_is_valid(decoded.requested_type, true) {
        return protocol_error_with_value(
            order,
            sequence,
            request,
            CoreErrorCode::BadAtom,
            decoded.requested_type,
        );
    }
    let reply = match state.get_property(PropertyReadRequest {
        window: decoded.window,
        property: decoded.property,
        requested_type: (decoded.requested_type != 0).then_some(decoded.requested_type),
        delete_after_read: decoded.delete_after_read,
        long_offset: decoded.long_offset,
        long_length: decoded.long_length,
    }) {
        PropertyReadOutcome::Success(reply) => reply,
        PropertyReadOutcome::BadValue => {
            return protocol_error_with_value(
                order,
                sequence,
                request,
                CoreErrorCode::BadValue,
                decoded.long_offset,
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
    let window = match decode_single_u32_request(order, request) {
        Ok(window) => window,
        Err(error) => return protocol_decode_error(order, sequence, request, error),
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
            if let Err(error) = decode_empty_request(request) {
                return protocol_decode_error(order, sequence, request, error);
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

fn protocol_decode_error(
    order: ByteOrder,
    sequence: u64,
    request: &FramedRequest,
    error: CoreRequestDecodeError,
) -> InitialCoreDispatch {
    protocol_error_with_value(order, sequence, request, error.code, error.bad_value)
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
    let decoded = match decode_intern_atom(order, request) {
        Ok(decoded) => decoded,
        Err(error) => return protocol_decode_error(order, sequence, request, error),
    };
    let atom = match state.intern_atom(decoded.name, decoded.only_if_exists) {
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
    let atom = match decode_single_u32_request(order, request) {
        Ok(atom) => atom,
        Err(error) => return protocol_decode_error(order, sequence, request, error),
    };
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

fn dispatch_query_extension(
    order: ByteOrder,
    sequence: u64,
    state: &impl CoreDispatchState,
    request: &FramedRequest,
) -> InitialCoreDispatch {
    let decoded = match decode_query_extension(order, request) {
        Ok(decoded) => decoded,
        Err(error) => return protocol_decode_error(order, sequence, request, error),
    };
    let extension = state.query_extension(decoded.name);
    let mut reply = ReplyBuilder::new(order, sequence, 0);
    reply
        .write_u8(u8::from(extension.is_some()))
        .and_then(|()| reply.write_u8(extension.map_or(0, |value| value.major_opcode)))
        .and_then(|()| reply.write_u8(extension.map_or(0, |value| value.first_event)))
        .and_then(|()| reply.write_u8(extension.map_or(0, |value| value.first_error)))
        .expect("QueryExtension fixed fields fit the core reply");
    InitialCoreDispatch::Packet(
        reply
            .finish()
            .expect("QueryExtension has no variable-length payload"),
    )
}

fn dispatch_list_extensions(
    order: ByteOrder,
    sequence: u64,
    state: &impl CoreDispatchState,
    request: &FramedRequest,
) -> InitialCoreDispatch {
    if let Err(error) = decode_empty_request(request) {
        return protocol_decode_error(order, sequence, request, error);
    }
    let names = state.enabled_extension_names();
    let Ok(count) = u8::try_from(names.len()) else {
        return protocol_error(order, sequence, request, CoreErrorCode::BadAlloc);
    };
    if names.iter().any(|name| u8::try_from(name.len()).is_err()) {
        return protocol_error(order, sequence, request, CoreErrorCode::BadAlloc);
    }
    let mut reply = ReplyBuilder::new(order, sequence, count);
    for name in names {
        reply.write_payload(&[name.len() as u8]);
        reply.write_payload(name);
    }
    match reply.finish() {
        Ok(packet) => InitialCoreDispatch::Packet(packet),
        Err(_) => protocol_error(order, sequence, request, CoreErrorCode::BadAlloc),
    }
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

    struct DiscoveryState;

    impl CoreDispatchState for DiscoveryState {
        fn intern_atom(&mut self, _name: &[u8], _only_if_exists: bool) -> InternAtomOutcome {
            unreachable!("extension discovery does not intern atoms")
        }

        fn atom_name(&self, _atom: u32) -> Option<&[u8]> {
            None
        }

        fn query_extension(&self, name: &[u8]) -> Option<ExtensionAssignment> {
            (name == b"BIG-REQUESTS").then_some(ExtensionAssignment {
                major_opcode: 128,
                first_event: 0,
                first_error: 0,
            })
        }

        fn extension_by_opcode(&self, major_opcode: u8) -> Option<ExtensionDescriptor> {
            (major_opcode == 128).then_some(crate::EXTENSION_REGISTRY[0])
        }

        fn enabled_extension_names(&self) -> Vec<&'static [u8]> {
            vec![b"BIG-REQUESTS", b"XFIXES"]
        }
    }

    fn query_extension_request(order: ByteOrder, name: &[u8]) -> FramedRequest {
        let padded_length = (name.len() + 3) & !3;
        let mut request = request(
            order,
            CoreOpcode::QueryExtension as u8,
            (2 + padded_length / 4) as u16,
        );
        request.bytes[4..6].copy_from_slice(&match order {
            ByteOrder::LittleEndian => (name.len() as u16).to_le_bytes(),
            ByteOrder::BigEndian => (name.len() as u16).to_be_bytes(),
        });
        request.bytes[8..8 + name.len()].copy_from_slice(name);
        request
    }

    #[test]
    fn big_requests_dispatch_matches_legacy_framing_contract() {
        for order in [ByteOrder::LittleEndian, ByteOrder::BigEndian] {
            let mut state = DiscoveryState;
            let big_requests = request(order, 128, 1);
            let InitialCoreDispatch::PacketWithAction { packet, action } =
                dispatch_core_request(order, 0x1_0001, 1, &mut state, &big_requests)
            else {
                panic!("enabled BIG-REQUESTS must return reply plus framing action");
            };
            assert_eq!(packet.len(), 32);
            assert_eq!(u16_at(order, &packet, 2), 1);
            assert_eq!(u32_at(order, &packet, 8), MAXIMUM_BIG_REQUEST_LENGTH_UNITS);
            assert_eq!(
                action,
                CoreDispatchAction::EnableBigRequests {
                    maximum_length_units: MAXIMUM_BIG_REQUEST_LENGTH_UNITS,
                }
            );

            let wrong_data = request_with_data(order, 128, 1, 1);
            let InitialCoreDispatch::Packet(error) =
                dispatch_core_request(order, 2, 1, &mut state, &wrong_data)
            else {
                panic!("BIG-REQUESTS nonzero minor opcode must fail");
            };
            assert_eq!(error[1], CoreErrorCode::BadRequest as u8);
            assert_eq!(error[10], 128);
            assert_eq!(u16_at(order, &error, 8), 1);

            let wrong_length = request(order, 128, 2);
            let InitialCoreDispatch::Packet(error) =
                dispatch_core_request(order, 3, 1, &mut state, &wrong_length)
            else {
                panic!("BIG-REQUESTS wrong length must fail");
            };
            assert_eq!(error[1], CoreErrorCode::BadLength as u8);

            let unknown = request(order, 200, 1);
            let InitialCoreDispatch::Packet(error) =
                dispatch_core_request(order, 4, 1, &mut state, &unknown)
            else {
                panic!("unknown extension opcode must fail");
            };
            assert_eq!(error[1], CoreErrorCode::BadRequest as u8);
        }
    }

    #[test]
    fn extension_discovery_preserves_wire_semantics_and_bounds() {
        for order in [ByteOrder::LittleEndian, ByteOrder::BigEndian] {
            let mut state = DiscoveryState;
            let InitialCoreDispatch::Packet(present) = dispatch_core_request(
                order,
                0x1_0001,
                1,
                &mut state,
                &query_extension_request(order, b"BIG-REQUESTS"),
            ) else {
                panic!("QueryExtension must reply");
            };
            assert_eq!(present.len(), 32);
            assert_eq!(u16_at(order, &present, 2), 1);
            assert_eq!(&present[8..12], &[1, 128, 0, 0]);

            for name in [b"big-requests".as_slice(), &[b'R', 0xff][..]] {
                let InitialCoreDispatch::Packet(absent) = dispatch_core_request(
                    order,
                    2,
                    1,
                    &mut state,
                    &query_extension_request(order, name),
                ) else {
                    panic!("unknown QueryExtension must reply");
                };
                assert_eq!(&absent[8..12], &[0, 0, 0, 0]);
            }

            let InitialCoreDispatch::Packet(list) = dispatch_core_request(
                order,
                3,
                1,
                &mut state,
                &request(order, CoreOpcode::ListExtensions as u8, 1),
            ) else {
                panic!("ListExtensions must reply");
            };
            assert_eq!(list[1], 2);
            assert_eq!(u32_at(order, &list, 4), 5);
            assert_eq!(&list[32..], b"\x0cBIG-REQUESTS\x06XFIXES");

            let malformed = request(order, CoreOpcode::QueryExtension as u8, 1);
            let InitialCoreDispatch::Packet(error) =
                dispatch_core_request(order, 4, 1, &mut state, &malformed)
            else {
                panic!("malformed QueryExtension must error");
            };
            assert_eq!(error[1], CoreErrorCode::BadLength as u8);
            assert_eq!(error[10], CoreOpcode::QueryExtension as u8);

            let wrong_list_length = request(order, CoreOpcode::ListExtensions as u8, 2);
            let InitialCoreDispatch::Packet(error) =
                dispatch_core_request(order, 5, 1, &mut state, &wrong_list_length)
            else {
                panic!("malformed ListExtensions must error");
            };
            assert_eq!(error[1], CoreErrorCode::BadLength as u8);
            assert_eq!(error[10], CoreOpcode::ListExtensions as u8);
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
