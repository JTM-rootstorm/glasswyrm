use std::fmt;
use std::io;
use std::os::fd::{AsFd, AsRawFd};
use std::path::Path;
use std::time::{Duration, Instant};

use gw_ipc::{
    HandshakeConfig, Transport, TransportError, TransportLimits, make_hello,
    validate_server_response,
};
use gw_types::{Capabilities, MessageFlags, MessageType, Role, Sequence};
use gw_wire::{Envelope, OutputStateQuery, encode_output_state_query};

use crate::OutputSnapshot;
use crate::snapshot::{ConsumeOutcome, OUTPUT_QUERY_FLAGS, SnapshotDecoder};
use crate::unix::{connect_seqpacket, pause, wait_readable, wait_writable};

const MAXIMUM_PAYLOAD: u32 = 4096;
const OPERATION_TIMEOUT: Duration = Duration::from_secs(5);
const RETRY_BACKOFF: Duration = Duration::from_millis(10);

#[derive(Debug)]
pub struct QueryError(String);

impl QueryError {
    fn detail(detail: impl Into<String>) -> Self {
        Self(detail.into())
    }
}

impl fmt::Display for QueryError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(&self.0)
    }
}

impl std::error::Error for QueryError {}

pub fn query_outputs(socket_path: &Path) -> Result<OutputSnapshot, QueryError> {
    let deadline = Instant::now() + OPERATION_TIMEOUT;
    let fd = connect_seqpacket(socket_path.as_os_str(), deadline).map_err(|error| {
        QueryError::detail(format!(
            "could not connect to output control socket: {error}"
        ))
    })?;
    let limits = TransportLimits::new(MAXIMUM_PAYLOAD, 0)
        .map_err(|error| QueryError::detail(error.to_string()))?;
    let mut transport = Transport::from_owned_fd(fd, limits)
        .map_err(|error| QueryError::detail(error.to_string()))?;
    let config = handshake_config(limits);
    let hello = make_hello(&config).map_err(|error| QueryError::detail(error.to_string()))?;
    send_until(&transport, &hello.envelope, &hello.payload, deadline)?;
    let welcome = receive_until(
        &transport,
        deadline,
        "establishing the output control connection",
    )?;
    let peer = validate_server_response(&welcome, &config)
        .map_err(|error| QueryError::detail(error.to_string()))?;
    transport
        .set_limits(peer.limits)
        .map_err(|error| QueryError::detail(error.to_string()))?;

    let mut request_id = 1_u64;
    let mut request_sequence = 2_u64;
    let mut expected_incoming_sequence = 2_u64;
    let mut saw_busy = false;
    while Instant::now() < deadline {
        let payload = encode_output_state_query(&OutputStateQuery {
            query_id: request_id,
            flags: OUTPUT_QUERY_FLAGS,
        });
        let sequence = Sequence::new(request_sequence);
        let mut envelope = Envelope::request(
            MessageType::OUTPUT_STATE_QUERY,
            sequence,
            payload.len() as u32,
        );
        envelope.flags = MessageFlags::ACK_REQUIRED;
        send_until(&transport, &envelope, &payload, deadline)?;

        let mut decoder = SnapshotDecoder::new(request_id, sequence);
        loop {
            let record = receive_until(
                &transport,
                deadline,
                "waiting for a complete output snapshot",
            )?;
            if record.envelope.sequence.get() != expected_incoming_sequence {
                return Err(QueryError::detail(
                    "output control connection failed: out-of-order sequence",
                ));
            }
            expected_incoming_sequence = expected_incoming_sequence
                .checked_add(1)
                .ok_or_else(|| QueryError::detail("output control sequence was exhausted"))?;
            match decoder
                .consume(&record)
                .map_err(|error| QueryError::detail(error.to_string()))?
            {
                ConsumeOutcome::Pending => {}
                ConsumeOutcome::Complete => return Ok(decoder.take()),
                ConsumeOutcome::Busy => {
                    saw_busy = true;
                    break;
                }
            }
        }
        request_id = request_id
            .checked_add(1)
            .ok_or_else(|| QueryError::detail("output query identity was exhausted"))?;
        request_sequence = request_sequence
            .checked_add(1)
            .ok_or_else(|| QueryError::detail("output control sequence was exhausted"))?;
        pause(RETRY_BACKOFF, deadline);
    }
    Err(QueryError::detail(if saw_busy {
        "timed out waiting for output snapshot readiness"
    } else {
        "timed out waiting for a complete output snapshot"
    }))
}

