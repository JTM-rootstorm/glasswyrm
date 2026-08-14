use std::fmt;
use std::io;
use std::os::fd::{AsFd, AsRawFd};
use std::path::Path;
use std::time::{Duration, Instant};

use gw_ipc::{
    ApplicationValidator, HandshakeConfig, Transport, TransportError, TransportLimits, make_hello,
    validate_server_response,
};
use gw_types::{
    Capabilities, Generation, MessageFlags, MessageType, Role, Sequence, SnapshotDomain, SnapshotId,
};
use gw_wire::compositor::encode_output_upsert;
use gw_wire::vrr::{OutputVrrPolicyUpsert, encode_output_vrr_policy_upsert};
use gw_wire::{
    Envelope, OutputConfigurationAcknowledged, OutputConfigurationCommit, OutputStateQuery,
    SnapshotBegin, SnapshotEnd, decode_output_configuration_acknowledged,
    encode_output_configuration_commit, encode_output_state_query, encode_snapshot_begin,
    encode_snapshot_end,
};

use crate::OutputSnapshot;
use crate::snapshot::{
    ConsumeOutcome, OUTPUT_QUERY_FLAGS, OUTPUT_QUERY_VRR, OUTPUT_QUERY_WINDOWS, SnapshotDecoder,
};
use crate::unix::{connect_seqpacket, pause, wait_readable, wait_writable};

const MAXIMUM_PAYLOAD: u32 = 4096;
const OPERATION_TIMEOUT: Duration = Duration::from_secs(5);
const RETRY_BACKOFF: Duration = Duration::from_millis(10);

pub(crate) const INVENTORY_QUERY_FLAGS: u32 = OUTPUT_QUERY_FLAGS;
pub(crate) const WINDOW_QUERY_FLAGS: u32 = OUTPUT_QUERY_WINDOWS;
pub(crate) const ALL_QUERY_FLAGS: u32 = OUTPUT_QUERY_FLAGS | OUTPUT_QUERY_WINDOWS;
pub(crate) const VRR_DIAGNOSTIC_QUERY_FLAGS: u32 =
    (OUTPUT_QUERY_FLAGS & !(1 << 1)) | OUTPUT_QUERY_WINDOWS | OUTPUT_QUERY_VRR;
pub(crate) const ALL_VRR_DIAGNOSTIC_QUERY_FLAGS: u32 =
    OUTPUT_QUERY_FLAGS | OUTPUT_QUERY_WINDOWS | OUTPUT_QUERY_VRR;
pub(crate) const VRR_CONFIGURATION_QUERY_FLAGS: u32 =
    OUTPUT_QUERY_FLAGS | OUTPUT_QUERY_WINDOWS | OUTPUT_QUERY_VRR;

#[derive(Debug)]
pub struct ControlError(String);

impl ControlError {
    fn detail(detail: impl Into<String>) -> Self {
        Self(detail.into())
    }
}

impl fmt::Display for ControlError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(&crate::format::visible_text(&self.0))
    }
}

impl std::error::Error for ControlError {}

pub(crate) struct OutputControlClient {
    transport: Transport,
    validator: ApplicationValidator,
    next_outgoing_sequence: u64,
    next_incoming_sequence: u64,
    next_request_id: u64,
}

impl OutputControlClient {
    pub(crate) fn connect(socket_path: &Path) -> Result<Self, ControlError> {
        let deadline = Instant::now() + OPERATION_TIMEOUT;
        let fd = connect_seqpacket(socket_path.as_os_str(), deadline).map_err(|error| {
            ControlError::detail(format!(
                "could not connect to output control socket: {error}"
            ))
        })?;
        let limits = TransportLimits::new(MAXIMUM_PAYLOAD, 0)
            .map_err(|error| ControlError::detail(error.to_string()))?;
        let mut transport = Transport::from_owned_fd(fd, limits)
            .map_err(|error| ControlError::detail(error.to_string()))?;
        let config = handshake_config(limits);
        let hello = make_hello(&config).map_err(|error| ControlError::detail(error.to_string()))?;
        send_until(&transport, &hello.envelope, &hello.payload, deadline)?;
        let welcome = receive_until(
            &transport,
            deadline,
            "establishing the output control connection",
        )?;
        let peer = validate_server_response(&welcome, &config)
            .map_err(|error| ControlError::detail(error.to_string()))?;
        transport
            .set_limits(peer.limits)
            .map_err(|error| ControlError::detail(error.to_string()))?;
        Ok(Self {
            transport,
            validator: ApplicationValidator::established(
                Role::DiagnosticTool,
                peer.role,
                peer.capabilities,
                2048,
            ),
            next_outgoing_sequence: 2,
            next_incoming_sequence: 2,
            next_request_id: 1,
        })
    }

