//! Dependency-free support for deterministic Glasswyrm process tests.
//!
//! This crate deliberately keeps process lifecycle, readiness, fixture, and
//! failure-artifact behavior independent of the production crates. Hardware is
//! never accessed by these helpers.

#![forbid(unsafe_code)]

mod failure;
mod fixture;
mod id;
mod poll;
mod process;
mod runtime;

pub use failure::{Attachment, FailureBundle};
pub use fixture::{
    FixtureDir, Sha256Digest, sha256, sha256_file, verify_sha256, write_checksum_manifest,
};
pub use id::{TestId, deterministic_seed};
pub use poll::{
    Clock, FakeClock, PollError, SystemClock, poll_until, poll_until_with_clock, wait_for_path,
    wait_for_unix_socket,
};
pub use process::{ExitInfo, ProcessSpec, RestartableProcess, Signal, SupervisedChild};
pub use runtime::RuntimeDir;