fn handshake_config(limits: TransportLimits) -> HandshakeConfig {
    let offered = Capabilities::SNAPSHOTS
        .with(Capabilities::OUTPUT_STATE)
        .with(Capabilities::OUTPUT_CONTROL)
        .with(Capabilities::SURFACE_STATE)
        .with(Capabilities::WINDOW_LIFECYCLE)
        .with(Capabilities::SURFACE_OUTPUT_MEMBERSHIP)
        .with(Capabilities::SCALE_METADATA)
        .with(Capabilities::VRR_METADATA)
        .with(Capabilities::VRR_POLICY)
        .with(Capabilities::PRESENTATION_TIMING);
    let mut config = HandshakeConfig::new(
        Role::DiagnosticTool,
        *b"gwinfo-rust-v001",
        "glasswyrm-output-tool",
    )
    .allow_peer_role(Role::ProtocolServer)
    .offer(offered)
    .require_peer(Capabilities::OUTPUT_CONTROL);
    config.limits = limits;
    config
}

fn send_until(
    transport: &Transport,
    envelope: &Envelope,
    payload: &[u8],
    deadline: Instant,
) -> Result<(), QueryError> {
    loop {
        match transport.send(envelope, payload, &[]) {
            Ok(()) => return Ok(()),
            Err(TransportError::Io(error)) if error.kind() == io::ErrorKind::WouldBlock => {
                wait_writable(transport.as_fd().as_raw_fd(), deadline).map_err(map_poll_error)?;
            }
            Err(error) => return Err(map_transport_error(error)),
        }
    }
}

fn receive_until(
    transport: &Transport,
    deadline: Instant,
    timeout_action: &'static str,
) -> Result<gw_ipc::ReceivedRecord, QueryError> {
    loop {
        match transport.receive() {
            Ok(record) => return Ok(record),
            Err(TransportError::Io(error)) if error.kind() == io::ErrorKind::WouldBlock => {
                match wait_readable(transport.as_fd().as_raw_fd(), deadline) {
                    Ok(()) => {}
                    Err(error) if error.kind() == io::ErrorKind::TimedOut => {
                        return Err(QueryError::detail(format!("timed out {timeout_action}")));
                    }
                    Err(error) => return Err(map_poll_error(error)),
                }
            }
            Err(error) => return Err(map_transport_error(error)),
        }
    }
}

fn map_poll_error(error: io::Error) -> QueryError {
    QueryError::detail(format!("polling the output control socket failed: {error}"))
}

fn map_transport_error(error: TransportError) -> QueryError {
    if matches!(error, TransportError::Disconnected) {
        QueryError::detail("output control connection failed: disconnected")
    } else {
        QueryError::detail(format!("output control connection failed: {error}"))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn handshake_matches_the_legacy_diagnostic_profile() {
        let config = handshake_config(TransportLimits::new(4096, 0).unwrap());
        assert_eq!(config.local_role, Role::DiagnosticTool);
        assert_eq!(config.label, "glasswyrm-output-tool");
        assert!(
            config
                .offered_capabilities
                .contains(Capabilities::OUTPUT_CONTROL)
        );
        assert!(
            config
                .required_peer_capabilities
                .contains(Capabilities::OUTPUT_CONTROL)
        );
        assert_eq!(config.limits, TransportLimits::new(4096, 0).unwrap());
    }
}
