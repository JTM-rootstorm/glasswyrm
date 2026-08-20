//! Hardware- and transport-independent X11 protocol codecs for Glasswyrm.

#![forbid(unsafe_code)]

mod atoms;
mod byte_order;
mod core;
mod decode;
mod dispatch;
mod event;
mod event_mask;
mod extension;
mod lifecycle_request;
mod reply;
mod request;
mod screen;
mod selection_request;
mod setup;
mod window_request;
mod wire;

pub use atoms::{LAST_PREDEFINED_ATOM, NONE_ATOM, PREDEFINED_ATOMS, PredefinedAtom};
pub use byte_order::ByteOrder;
pub use core::{CoreErrorCode, CoreEventType, CoreOpcode, wire_sequence};
pub use decode::{
    CoreRequestDecodeError, DecodedPropertyChangeRequest, DeletePropertyRequest,
    GetPropertyRequest, InternAtomRequest, PropertyMode, QueryExtensionRequest,
    WindowCreateRequest, decode_change_property, decode_create_window, decode_delete_property,
    decode_empty_request, decode_get_property, decode_intern_atom, decode_query_extension,
    decode_single_u32_request,
};
pub use dispatch::{
    CoreClient, CoreDispatchAction, CoreDispatchState, InitialCoreDispatch, InternAtomOutcome,
    PropertyChangeRequest, PropertyData, PropertyMutationOutcome, PropertyReadOutcome,
    PropertyReadReply, PropertyReadRequest, WindowCreateOutcome, WindowDestroyOutcome,
    WindowGeometryReply, WindowTreeReply, dispatch_core_request, dispatch_core_request_for_client,
    dispatch_initial_core_request,
};
pub use event::{
    CORE_EVENT_SIZE, ClientMessageData, ClientMessageEvent, ConfigureNotifyEvent, CrossingEvent,
    DestroyNotifyEvent, EventEncodeError, ExposeEvent, FocusEvent, GraphicsExposeEvent, InputEvent,
    MapNotifyEvent, NoExposeEvent, NotifyDetail, NotifyMode, PropertyNotifyEvent,
    PropertyNotifyState, SelectionClearEvent, SelectionNotifyEvent, SelectionRequestEvent,
    UnmapNotifyEvent, encode_client_message, encode_configure_notify, encode_crossing_event,
    encode_destroy_notify, encode_expose, encode_focus_event, encode_graphics_expose,
    encode_input_event, encode_map_notify, encode_no_expose, encode_property_notify,
    encode_selection_clear, encode_selection_notify, encode_selection_request, encode_unmap_notify,
};
pub use event_mask::{EventMask, StateMask};
pub use extension::{
    EXTENSION_REGISTRY, ExtensionAssignment, ExtensionDescriptor, ExtensionKind, ExtensionProfiles,
    ExtensionRegistry,
};
pub use lifecycle_request::{
    CONFIGURE_BORDER_WIDTH, CONFIGURE_HEIGHT, CONFIGURE_SIBLING, CONFIGURE_STACK_MODE,
    CONFIGURE_WIDTH, CONFIGURE_X, CONFIGURE_Y, ConfigureWindowRequest, CoreStackMode,
    KNOWN_CONFIGURE_MASK, LifecycleDecodeStatus, WindowLifecycleRequest, decode_configure_window,
    decode_map_subwindows, decode_map_window, decode_unmap_subwindows, decode_unmap_window,
};
pub use reply::{
    CORE_ERROR_SIZE, CORE_REPLY_SIZE, CoreError, ReplyBuildError, ReplyBuilder, encode_core_error,
};
pub use request::{
    BIG_REQUEST_HEADER_SIZE, CORE_REQUEST_HEADER_SIZE, FramedRequest,
    MAXIMUM_BIG_REQUEST_LENGTH_UNITS, MAXIMUM_BIG_REQUEST_SIZE, MAXIMUM_CORE_REQUEST_LENGTH_UNITS,
    MAXIMUM_CORE_REQUEST_SIZE, RequestFrameResult, RequestFrameStatus, RequestFramer,
    RequestFramerConfigError,
};
pub use screen::{SCREEN_MODEL, ScreenModel};
pub use selection_request::{
    ClientMessageWire, ConvertSelectionRequest, SelectionNotifyWire, SendEventHeader,
    SendEventPayload, SetSelectionOwnerRequest, UnmapNotifyWire, decode_client_message_data,
    decode_convert_selection, decode_get_selection_owner, decode_send_event_after_destination,
    decode_send_event_header, decode_set_selection_owner,
};
pub use setup::{
    DEFAULT_MAXIMUM_SETUP_SIZE, PROTOCOL_MAJOR, PROTOCOL_MINOR, ParseResult, ParseStatus,
    SETUP_REQUEST_HEADER_SIZE, SetupDecision, SetupEncodeError, SetupParser, SetupReplyConfig,
    SetupRequest, encode_setup_failure, encode_setup_success, evaluate_setup_request,
};
pub use window_request::{
    ChangeWindowAttributesRequest, WINDOW_ATTRIBUTE_MASK, WindowAttributeValues,
    decode_change_window_attributes, decode_window_attribute_values,
};