    pub(crate) fn query(&mut self, flags: u32) -> Result<OutputSnapshot, ControlError> {
        let deadline = Instant::now() + OPERATION_TIMEOUT;
        let mut saw_busy = false;
        while Instant::now() < deadline {
            let request_id = self.take_request_id("output query identity was exhausted")?;
            let sequence = self.take_outgoing_sequence()?;
            let payload = encode_output_state_query(&OutputStateQuery {
                query_id: request_id,
                flags,
            });
            let mut envelope = Envelope::request(
                MessageType::OUTPUT_STATE_QUERY,
                sequence,
                payload.len() as u32,
            );
            envelope.flags = MessageFlags::ACK_REQUIRED;
            self.validate_and_send(&envelope, &payload, deadline)?;

            let mut decoder = SnapshotDecoder::new(request_id, sequence, flags);
            loop {
                let record =
                    self.receive_next(deadline, "waiting for a complete output snapshot")?;
                match decoder
                    .consume(&record)
                    .map_err(|error| ControlError::detail(error.to_string()))?
                {
                    ConsumeOutcome::Pending => {}
                    ConsumeOutcome::Complete => {
                        let snapshot = decoder.take();
                        validate_snapshot(&snapshot)?;
                        return Ok(snapshot);
                    }
                    ConsumeOutcome::Busy => {
                        saw_busy = true;
                        break;
                    }
                }
            }
            pause(RETRY_BACKOFF, deadline);
        }
        Err(ControlError::detail(if saw_busy {
            "timed out waiting for output snapshot readiness"
        } else {
            "timed out waiting for a complete output snapshot"
        }))
    }

    pub(crate) fn commit(
        &mut self,
        snapshot: &OutputSnapshot,
    ) -> Result<OutputConfigurationAcknowledged, ControlError> {
        if snapshot.outputs.is_empty() {
            return Err(ControlError::detail(
                "output configuration contains no outputs",
            ));
        }
        if snapshot.vrr_queried && snapshot.vrr_policies.len() != snapshot.outputs.len() {
            return Err(ControlError::detail(
                "VRR configuration requires one policy for every output",
            ));
        }
        let configuration_id =
            self.take_request_id("output configuration identity was exhausted")?;
        let policy_count = if snapshot.vrr_queried {
            snapshot.vrr_policies.len()
        } else {
            0
        };
        let item_count = snapshot
            .outputs
            .len()
            .checked_add(policy_count)
            .and_then(|count| u32::try_from(count).ok())
            .ok_or_else(|| ControlError::detail("output configuration is too large"))?;
        let deadline = Instant::now() + OPERATION_TIMEOUT;

        self.send_record(
            MessageType::SNAPSHOT_BEGIN,
            MessageFlags::default(),
            encode_snapshot_begin(SnapshotBegin {
                snapshot_id: SnapshotId::new(configuration_id),
                domain: SnapshotDomain::Outputs,
                flags: 0,
                generation: Generation::new(snapshot.generation),
                expected_item_count: item_count,
            }),
            deadline,
        )?;
        for output in snapshot.outputs.values() {
            self.send_record(
                MessageType::OUTPUT_UPSERT,
                MessageFlags::SNAPSHOT_ITEM,
                encode_output_upsert(output),
                deadline,
            )?;
        }
        if snapshot.vrr_queried {
            for (&output_id, &mode) in &snapshot.vrr_policies {
                self.send_record(
                    MessageType::OUTPUT_VRR_POLICY_UPSERT,
                    MessageFlags::SNAPSHOT_ITEM,
                    encode_output_vrr_policy_upsert(&OutputVrrPolicyUpsert {
                        output_id,
                        mode,
                        flags: 0,
                    }),
                    deadline,
                )?;
            }
        }
        self.send_record(
            MessageType::SNAPSHOT_END,
            MessageFlags::default(),
            encode_snapshot_end(SnapshotEnd {
                snapshot_id: SnapshotId::new(configuration_id),
                generation: Generation::new(snapshot.generation),
                actual_item_count: item_count,
            }),
            deadline,
        )?;
        let commit_sequence = self.take_outgoing_sequence()?;
        let payload = encode_output_configuration_commit(&OutputConfigurationCommit {
            configuration_id,
            base_generation: snapshot.generation,
            primary_output_id: snapshot.primary_output_id,
            flags: 0,
        });
        let mut envelope = Envelope::request(
            MessageType::OUTPUT_CONFIGURATION_COMMIT,
            commit_sequence,
            payload.len() as u32,
        );
        envelope.flags = MessageFlags::ACK_REQUIRED;
        self.validate_and_send(&envelope, &payload, deadline)?;

        let record =
            self.receive_next(deadline, "waiting for output configuration acknowledgement")?;
        let acknowledgement =
            decode_output_configuration_acknowledged(&record.payload).map_err(|_| {
                ControlError::detail("control server sent an invalid configuration acknowledgement")
            })?;
        if record.envelope.message_type != MessageType::OUTPUT_CONFIGURATION_ACKNOWLEDGED
            || record.envelope.flags != MessageFlags::REPLY
            || record.envelope.reply_to != commit_sequence
            || acknowledgement.request_id != configuration_id
        {
            return Err(ControlError::detail(
                "control server sent an invalid configuration acknowledgement",
            ));
        }
        Ok(acknowledgement)
    }

