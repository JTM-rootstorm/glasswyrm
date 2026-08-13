//! Hardware-independent compositor policy and backend contracts.
//!
//! This crate deliberately contains no DRM access, renderer, or process code.
//! Its recorded and headless backends are software test doubles and cannot
//! provide evidence that physical hardware entered VRR.

#![forbid(unsafe_code)]

mod backend;
mod damage;
mod geometry;
mod headless;
mod model;
mod replay;
mod scripted;
mod software;

pub use backend::OutputBackend;
pub use damage::DamageRegion;
pub use geometry::Rectangle;
pub use headless::HeadlessBackend;
pub use model::{
    BackendCapabilities, BackendError, BackendEvent, CapabilityOrigin, CommitDisposition,
    CommitRequest, OutputCommitResult, OutputInfo, OutputMode, PresentDisposition, PresentRequest,
    PresentResult, RejectionReason, ValidationResult, VrrCapability, VrrEligibility,
    VrrObservation, VrrPlan, VrrPolicy, VrrPolicyReason, VrrWindowPreference, plan_vrr,
};
pub use replay::{PeerEpoch, QueuedRejection, ReplayLedger, RestartReplay};
pub use scripted::{RecordedMutation, ScriptedMockBackend};
pub use software::{
    FULL_OPACITY, FrameHashMeasurement, FramebufferView, ImageView, OutputSpec, Pixel, PixelFormat,
    RenderResult, SoftwareFrame, SoftwareFrameError, apply_opacity, blend, clear, composite,
    hash_visible_xrgb8888, hash_visible_xrgb8888_measured, pack_xrgb8888, source_over,
    unpack_argb8888, unpack_xrgb8888,
};
