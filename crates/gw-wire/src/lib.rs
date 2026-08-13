//! Dependency-light GWIPC wire primitives.
//!
//! The codec intentionally does not serialize Rust memory layouts. All numeric
//! values are read and written explicitly in the legacy GWIPC byte order.

#![forbid(unsafe_code)]

pub mod compositor;
mod control;
mod envelope;
mod input;
mod lifecycle;
pub mod output;
mod policy;
mod primitive;
mod session;
pub mod vrr;

pub use control::{
    ControlDecodeError, ControlEncodeError, Hello, Ping, Pong, ProtocolError, Reject,
    SnapshotAbort, SnapshotBegin, SnapshotEnd, Welcome, decode_hello, decode_ping, decode_pong,
    decode_protocol_error, decode_reject, decode_snapshot_abort, decode_snapshot_begin,
    decode_snapshot_end, decode_welcome, encode_hello, encode_ping, encode_pong,
    encode_protocol_error, encode_reject, encode_snapshot_abort, encode_snapshot_begin,
    encode_snapshot_end, encode_welcome,
};
pub use envelope::{
    DecodeLimits, Envelope, EnvelopeDecodeError, GWIPC_ENVELOPE_SIZE, GWIPC_MAGIC, decode_envelope,
    encode_envelope,
};
pub use input::*;
pub use lifecycle::*;
pub use output::*;
pub use policy::*;
pub use primitive::{ByteReader, ByteWriter, PrimitiveDecodeError, PrimitiveEncodeError};
pub use session::*;
