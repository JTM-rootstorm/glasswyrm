//! Dependency-light GWIPC wire primitives.
//!
//! The codec intentionally does not serialize Rust memory layouts. All numeric
//! values are read and written explicitly in the legacy GWIPC byte order.

#![forbid(unsafe_code)]

mod control;
mod envelope;
mod primitive;

pub use control::{
    ControlDecodeError, ControlEncodeError, Hello, Reject, Welcome, decode_hello, decode_reject,
    decode_welcome, encode_hello, encode_reject, encode_welcome,
};
pub use envelope::{
    DecodeLimits, Envelope, EnvelopeDecodeError, GWIPC_ENVELOPE_SIZE, GWIPC_MAGIC, decode_envelope,
    encode_envelope,
};
pub use primitive::{ByteReader, ByteWriter, PrimitiveDecodeError, PrimitiveEncodeError};
