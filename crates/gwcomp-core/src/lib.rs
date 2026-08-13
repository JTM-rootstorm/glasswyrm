//! Hardware-independent compositor policy and backend contracts.
//!
//! This crate deliberately contains no DRM access, renderer, or process code.
//! Its recorded and headless backends are software test doubles and cannot
//! provide evidence that physical hardware entered VRR.

#![forbid(unsafe_code)]

mod backend;
mod headless;
mod model;
mod replay;
mod scripted;

pub use backend::OutputBackend;
pub use headless::HeadlessBackend;
pub use model::{
    BackendCapabilities, BackendError, BackendEvent, CapabilityOrigin, CommitDisposition,
    CommitRequest, OutputCommitResult, OutputInfo, OutputMode, PresentDisposition, PresentRequest,
    PresentResult, RejectionReason, ValidationResult, VrrCapability, VrrEligibility,
    VrrObservation, VrrPlan, VrrPolicy, VrrPolicyReason, plan_vrr,
};
pub use replay::{QueuedRejection, ReplayLedger, RestartReplay};
pub use scripted::{RecordedMutation, ScriptedMockBackend};
