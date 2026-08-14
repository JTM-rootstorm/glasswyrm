//! Shared implementation for Glasswyrm command-line tools.

mod client;
mod format;
mod snapshot;
mod unix;

pub use client::{QueryError, query_outputs};
pub use format::format_outputs;
pub use snapshot::{OutputSnapshot, SnapshotError};
