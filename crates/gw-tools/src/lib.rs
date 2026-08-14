//! Shared implementation for Glasswyrm command-line tools.

mod client;
mod control_format;
mod format;
mod layout;
mod output_control;
mod snapshot;
mod unix;

pub use client::{QueryError, query_outputs};
pub use control_format::{format_acknowledgement, format_vrr};
pub use format::format_outputs;
pub use layout::{
    EditError, OutputEdit, apply_output_edit, apply_vrr_edit, parse_mode, parse_position,
    parse_scale, parse_transform, parse_vrr_policy,
};
pub use output_control::ControlError;
pub use snapshot::{OutputSnapshot, SnapshotError};

pub fn control_outputs(
    socket_path: &std::path::Path,
    edit_vrr: bool,
) -> Result<(OutputControlSession, OutputSnapshot), ControlError> {
    let mut client = output_control::OutputControlClient::connect(socket_path)?;
    let flags = if edit_vrr {
        output_control::VRR_CONFIGURATION_QUERY_FLAGS
    } else {
        output_control::INVENTORY_QUERY_FLAGS
    };
    let snapshot = client.query(flags)?;
    Ok((OutputControlSession(client), snapshot))
}

pub struct OutputControlSession(output_control::OutputControlClient);

impl OutputControlSession {
    pub fn commit(
        &mut self,
        snapshot: &OutputSnapshot,
    ) -> Result<gw_wire::OutputConfigurationAcknowledged, ControlError> {
        self.0.commit(snapshot)
    }

    pub fn query_vrr(&mut self) -> Result<OutputSnapshot, ControlError> {
        self.0.query(output_control::VRR_CONFIGURATION_QUERY_FLAGS)
    }
}
