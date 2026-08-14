//! Linux GWIPC transport and handshake state.
//!
//! GWIPC uses local `SOCK_SEQPACKET` sockets so each wire record remains paired
//! with its ancillary file descriptors. Received descriptors are immediately
//! wrapped in [`OwnedFd`](std::os::fd::OwnedFd); dropping a message closes every
//! descriptor the caller did not deliberately move out of it.

#![cfg_attr(not(target_os = "linux"), allow(dead_code))]

#[cfg(not(target_os = "linux"))]
compile_error!("gw-ipc currently supports Linux only");

mod application;
mod endpoint;
mod handshake;
mod transport;

pub use application::{ApplicationError, ApplicationValidator, MessageDirection, SnapshotState};
pub use endpoint::EndpointListener;
pub use handshake::{
    HandshakeConfig, HandshakeError, HandshakeRecord, NegotiatedPeer, ServerHandshakeResponse,
    accept_hello, make_hello, validate_server_response,
};
pub use transport::{
    HARD_MAXIMUM_FDS, HARD_MAXIMUM_PAYLOAD, ReceivedRecord, Transport, TransportError,
    TransportLimits,
};
