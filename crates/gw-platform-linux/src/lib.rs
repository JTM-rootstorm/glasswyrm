//! Safe, dependency-light wrappers around the Linux facilities Glasswyrm uses.
//!
//! Raw ABI declarations stay in `gw-sys`. This crate owns descriptors and
//! mappings through RAII and validates lengths, flags, and return values before
//! exposing them to runtime crates.

#![cfg(target_os = "linux")]

mod event;
mod fd;
mod memory;
mod polling;
mod signal;
mod socket;

pub use event::{EventFd, SignalTokenError, TimerFd};
pub use fd::{DescriptorFlags, HardenedFd, descriptor_flags, harden_descriptor};
pub use memory::{MapAccess, Mapping, Memfd, SealSet};
pub use polling::{PollEvents, PollInterest, PollTarget, poll};
pub use signal::{Signal, SignalEvent, SignalFd};
pub use socket::{PeerCredentials, UnixSocket, UnixSocketType, peer_credentials};
