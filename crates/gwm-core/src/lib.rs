//! Deterministic window-management policy for Glasswyrm.
//!
//! This crate has no process, socket, X11 parsing, renderer, or platform code.
//! Inputs are complete snapshots and outputs are reproducible policy decisions.

#![forbid(unsafe_code)]

mod geometry;
mod model;
mod policy;
mod vrr;

pub use geometry::{OutputSelection, initial_placement, retain_visible_pixel, select_output};
pub use model::{
    AppliedState, Context, DecorationPreference, EvaluationError, OutputContext, RawState,
    RawWindow, Rectangle, StackMode, TriState, WindowOutputHint, WindowState, WindowType,
    WorkspaceId,
};
pub use policy::{PolicyState, evaluate};
pub use vrr::{
    VrrReason, VrrWindowClassification, VrrWindowInput, VrrWindowPreference,
    classify_borderless_fullscreen, classify_vrr_window,
};