    fn send_record(
        &mut self,
        message_type: MessageType,
        flags: MessageFlags,
        payload: Vec<u8>,
        deadline: Instant,
    ) -> Result<(), ControlError> {
        let sequence = self.take_outgoing_sequence()?;
        let mut envelope = Envelope::request(message_type, sequence, payload.len() as u32);
        envelope.flags = flags;
        self.validate_and_send(&envelope, &payload, deadline)
    }

    fn receive_next(
        &mut self,
        deadline: Instant,
        action: &'static str,
    ) -> Result<gw_ipc::ReceivedRecord, ControlError> {
        let record = receive_until(&self.transport, deadline, action)?;
        self.validator
            .validate_incoming(&record.envelope, &record.payload, record.fds.len())
            .map_err(|error| {
                ControlError::detail(format!(
                    "output control connection failed validation: {error}"
                ))
            })?;
        if record.envelope.sequence.get() != self.next_incoming_sequence {
            return Err(ControlError::detail(
                "output control connection failed: out-of-order sequence",
            ));
        }
        self.next_incoming_sequence = self
            .next_incoming_sequence
            .checked_add(1)
            .ok_or_else(|| ControlError::detail("output control sequence was exhausted"))?;
        Ok(record)
    }

    fn validate_and_send(
        &mut self,
        envelope: &Envelope,
        payload: &[u8],
        deadline: Instant,
    ) -> Result<(), ControlError> {
        self.validator
            .validate_outgoing(envelope, payload, 0)
            .map_err(|error| {
                ControlError::detail(format!("output control request failed validation: {error}"))
            })?;
        send_until(&self.transport, envelope, payload, deadline)
    }

    fn take_request_id(&mut self, exhausted: &'static str) -> Result<u64, ControlError> {
        let current = self.next_request_id;
        self.next_request_id = current
            .checked_add(1)
            .ok_or_else(|| ControlError::detail(exhausted))?;
        Ok(current)
    }

    fn take_outgoing_sequence(&mut self) -> Result<Sequence, ControlError> {
        let current = self.next_outgoing_sequence;
        self.next_outgoing_sequence = current
            .checked_add(1)
            .ok_or_else(|| ControlError::detail("output control sequence was exhausted"))?;
        Ok(Sequence::new(current))
    }
}

