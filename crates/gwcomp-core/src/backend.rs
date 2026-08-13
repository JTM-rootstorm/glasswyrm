use crate::{
    BackendCapabilities, BackendError, BackendEvent, CommitRequest, OutputCommitResult, OutputInfo,
    PresentRequest, PresentResult, ValidationResult,
};
use gw_types::OutputId;

/// The compositor-facing output boundary.
///
/// The shape follows the legacy presentation backend: discovery and
/// capability reads happen before a validate/commit/present sequence, while
/// asynchronous completion is returned through event polling. Platform code
/// may implement this trait later without leaking platform handles into core
/// policy.
pub trait OutputBackend {
    fn enumerate_outputs(&self) -> Vec<OutputInfo>;

    fn read_capabilities(&self, output_id: OutputId) -> Result<BackendCapabilities, BackendError>;

    fn validate_commit(&mut self, request: &CommitRequest) -> ValidationResult;

    fn commit(&mut self, request: &CommitRequest) -> OutputCommitResult;

    fn present(&mut self, request: &PresentRequest) -> PresentResult;

    fn poll_event(&mut self) -> Option<BackendEvent>;
}
