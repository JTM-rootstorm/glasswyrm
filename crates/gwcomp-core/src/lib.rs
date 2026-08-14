//! Hardware-independent compositor policy and backend contracts.
//!
//! This crate deliberately contains no DRM access, renderer, or process code.
//! Its recorded and headless backends are software test doubles and cannot
//! provide evidence that physical hardware entered VRR.

#![forbid(unsafe_code)]

mod backend;
mod damage;
mod frame_set;
mod geometry;
mod headless;
mod model;
mod output;
mod replay;
mod scene;
mod scene_renderer;
mod scripted;
mod software;

pub use backend::OutputBackend;
pub use damage::DamageRegion;
pub use frame_set::{
    FrameSetError, OutputFrameResult, SoftwareFrameSet, calculate_frame_set_aggregate_hash,
};
pub use geometry::Rectangle;
pub use headless::HeadlessBackend;
pub use model::{
    BackendCapabilities, BackendError, BackendEvent, CapabilityOrigin, CommitDisposition,
    CommitRequest, OutputCommitResult, OutputInfo, OutputMode, PresentDisposition, PresentRequest,
    PresentResult, RejectionReason, ValidationResult, VrrCapability, VrrEligibility,
    VrrObservation, VrrPlan, VrrPolicy, VrrPolicyReason, VrrWindowPreference, plan_vrr,
};
pub use output::{
    DamageFilterFootprint, LogicalExtent, LogicalPoint, LogicalSamplePoint, MAXIMUM_OUTPUTS,
    MAXIMUM_SCALE_DENOMINATOR, MAXIMUM_TOTAL_OUTPUT_PIXELS, OutputMapping, OutputTransform,
    PhysicalExtent, PhysicalPoint, PhysicalRectangle, RationalScale, derive_logical_dimension,
    inverse_transform_boundary, inverse_transform_rectangle, is_reduced,
    map_logical_damage_to_native, map_logical_point_to_native, map_logical_rectangle_to_native,
    map_native_pixel_center_to_logical, transform_boundary, transform_rectangle,
    transformed_physical_extent, valid_output_mapping, valid_output_scale,
};
pub use replay::{PeerEpoch, QueuedRejection, ReplayLedger, RestartReplay};
pub use scene::{
    Scene, SceneError, SceneOutput, SceneSurface, SurfaceBuffer, SurfaceOutputMembership,
    SurfacePresentation,
};
pub use scene_renderer::{
    OutputSoftwareRenderMetrics, SamplingFilter, SoftwareRenderError, SoftwareRenderRequest,
    SoftwareRenderResult, render_software_scene, select_sampling_filter,
};
pub use scripted::{RecordedMutation, ScriptedMockBackend};
pub use software::{
    FULL_OPACITY, FrameHashMeasurement, FramebufferView, ImageView, OutputSpec, Pixel, PixelFormat,
    RenderResult, SoftwareFrame, SoftwareFrameError, apply_opacity, blend, clear, composite,
    hash_visible_xrgb8888, hash_visible_xrgb8888_measured, pack_xrgb8888, source_over,
    unpack_argb8888, unpack_xrgb8888,
};
