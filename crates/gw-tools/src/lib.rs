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
pub use format::{format_all, format_outputs, format_windows};
pub use layout::{
    EditError, OutputEdit, apply_output_edit, apply_vrr_edit, parse_mode, parse_position,
    parse_scale, parse_transform, parse_vrr_policy,
};
pub use output_control::ControlError;
pub use snapshot::{OutputSnapshot, SnapshotError};

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum DiagnosticQuery {
    Outputs { include_vrr: bool },
    Windows { include_vrr: bool },
    All { include_vrr: bool },
    Vrr,
}

pub fn query_diagnostics(
    socket_path: &std::path::Path,
    query: DiagnosticQuery,
) -> Result<OutputSnapshot, ControlError> {
    let flags = match query {
        DiagnosticQuery::Outputs { include_vrr: false } => output_control::INVENTORY_QUERY_FLAGS,
        DiagnosticQuery::Windows { include_vrr: false } => output_control::WINDOW_QUERY_FLAGS,
        DiagnosticQuery::All { include_vrr: false } => output_control::ALL_QUERY_FLAGS,
        DiagnosticQuery::Outputs { include_vrr: true } => {
            output_control::ALL_VRR_DIAGNOSTIC_QUERY_FLAGS
        }
        DiagnosticQuery::Windows { include_vrr: true } | DiagnosticQuery::Vrr => {
            output_control::VRR_DIAGNOSTIC_QUERY_FLAGS
        }
        DiagnosticQuery::All { include_vrr: true } => {
            output_control::ALL_VRR_DIAGNOSTIC_QUERY_FLAGS
        }
    };
    let mut client = output_control::OutputControlClient::connect(socket_path)?;
    client.query(flags)
}

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