fn validate_snapshot(snapshot: &OutputSnapshot) -> Result<(), ControlError> {
    if !snapshot.vrr_queried {
        return Ok(());
    }
    for output_id in snapshot.outputs.keys() {
        let Some(policy) = snapshot.vrr_policies.get(output_id) else {
            return Err(ControlError::detail(
                "VRR snapshot omits capability, policy, or effective state",
            ));
        };
        let Some(state) = snapshot.vrr_outputs.get(output_id) else {
            return Err(ControlError::detail(
                "VRR snapshot omits capability, policy, or effective state",
            ));
        };
        if !snapshot.vrr_capabilities.contains_key(output_id) {
            return Err(ControlError::detail(
                "VRR snapshot omits capability, policy, or effective state",
            ));
        }
        if *policy != state.requested_mode {
            return Err(ControlError::detail(
                "VRR policy and effective state disagree on requested mode",
            ));
        }
    }
    for output_id in snapshot.vrr_capabilities.keys() {
        if !snapshot.outputs.contains_key(output_id) {
            return Err(ControlError::detail(
                "VRR capability references an unknown output",
            ));
        }
    }
    for output_id in snapshot.vrr_policies.keys() {
        if !snapshot.outputs.contains_key(output_id) {
            return Err(ControlError::detail(
                "VRR policy references an unknown output",
            ));
        }
    }
    for output_id in snapshot.vrr_outputs.keys() {
        if !snapshot.outputs.contains_key(output_id) {
            return Err(ControlError::detail(
                "VRR effective state references an unknown output",
            ));
        }
    }
    for output_id in snapshot.vrr_timings.keys() {
        if !snapshot.outputs.contains_key(output_id) {
            return Err(ControlError::detail(
                "VRR timing references an unknown output",
            ));
        }
    }
    for (&window_id, window) in &snapshot.windows {
        let Some(state) = snapshot.vrr_windows.get(&window_id) else {
            return Err(ControlError::detail(
                "VRR snapshot omits queried window state",
            ));
        };
        if state.surface_id != window.surface_id || !snapshot.outputs.contains_key(&state.output_id)
        {
            return Err(ControlError::detail(
                "VRR window state does not match the queried scene",
            ));
        }
    }
    for (&window_id, state) in &snapshot.vrr_windows {
        if snapshot
            .windows
            .get(&window_id)
            .is_none_or(|window| window.surface_id != state.surface_id)
            || !snapshot.outputs.contains_key(&state.output_id)
        {
            return Err(ControlError::detail(
                "VRR window state does not match the queried scene",
            ));
        }
    }
    for (&output_id, state) in &snapshot.vrr_outputs {
        if state.candidate_window_id == 0 && state.candidate_surface_id == 0 {
            continue;
        }
        if state.candidate_window_id == 0
            || state.candidate_surface_id == 0
            || !snapshot
                .vrr_windows
                .get(&state.candidate_window_id)
                .is_some_and(|window| {
                    window.surface_id == state.candidate_surface_id && window.output_id == output_id
                })
        {
            return Err(ControlError::detail(
                "VRR candidate does not match the queried window state",
            ));
        }
    }
    Ok(())
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
        *b"gwout-rust-v0001",
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
) -> Result<(), ControlError> {
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
) -> Result<gw_ipc::ReceivedRecord, ControlError> {
    loop {
        match transport.receive() {
            Ok(record) => return Ok(record),
            Err(TransportError::Io(error)) if error.kind() == io::ErrorKind::WouldBlock => {
                match wait_readable(transport.as_fd().as_raw_fd(), deadline) {
                    Ok(()) => {}
                    Err(error) if error.kind() == io::ErrorKind::TimedOut => {
                        return Err(ControlError::detail(format!("timed out {timeout_action}")));
                    }
                    Err(error) => return Err(map_poll_error(error)),
                }
            }
            Err(error) => return Err(map_transport_error(error)),
        }
    }
}

fn map_poll_error(error: io::Error) -> ControlError {
    ControlError::detail(format!("polling the output control socket failed: {error}"))
}

fn map_transport_error(error: TransportError) -> ControlError {
    if matches!(error, TransportError::Disconnected) {
        ControlError::detail("output control connection failed: disconnected")
    } else {
        ControlError::detail(format!("output control connection failed: {error}"))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn handshake_matches_the_legacy_output_tool_profile() {
        let config = handshake_config(TransportLimits::new(4096, 0).unwrap());
        assert_eq!(config.local_role, Role::DiagnosticTool);
        assert_eq!(config.label, "glasswyrm-output-tool");
        assert!(
            config
                .offered_capabilities
                .contains(Capabilities::VRR_POLICY)
        );
        assert!(
            config
                .required_peer_capabilities
                .contains(Capabilities::OUTPUT_CONTROL)
        );
    }

    #[test]
    fn errors_make_peer_control_characters_visible() {
        let error = ControlError::detail("peer said no\n\u{1b}[2J\u{7f}");
        assert_eq!(error.to_string(), "peer said no\\x0a\\x1b[2J\\x7f");
    }
}
