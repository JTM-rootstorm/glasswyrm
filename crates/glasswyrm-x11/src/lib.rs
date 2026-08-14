//! Hardware- and transport-independent X11 protocol codecs for Glasswyrm.

#![forbid(unsafe_code)]

mod atoms;
mod byte_order;
mod core;
mod reply;
mod request;
mod screen;
mod setup;
mod wire;

pub use atoms::{LAST_PREDEFINED_ATOM, NONE_ATOM, PREDEFINED_ATOMS, PredefinedAtom};
pub use byte_order::ByteOrder;
pub use core::{CoreErrorCode, CoreOpcode, wire_sequence};
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
pub use setup::{
    DEFAULT_MAXIMUM_SETUP_SIZE, PROTOCOL_MAJOR, PROTOCOL_MINOR, ParseResult, ParseStatus,
    SETUP_REQUEST_HEADER_SIZE, SetupDecision, SetupEncodeError, SetupParser, SetupReplyConfig,
    SetupRequest, encode_setup_failure, encode_setup_success, evaluate_setup_request,
};
