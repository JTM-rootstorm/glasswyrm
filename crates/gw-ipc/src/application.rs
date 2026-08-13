//! Established-connection GWIPC application validation and correlation.
//!
//! This state machine mirrors the legacy M14 connection validators. Wire
//! decoding remains in `gw-wire`; this module owns the stateful rules that
//! depend on roles, negotiated capabilities, snapshot lifecycle, sequencing,
//! and request/reply correlation.

use std::collections::{HashMap, HashSet};
use std::fmt;

use gw_types::{Capabilities, MessageFlags, MessageType, Role, SnapshotDomain};
use gw_wire::compositor::{
    decode_buffer_attach, decode_buffer_detach, decode_buffer_release, decode_frame_acknowledged,
    decode_frame_commit, decode_output_remove, decode_output_upsert, decode_surface_damage,
    decode_surface_remove, decode_surface_upsert,
};
use gw_wire::vrr::{
    decode_output_vrr_capability_upsert, decode_output_vrr_policy_upsert,
    decode_output_vrr_state_upsert, decode_policy_output_vrr_state,
    decode_policy_output_vrr_upsert, decode_policy_window_vrr_state,
    decode_policy_window_vrr_upsert, decode_presentation_timing, decode_surface_vrr_state,
};
use gw_wire::{
    Envelope, decode_output_configuration_acknowledged, decode_output_configuration_commit,
    decode_output_descriptor_upsert, decode_output_mode_upsert, decode_output_state_query,
    decode_ping, decode_policy_acknowledged, decode_policy_bindings_upsert, decode_policy_commit,
    decode_policy_context_upsert, decode_policy_lifecycle_window_upsert,
    decode_policy_output_upsert, decode_policy_window_output_hint, decode_policy_window_remove,
    decode_policy_window_state, decode_policy_window_upsert, decode_pong, decode_protocol_error,
    decode_session_state_acknowledged, decode_session_state_change, decode_snapshot_abort,
    decode_snapshot_begin, decode_snapshot_end, decode_surface_output_state,
    decode_surface_policy_upsert, decode_synthetic_barrier, decode_synthetic_button,
    decode_synthetic_input_acknowledged, decode_synthetic_key, decode_synthetic_motion,
};

/// Which endpoint originated a message relative to this validator.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum MessageDirection {
    Incoming,
    Outgoing,
}

/// Stateful validation failures corresponding to the legacy status classes.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ApplicationError {
    UnsupportedMessage,
    Protocol,
    CapabilityMismatch,
    InvalidState,
    LimitExceeded,
    OutOfOrderSequence,
    UnexpectedReply,
}

impl fmt::Display for ApplicationError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(match self {
            Self::UnsupportedMessage => "unsupported GWIPC application message",
            Self::Protocol => "invalid GWIPC application message",
            Self::CapabilityMismatch => "GWIPC capability mismatch",
            Self::InvalidState => "invalid GWIPC connection state",
            Self::LimitExceeded => "GWIPC correlation limit exceeded",
            Self::OutOfOrderSequence => "out-of-order GWIPC sequence",
            Self::UnexpectedReply => "unexpected GWIPC reply",
        })
    }
}

impl std::error::Error for ApplicationError {}

/// Snapshot transaction state maintained independently in each direction.
#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct SnapshotState {
    pub active: bool,
    pub snapshot_id: u64,
    pub generation: u64,
    pub expected_item_count: u32,
    pub item_count: u32,
    pub domain: Option<SnapshotDomain>,
    pub policy_bindings_count: u8,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
struct SessionCorrelation {
    generation: u64,
    state: u16,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
struct OutputCorrelation {
    request_id: u64,
}

/// Established GWIPC validation state for one connection.
#[derive(Clone, Debug)]
pub struct ApplicationValidator {
    local_role: Role,
    peer_role: Role,
    capabilities: Capabilities,
    maximum_queued_messages: usize,
    next_send_sequence: u64,
    next_receive_sequence: u64,
    outgoing_snapshot: SnapshotState,
    incoming_snapshot: SnapshotState,
    pending_replies: HashSet<u64>,
    pending_ping_nonces: HashMap<u64, u64>,
    pending_frame_commits: HashMap<u64, u64>,
    pending_policy_commits: HashMap<u64, u64>,
    pending_synthetic_inputs: HashMap<u64, u64>,
    pending_session_states: HashMap<u64, SessionCorrelation>,
    pending_output_requests: HashMap<u64, OutputCorrelation>,
    incoming_frame_commits: HashMap<u64, u64>,
    incoming_policy_commits: HashMap<u64, u64>,
    incoming_synthetic_inputs: HashMap<u64, u64>,
    incoming_session_states: HashMap<u64, SessionCorrelation>,
    incoming_output_requests: HashMap<u64, OutputCorrelation>,
    last_outgoing_session_generation: u64,
    last_incoming_session_generation: u64,
}

impl ApplicationValidator {
    #[must_use]
    pub fn new(
        local_role: Role,
        peer_role: Role,
        capabilities: Capabilities,
        maximum_queued_messages: usize,
    ) -> Self {
        Self {
            local_role,
            peer_role,
            capabilities,
            maximum_queued_messages,
            next_send_sequence: 1,
            next_receive_sequence: 1,
            outgoing_snapshot: SnapshotState::default(),
            incoming_snapshot: SnapshotState::default(),
            pending_replies: HashSet::new(),
            pending_ping_nonces: HashMap::new(),
            pending_frame_commits: HashMap::new(),
            pending_policy_commits: HashMap::new(),
            pending_synthetic_inputs: HashMap::new(),
            pending_session_states: HashMap::new(),
            pending_output_requests: HashMap::new(),
            incoming_frame_commits: HashMap::new(),
            incoming_policy_commits: HashMap::new(),
            incoming_synthetic_inputs: HashMap::new(),
            incoming_session_states: HashMap::new(),
            incoming_output_requests: HashMap::new(),
            last_outgoing_session_generation: 0,
            last_incoming_session_generation: 0,
        }
    }

    #[must_use]
    pub const fn outgoing_snapshot(&self) -> SnapshotState {
        self.outgoing_snapshot
    }

    #[must_use]
    pub const fn incoming_snapshot(&self) -> SnapshotState {
        self.incoming_snapshot
    }

    /// Validates and records a message before it is queued for transport.
    pub fn validate_outgoing(
        &mut self,
        envelope: &Envelope,
        payload: &[u8],
        fd_count: usize,
    ) -> Result<(), ApplicationError> {
        if envelope.sequence.get() != self.next_send_sequence || self.next_send_sequence == u64::MAX
        {
            return Err(ApplicationError::OutOfOrderSequence);
        }
        validate_envelope_application_shape(envelope, payload, fd_count)?;

        let mut candidate = self.clone();
        candidate.validate_application(envelope, payload, fd_count, MessageDirection::Outgoing)?;
        candidate.correlate_outgoing(envelope, payload)?;
        candidate.next_send_sequence += 1;
        *self = candidate;
        Ok(())
    }

    /// Validates and records a received message.
    ///
    /// As in the legacy transport, an envelope consumes its expected receive
    /// sequence before application validation. A protocol failure closes the
    /// real connection, so later reuse of this state is intentionally invalid.
    pub fn validate_incoming(
        &mut self,
        envelope: &Envelope,
        payload: &[u8],
        fd_count: usize,
    ) -> Result<(), ApplicationError> {
        if envelope.sequence.get() != self.next_receive_sequence {
            return Err(ApplicationError::OutOfOrderSequence);
        }
        if self.next_receive_sequence == u64::MAX {
            return Err(ApplicationError::LimitExceeded);
        }
        self.next_receive_sequence += 1;
        validate_envelope_application_shape(envelope, payload, fd_count)?;

        let mut candidate = self.clone();
        candidate.validate_application(envelope, payload, fd_count, MessageDirection::Incoming)?;
        candidate.correlate_incoming(envelope, payload)?;
        *self = candidate;
        Ok(())
    }

    fn validate_application(
        &mut self,
        envelope: &Envelope,
        payload: &[u8],
        fd_count: usize,
        direction: MessageDirection,
    ) -> Result<(), ApplicationError> {
        let message_type = envelope.message_type;
        let flags = envelope.flags;
        let roles = self.roles(direction);

        if message_type == MessageType::SNAPSHOT_BEGIN {
            let begin = decode_snapshot_begin(payload).map_err(|_| ApplicationError::Protocol)?;
            if (roles == (Role::ProtocolServer, Role::Compositor)
                && begin.domain != SnapshotDomain::CompleteSession)
                || (roles == (Role::Compositor, Role::ProtocolServer)
                    && begin.domain != SnapshotDomain::Outputs)
            {
                return Err(ApplicationError::Protocol);
            }
        }

        let snapshot = match direction {
            MessageDirection::Incoming => self.incoming_snapshot,
            MessageDirection::Outgoing => self.outgoing_snapshot,
        };
        if is_output_extension(message_type) {
            self.validate_output_extension(
                message_type,
                flags,
                payload,
                fd_count,
                snapshot,
                roles,
            )?;
        } else {
            self.require_capabilities(required_capabilities(message_type))?;
            validate_special_direction(message_type, roles)?;
            validate_base_payload_and_flags(message_type, flags, payload, snapshot)?;
            if message_type == MessageType::SURFACE_UPSERT {
                self.validate_surface_presentation_capabilities(payload)?;
            }
            validate_descriptor_shape(message_type, payload, fd_count, self.capabilities)?;
        }

        let require_policy_bindings = self.capabilities.contains(Capabilities::INTERACTIVE_POLICY)
            && roles == (Role::WindowManager, Role::ProtocolServer);
        let state = match direction {
            MessageDirection::Incoming => &mut self.incoming_snapshot,
            MessageDirection::Outgoing => &mut self.outgoing_snapshot,
        };
        validate_snapshot_lifecycle(state, message_type, flags, payload, require_policy_bindings)
    }

    fn validate_output_extension(
        &self,
        message_type: MessageType,
        flags: MessageFlags,
        payload: &[u8],
        fd_count: usize,
        snapshot: SnapshotState,
        roles: (Role, Role),
    ) -> Result<(), ApplicationError> {
        if fd_count != 0 {
            return Err(ApplicationError::Protocol);
        }
        let required = output_roles_and_capabilities(message_type, roles)?;
        self.require_capabilities(required)?;
        validate_output_flags_and_snapshot(message_type, flags, snapshot, roles)?;
        if valid_output_payload(message_type, payload) {
            Ok(())
        } else {
            Err(ApplicationError::Protocol)
        }
    }

    fn roles(&self, direction: MessageDirection) -> (Role, Role) {
        match direction {
            MessageDirection::Outgoing => (self.local_role, self.peer_role),
            MessageDirection::Incoming => (self.peer_role, self.local_role),
        }
    }

    fn require_capabilities(&self, required: u64) -> Result<(), ApplicationError> {
        if self.capabilities.bits() & required == required {
            Ok(())
        } else {
            Err(ApplicationError::CapabilityMismatch)
        }
    }

    fn validate_surface_presentation_capabilities(
        &self,
        payload: &[u8],
    ) -> Result<(), ApplicationError> {
        let surface = decode_surface_upsert(payload).map_err(|_| ApplicationError::Protocol)?;
        const METADATA_ONLY: u32 = 1;
        const CURSOR: u32 = 1 << 1;
        if surface.presentation_flags & METADATA_ONLY != 0
            && !self.capabilities.contains(Capabilities::WINDOW_LIFECYCLE)
        {
            return Err(ApplicationError::CapabilityMismatch);
        }
        if surface.presentation_flags & CURSOR != 0 {
            self.require_capabilities(
                Capabilities::CURSOR_SURFACE.bits()
                    | Capabilities::FD_PASSING.bits()
                    | Capabilities::MEMFD_BUFFERS.bits()
                    | Capabilities::DAMAGE_REGIONS.bits()
                    | Capabilities::WINDOW_LIFECYCLE.bits(),
            )?;
        }
        Ok(())
    }

    fn correlate_outgoing(
        &mut self,
        envelope: &Envelope,
        payload: &[u8],
    ) -> Result<(), ApplicationError> {
        let sequence = envelope.sequence.get();
        let reply_to = envelope.reply_to.get();
        if envelope.flags.contains(MessageFlags::REPLY) {
            self.validate_outgoing_reply(envelope.message_type, reply_to, payload)?;
        }

        if envelope.message_type == MessageType::PING {
            let value = decode_ping(payload).map_err(|_| ApplicationError::Protocol)?;
            self.pending_ping_nonces.insert(sequence, value.nonce);
        } else if envelope.message_type == MessageType::FRAME_COMMIT {
            let value = decode_frame_commit(payload).map_err(|_| ApplicationError::Protocol)?;
            insert_limited(
                &mut self.pending_frame_commits,
                sequence,
                value.commit_id,
                self.maximum_queued_messages,
            )?;
        } else if envelope.message_type == MessageType::POLICY_COMMIT {
            let value = decode_policy_commit(payload).map_err(|_| ApplicationError::Protocol)?;
            insert_limited(
                &mut self.pending_policy_commits,
                sequence,
                value.commit_id,
                self.maximum_queued_messages,
            )?;
        } else if let Some(input_id) = synthetic_input_id(envelope.message_type, payload)? {
            insert_limited(
                &mut self.pending_synthetic_inputs,
                sequence,
                input_id,
                self.maximum_queued_messages,
            )?;
        } else if envelope.message_type == MessageType::SESSION_STATE_CHANGE {
            let value =
                decode_session_state_change(payload).map_err(|_| ApplicationError::Protocol)?;
            if value.generation <= self.last_outgoing_session_generation {
                return Err(ApplicationError::InvalidState);
            }
            insert_limited(
                &mut self.pending_session_states,
                sequence,
                SessionCorrelation {
                    generation: value.generation,
                    state: value.state as u16,
                },
                self.maximum_queued_messages,
            )?;
            self.last_outgoing_session_generation = value.generation;
        } else if let Some(output) = output_request_identity(envelope.message_type, payload)? {
            insert_limited(
                &mut self.pending_output_requests,
                sequence,
                output,
                self.maximum_queued_messages,
            )?;
        }

        if envelope.flags.contains(MessageFlags::ACK_REQUIRED) {
            if self.pending_replies.len() >= self.maximum_queued_messages {
                return Err(ApplicationError::LimitExceeded);
            }
            self.pending_replies.insert(sequence);
        }
        Ok(())
    }

    fn correlate_incoming(
        &mut self,
        envelope: &Envelope,
        payload: &[u8],
    ) -> Result<(), ApplicationError> {
        if envelope.flags.contains(MessageFlags::REPLY) {
            self.validate_incoming_reply(envelope, payload)?;
        }

        let sequence = envelope.sequence.get();
        if envelope.message_type == MessageType::FRAME_COMMIT {
            let value = decode_frame_commit(payload).map_err(|_| ApplicationError::Protocol)?;
            insert_limited(
                &mut self.incoming_frame_commits,
                sequence,
                value.commit_id,
                self.maximum_queued_messages,
            )?;
        } else if envelope.message_type == MessageType::POLICY_COMMIT {
            let value = decode_policy_commit(payload).map_err(|_| ApplicationError::Protocol)?;
            insert_limited(
                &mut self.incoming_policy_commits,
                sequence,
                value.commit_id,
                self.maximum_queued_messages,
            )?;
        } else if let Some(input_id) = synthetic_input_id(envelope.message_type, payload)? {
            insert_limited(
                &mut self.incoming_synthetic_inputs,
                sequence,
                input_id,
                self.maximum_queued_messages,
            )?;
        } else if envelope.message_type == MessageType::SESSION_STATE_CHANGE {
            let value =
                decode_session_state_change(payload).map_err(|_| ApplicationError::Protocol)?;
            if value.generation <= self.last_incoming_session_generation {
                return Err(ApplicationError::Protocol);
            }
            insert_limited(
                &mut self.incoming_session_states,
                sequence,
                SessionCorrelation {
                    generation: value.generation,
                    state: value.state as u16,
                },
                self.maximum_queued_messages,
            )?;
            self.last_incoming_session_generation = value.generation;
        } else if let Some(output) = output_request_identity(envelope.message_type, payload)? {
            insert_limited(
                &mut self.incoming_output_requests,
                sequence,
                output,
                self.maximum_queued_messages,
            )?;
        }
        Ok(())
    }

    fn validate_outgoing_reply(
        &mut self,
        message_type: MessageType,
        reply_to: u64,
        payload: &[u8],
    ) -> Result<(), ApplicationError> {
        if message_type == MessageType::FRAME_ACKNOWLEDGED {
            let value =
                decode_frame_acknowledged(payload).map_err(|_| ApplicationError::InvalidState)?;
            take_matching(&mut self.incoming_frame_commits, reply_to, value.commit_id)
                .map_err(|_| ApplicationError::InvalidState)?;
        } else if message_type == MessageType::OUTPUT_VRR_STATE_UPSERT {
            let value = decode_output_vrr_state_upsert(payload)
                .map_err(|_| ApplicationError::InvalidState)?;
            let expected = self.incoming_frame_commits.get(&reply_to).copied();
            if expected != Some(value.last_commit_id) {
                return Err(ApplicationError::InvalidState);
            }
        } else if message_type == MessageType::POLICY_ACKNOWLEDGED {
            let value =
                decode_policy_acknowledged(payload).map_err(|_| ApplicationError::InvalidState)?;
            take_matching(&mut self.incoming_policy_commits, reply_to, value.commit_id)
                .map_err(|_| ApplicationError::InvalidState)?;
        } else if message_type == MessageType::SYNTHETIC_INPUT_ACKNOWLEDGED {
            let value = decode_synthetic_input_acknowledged(payload)
                .map_err(|_| ApplicationError::InvalidState)?;
            take_matching(
                &mut self.incoming_synthetic_inputs,
                reply_to,
                value.input_id,
            )
            .map_err(|_| ApplicationError::InvalidState)?;
        } else if message_type == MessageType::SESSION_STATE_ACKNOWLEDGED {
            let value = decode_session_state_acknowledged(payload)
                .map_err(|_| ApplicationError::InvalidState)?;
            let expected = SessionCorrelation {
                generation: value.generation,
                state: value.state as u16,
            };
            take_matching(&mut self.incoming_session_states, reply_to, expected)
                .map_err(|_| ApplicationError::InvalidState)?;
        } else if message_type == MessageType::OUTPUT_CONFIGURATION_ACKNOWLEDGED {
            let value = decode_output_configuration_acknowledged(payload)
                .map_err(|_| ApplicationError::InvalidState)?;
            let Some(expected) = self.incoming_output_requests.get(&reply_to) else {
                return Err(ApplicationError::InvalidState);
            };
            if expected.request_id != value.request_id {
                return Err(ApplicationError::InvalidState);
            }
            self.incoming_output_requests.remove(&reply_to);
        }
        Ok(())
    }

    fn validate_incoming_reply(
        &mut self,
        envelope: &Envelope,
        payload: &[u8],
    ) -> Result<(), ApplicationError> {
        let reply_to = envelope.reply_to.get();
        let message_type = envelope.message_type;
        if message_type == MessageType::PONG {
            let value = decode_pong(payload).map_err(|_| ApplicationError::UnexpectedReply)?;
            take_matching(&mut self.pending_ping_nonces, reply_to, value.nonce)?;
        } else if message_type == MessageType::FRAME_ACKNOWLEDGED {
            let value = decode_frame_acknowledged(payload)
                .map_err(|_| ApplicationError::UnexpectedReply)?;
            take_matching(&mut self.pending_frame_commits, reply_to, value.commit_id)?;
        } else if message_type == MessageType::OUTPUT_VRR_STATE_UPSERT {
            let value = decode_output_vrr_state_upsert(payload)
                .map_err(|_| ApplicationError::UnexpectedReply)?;
            let expected = self.pending_frame_commits.get(&reply_to).copied();
            if expected != Some(value.last_commit_id) {
                return Err(ApplicationError::UnexpectedReply);
            }
            // A VRR state reply is correlated to the frame but does not consume
            // its pending acknowledgement; the later FrameAcknowledged closes it.
            return Ok(());
        } else if message_type == MessageType::POLICY_ACKNOWLEDGED {
            let value = decode_policy_acknowledged(payload)
                .map_err(|_| ApplicationError::UnexpectedReply)?;
            take_matching(&mut self.pending_policy_commits, reply_to, value.commit_id)?;
        } else if message_type == MessageType::SYNTHETIC_INPUT_ACKNOWLEDGED {
            let value = decode_synthetic_input_acknowledged(payload)
                .map_err(|_| ApplicationError::UnexpectedReply)?;
            take_matching(&mut self.pending_synthetic_inputs, reply_to, value.input_id)?;
        } else if message_type == MessageType::SESSION_STATE_ACKNOWLEDGED {
            let value = decode_session_state_acknowledged(payload)
                .map_err(|_| ApplicationError::UnexpectedReply)?;
            take_matching(
                &mut self.pending_session_states,
                reply_to,
                SessionCorrelation {
                    generation: value.generation,
                    state: value.state as u16,
                },
            )?;
        } else if message_type == MessageType::OUTPUT_CONFIGURATION_ACKNOWLEDGED {
            let value = decode_output_configuration_acknowledged(payload)
                .map_err(|_| ApplicationError::UnexpectedReply)?;
            let Some(expected) = self.pending_output_requests.get(&reply_to) else {
                return Err(ApplicationError::UnexpectedReply);
            };
            if expected.request_id != value.request_id {
                return Err(ApplicationError::UnexpectedReply);
            }
            self.pending_output_requests.remove(&reply_to);
        }

        let protocol_error_for_sent_message = message_type == MessageType::PROTOCOL_ERROR
            && reply_to > 0
            && reply_to < self.next_send_sequence;
        if !self.pending_replies.remove(&reply_to) && !protocol_error_for_sent_message {
            return Err(ApplicationError::UnexpectedReply);
        }
        Ok(())
    }
}

fn validate_envelope_application_shape(
    envelope: &Envelope,
    payload: &[u8],
    fd_count: usize,
) -> Result<(), ApplicationError> {
    if envelope.version != gw_types::GWIPC_WIRE_VERSION
        || envelope.payload_size as usize != payload.len()
        || usize::from(envelope.fd_count) != fd_count
        || envelope.message_type == MessageType::HELLO
        || envelope.message_type == MessageType::WELCOME
        || envelope.message_type == MessageType::REJECT
    {
        return Err(ApplicationError::Protocol);
    }
    let reply = envelope.flags.contains(MessageFlags::REPLY);
    let error = envelope.flags.contains(MessageFlags::ERROR);
    if error && !reply || reply != (envelope.reply_to.get() != 0) {
        return Err(ApplicationError::Protocol);
    }
    Ok(())
}

fn required_capabilities(message_type: MessageType) -> u64 {
    match message_type {
        MessageType::SNAPSHOT_BEGIN | MessageType::SNAPSHOT_END | MessageType::SNAPSHOT_ABORT => {
            Capabilities::SNAPSHOTS.bits()
        }
        MessageType::OUTPUT_UPSERT | MessageType::OUTPUT_REMOVE => {
            Capabilities::OUTPUT_STATE.bits()
        }
        MessageType::SURFACE_UPSERT | MessageType::SURFACE_REMOVE => {
            Capabilities::SURFACE_STATE.bits()
        }
        MessageType::SURFACE_POLICY_UPSERT => {
            Capabilities::SURFACE_STATE.bits() | Capabilities::WINDOW_LIFECYCLE.bits()
        }
        MessageType::BUFFER_ATTACH => {
            Capabilities::FD_PASSING.bits() | Capabilities::MEMFD_BUFFERS.bits()
        }
        MessageType::SURFACE_DAMAGE => Capabilities::DAMAGE_REGIONS.bits(),
        MessageType::FRAME_ACKNOWLEDGED => Capabilities::FRAME_ACKNOWLEDGEMENT.bits(),
        MessageType::POLICY_CONTEXT_UPSERT
        | MessageType::POLICY_WINDOW_UPSERT
        | MessageType::POLICY_WINDOW_REMOVE
        | MessageType::POLICY_COMMIT
        | MessageType::POLICY_WINDOW_STATE
        | MessageType::POLICY_ACKNOWLEDGED => Capabilities::WINDOW_POLICY.bits(),
        MessageType::POLICY_LIFECYCLE_WINDOW_UPSERT => {
            Capabilities::WINDOW_POLICY.bits() | Capabilities::WINDOW_LIFECYCLE.bits()
        }
        MessageType::SYNTHETIC_MOTION
        | MessageType::SYNTHETIC_BUTTON
        | MessageType::SYNTHETIC_KEY
        | MessageType::SYNTHETIC_BARRIER
        | MessageType::SYNTHETIC_INPUT_ACKNOWLEDGED => Capabilities::SYNTHETIC_INPUT.bits(),
        MessageType::POLICY_BINDINGS_UPSERT => {
            Capabilities::WINDOW_POLICY.bits() | Capabilities::INTERACTIVE_POLICY.bits()
        }
        MessageType::SESSION_STATE_CHANGE | MessageType::SESSION_STATE_ACKNOWLEDGED => {
            Capabilities::SESSION_STATE.bits()
        }
        _ => 0,
    }
}

fn validate_special_direction(
    message_type: MessageType,
    roles: (Role, Role),
) -> Result<(), ApplicationError> {
    let valid = match message_type {
        MessageType::SESSION_STATE_CHANGE => roles == (Role::Compositor, Role::ProtocolServer),
        MessageType::SESSION_STATE_ACKNOWLEDGED => {
            roles == (Role::ProtocolServer, Role::Compositor)
        }
        MessageType::POLICY_BINDINGS_UPSERT => roles == (Role::WindowManager, Role::ProtocolServer),
        _ => true,
    };
    valid.then_some(()).ok_or(ApplicationError::Protocol)
}

fn validate_base_payload_and_flags(
    message_type: MessageType,
    flags: MessageFlags,
    payload: &[u8],
    snapshot: SnapshotState,
) -> Result<(), ApplicationError> {
    let bits = flags.bits();
    let none_or_item = bits == 0 || bits == MessageFlags::SNAPSHOT_ITEM.bits();
    let valid = match message_type {
        MessageType::PING => {
            bits == MessageFlags::ACK_REQUIRED.bits() && decode_ping(payload).is_ok()
        }
        MessageType::PONG => bits == MessageFlags::REPLY.bits() && decode_pong(payload).is_ok(),
        MessageType::PROTOCOL_ERROR => {
            bits == MessageFlags::REPLY.with(MessageFlags::ERROR).bits()
                && decode_protocol_error(payload).is_ok()
        }
        MessageType::SNAPSHOT_BEGIN => decode_snapshot_begin(payload).is_ok(),
        MessageType::SNAPSHOT_END => decode_snapshot_end(payload).is_ok(),
        MessageType::SNAPSHOT_ABORT => decode_snapshot_abort(payload).is_ok(),
        MessageType::OUTPUT_UPSERT => decode_output_upsert(payload).is_ok(),
        MessageType::OUTPUT_REMOVE => decode_output_remove(payload).is_ok(),
        MessageType::SURFACE_UPSERT => decode_surface_upsert(payload).is_ok(),
        MessageType::SURFACE_REMOVE => decode_surface_remove(payload).is_ok(),
        MessageType::BUFFER_ATTACH => decode_buffer_attach(payload).is_ok(),
        MessageType::BUFFER_DETACH => decode_buffer_detach(payload).is_ok(),
        MessageType::BUFFER_RELEASE => decode_buffer_release(payload).is_ok(),
        MessageType::SURFACE_DAMAGE => decode_surface_damage(payload).is_ok(),
        MessageType::FRAME_COMMIT => {
            bits == MessageFlags::ACK_REQUIRED.bits() && decode_frame_commit(payload).is_ok()
        }
        MessageType::FRAME_ACKNOWLEDGED => {
            bits == MessageFlags::REPLY.bits() && decode_frame_acknowledged(payload).is_ok()
        }
        MessageType::POLICY_CONTEXT_UPSERT => {
            none_or_item && decode_policy_context_upsert(payload).is_ok()
        }
        MessageType::POLICY_WINDOW_UPSERT => {
            none_or_item && decode_policy_window_upsert(payload).is_ok()
        }
        MessageType::POLICY_WINDOW_REMOVE => {
            bits == 0 && decode_policy_window_remove(payload).is_ok()
        }
        MessageType::POLICY_COMMIT => {
            bits == MessageFlags::ACK_REQUIRED.bits()
                && !snapshot.active
                && decode_policy_commit(payload).is_ok()
        }
        MessageType::POLICY_WINDOW_STATE => {
            bits == MessageFlags::SNAPSHOT_ITEM.bits()
                && decode_policy_window_state(payload).is_ok()
        }
        MessageType::POLICY_ACKNOWLEDGED => {
            bits == MessageFlags::REPLY.bits() && decode_policy_acknowledged(payload).is_ok()
        }
        MessageType::POLICY_LIFECYCLE_WINDOW_UPSERT => {
            none_or_item && decode_policy_lifecycle_window_upsert(payload).is_ok()
        }
        MessageType::POLICY_BINDINGS_UPSERT => {
            bits == MessageFlags::SNAPSHOT_ITEM.bits()
                && decode_policy_bindings_upsert(payload).is_ok()
        }
        MessageType::SURFACE_POLICY_UPSERT => {
            bits == MessageFlags::SNAPSHOT_ITEM.bits()
                && decode_surface_policy_upsert(payload).is_ok()
        }
        MessageType::SYNTHETIC_MOTION => {
            bits == MessageFlags::ACK_REQUIRED.bits() && decode_synthetic_motion(payload).is_ok()
        }
        MessageType::SYNTHETIC_BUTTON => {
            bits == MessageFlags::ACK_REQUIRED.bits() && decode_synthetic_button(payload).is_ok()
        }
        MessageType::SYNTHETIC_KEY => {
            bits == MessageFlags::ACK_REQUIRED.bits() && decode_synthetic_key(payload).is_ok()
        }
        MessageType::SYNTHETIC_BARRIER => {
            bits == MessageFlags::ACK_REQUIRED.bits() && decode_synthetic_barrier(payload).is_ok()
        }
        MessageType::SYNTHETIC_INPUT_ACKNOWLEDGED => {
            bits == MessageFlags::REPLY.bits()
                && decode_synthetic_input_acknowledged(payload).is_ok()
        }
        MessageType::SESSION_STATE_CHANGE => {
            bits == MessageFlags::ACK_REQUIRED.bits()
                && decode_session_state_change(payload).is_ok()
        }
        MessageType::SESSION_STATE_ACKNOWLEDGED => {
            bits == MessageFlags::REPLY.bits() && decode_session_state_acknowledged(payload).is_ok()
        }
        _ => return Err(ApplicationError::UnsupportedMessage),
    };
    valid.then_some(()).ok_or(ApplicationError::Protocol)
}

fn validate_descriptor_shape(
    message_type: MessageType,
    payload: &[u8],
    fd_count: usize,
    capabilities: Capabilities,
) -> Result<(), ApplicationError> {
    if message_type != MessageType::BUFFER_ATTACH {
        return (fd_count == 0)
            .then_some(())
            .ok_or(ApplicationError::Protocol);
    }
    let attachment = decode_buffer_attach(payload).map_err(|_| ApplicationError::Protocol)?;
    let synchronized =
        attachment.synchronization == gw_wire::compositor::SynchronizationMode::EventFd;
    if synchronized && !capabilities.contains(Capabilities::CPU_BUFFER_SYNCHRONIZATION) {
        return Err(ApplicationError::CapabilityMismatch);
    }
    let expected = if synchronized { 2 } else { 1 };
    (fd_count == expected)
        .then_some(())
        .ok_or(ApplicationError::Protocol)
}

fn validate_snapshot_lifecycle(
    state: &mut SnapshotState,
    message_type: MessageType,
    flags: MessageFlags,
    payload: &[u8],
    require_policy_bindings: bool,
) -> Result<(), ApplicationError> {
    let item = flags.contains(MessageFlags::SNAPSHOT_ITEM);
    if is_snapshot_control(message_type) && item {
        return Err(ApplicationError::Protocol);
    }
    if message_type == MessageType::SNAPSHOT_BEGIN {
        let begin = decode_snapshot_begin(payload).map_err(|_| ApplicationError::Protocol)?;
        if state.active {
            return Err(ApplicationError::Protocol);
        }
        *state = SnapshotState {
            active: true,
            snapshot_id: begin.snapshot_id.get(),
            generation: begin.generation.get(),
            expected_item_count: begin.expected_item_count,
            item_count: 0,
            domain: Some(begin.domain),
            policy_bindings_count: 0,
        };
        return Ok(());
    }
    if message_type == MessageType::SNAPSHOT_END {
        let end = decode_snapshot_end(payload).map_err(|_| ApplicationError::Protocol)?;
        let count_matches =
            state.expected_item_count == u32::MAX || state.expected_item_count == state.item_count;
        if !state.active
            || end.snapshot_id.get() != state.snapshot_id
            || end.generation.get() != state.generation
            || end.actual_item_count != state.item_count
            || !count_matches
            || (require_policy_bindings
                && state.domain == Some(SnapshotDomain::WindowPolicy)
                && state.policy_bindings_count != 1)
        {
            return Err(ApplicationError::Protocol);
        }
        *state = SnapshotState::default();
        return Ok(());
    }
    if message_type == MessageType::POLICY_BINDINGS_UPSERT {
        if !state.active
            || state.domain != Some(SnapshotDomain::WindowPolicy)
            || state.policy_bindings_count != 0
        {
            return Err(ApplicationError::Protocol);
        }
        state.policy_bindings_count = 1;
    }
    if message_type == MessageType::SNAPSHOT_ABORT {
        let abort = decode_snapshot_abort(payload).map_err(|_| ApplicationError::Protocol)?;
        if !state.active || abort.snapshot_id.get() != state.snapshot_id {
            return Err(ApplicationError::Protocol);
        }
        *state = SnapshotState::default();
        return Ok(());
    }
    if item {
        if !state.active || state.item_count == u32::MAX {
            return Err(ApplicationError::Protocol);
        }
        state.item_count += 1;
    }
    Ok(())
}

fn is_snapshot_control(message_type: MessageType) -> bool {
    matches!(
        message_type,
        MessageType::SNAPSHOT_BEGIN | MessageType::SNAPSHOT_END | MessageType::SNAPSHOT_ABORT
    )
}

fn is_output_extension(message_type: MessageType) -> bool {
    matches!(
        message_type,
        MessageType::OUTPUT_DESCRIPTOR_UPSERT
            | MessageType::OUTPUT_MODE_UPSERT
            | MessageType::OUTPUT_VRR_CAPABILITY_UPSERT
            | MessageType::OUTPUT_VRR_POLICY_UPSERT
            | MessageType::OUTPUT_VRR_STATE_UPSERT
            | MessageType::SURFACE_OUTPUT_STATE
            | MessageType::SURFACE_VRR_STATE
            | MessageType::POLICY_OUTPUT_UPSERT
            | MessageType::POLICY_WINDOW_OUTPUT_HINT
            | MessageType::POLICY_WINDOW_VRR_UPSERT
            | MessageType::POLICY_OUTPUT_VRR_UPSERT
            | MessageType::POLICY_WINDOW_VRR_STATE
            | MessageType::POLICY_OUTPUT_VRR_STATE
            | MessageType::PRESENTATION_TIMING
            | MessageType::OUTPUT_STATE_QUERY
            | MessageType::OUTPUT_CONFIGURATION_COMMIT
            | MessageType::OUTPUT_CONFIGURATION_ACKNOWLEDGED
    )
}

fn output_roles_and_capabilities(
    message_type: MessageType,
    roles: (Role, Role),
) -> Result<u64, ApplicationError> {
    let caps = match message_type {
        MessageType::OUTPUT_DESCRIPTOR_UPSERT | MessageType::OUTPUT_MODE_UPSERT => match roles {
            (Role::Compositor, Role::ProtocolServer) => Capabilities::OUTPUT_MANAGEMENT.bits(),
            (Role::ProtocolServer, Role::DiagnosticTool) => Capabilities::OUTPUT_CONTROL.bits(),
            _ => return Err(ApplicationError::Protocol),
        },
        MessageType::OUTPUT_VRR_CAPABILITY_UPSERT => match roles {
            (Role::Compositor, Role::ProtocolServer) => Capabilities::VRR_METADATA.bits(),
            (Role::ProtocolServer, Role::DiagnosticTool) => {
                Capabilities::OUTPUT_CONTROL.bits() | Capabilities::VRR_METADATA.bits()
            }
            _ => return Err(ApplicationError::Protocol),
        },
        MessageType::OUTPUT_VRR_POLICY_UPSERT => match roles {
            (Role::Compositor, Role::ProtocolServer) => {
                Capabilities::VRR_METADATA.bits() | Capabilities::VRR_POLICY.bits()
            }
            (Role::ProtocolServer, Role::Compositor) => Capabilities::VRR_POLICY.bits(),
            (Role::DiagnosticTool, Role::ProtocolServer)
            | (Role::ProtocolServer, Role::DiagnosticTool) => {
                Capabilities::OUTPUT_CONTROL.bits() | Capabilities::VRR_POLICY.bits()
            }
            _ => return Err(ApplicationError::Protocol),
        },
        MessageType::OUTPUT_VRR_STATE_UPSERT => match roles {
            (Role::Compositor, Role::ProtocolServer) => {
                Capabilities::VRR_METADATA.bits() | Capabilities::VRR_POLICY.bits()
            }
            (Role::ProtocolServer, Role::DiagnosticTool) => {
                Capabilities::OUTPUT_CONTROL.bits()
                    | Capabilities::VRR_METADATA.bits()
                    | Capabilities::VRR_POLICY.bits()
            }
            _ => return Err(ApplicationError::Protocol),
        },
        MessageType::SURFACE_OUTPUT_STATE => match roles {
            (Role::ProtocolServer, Role::Compositor) => {
                Capabilities::SURFACE_OUTPUT_MEMBERSHIP.bits() | Capabilities::SCALE_METADATA.bits()
            }
            (Role::ProtocolServer, Role::DiagnosticTool) => {
                Capabilities::OUTPUT_CONTROL.bits()
                    | Capabilities::SURFACE_OUTPUT_MEMBERSHIP.bits()
                    | Capabilities::SCALE_METADATA.bits()
            }
            _ => return Err(ApplicationError::Protocol),
        },
        MessageType::SURFACE_VRR_STATE => match roles {
            (Role::ProtocolServer, Role::Compositor) => {
                Capabilities::VRR_METADATA.bits() | Capabilities::VRR_POLICY.bits()
            }
            (Role::ProtocolServer, Role::DiagnosticTool) => {
                Capabilities::OUTPUT_CONTROL.bits()
                    | Capabilities::VRR_METADATA.bits()
                    | Capabilities::VRR_POLICY.bits()
            }
            _ => return Err(ApplicationError::Protocol),
        },
        MessageType::POLICY_OUTPUT_UPSERT | MessageType::POLICY_WINDOW_OUTPUT_HINT => {
            if roles != (Role::ProtocolServer, Role::WindowManager) {
                return Err(ApplicationError::Protocol);
            }
            Capabilities::WINDOW_POLICY.bits()
                | Capabilities::MULTI_OUTPUT_POLICY.bits()
                | Capabilities::SCALE_METADATA.bits()
        }
        MessageType::POLICY_WINDOW_VRR_UPSERT | MessageType::POLICY_OUTPUT_VRR_UPSERT => {
            if roles != (Role::ProtocolServer, Role::WindowManager) {
                return Err(ApplicationError::Protocol);
            }
            Capabilities::WINDOW_POLICY.bits() | Capabilities::VRR_POLICY.bits()
        }
        MessageType::POLICY_WINDOW_VRR_STATE | MessageType::POLICY_OUTPUT_VRR_STATE => {
            if roles != (Role::WindowManager, Role::ProtocolServer) {
                return Err(ApplicationError::Protocol);
            }
            Capabilities::WINDOW_POLICY.bits() | Capabilities::VRR_POLICY.bits()
        }
        MessageType::PRESENTATION_TIMING => match roles {
            (Role::Compositor, Role::ProtocolServer) => Capabilities::PRESENTATION_TIMING.bits(),
            (Role::ProtocolServer, Role::DiagnosticTool) => {
                Capabilities::OUTPUT_CONTROL.bits() | Capabilities::PRESENTATION_TIMING.bits()
            }
            _ => return Err(ApplicationError::Protocol),
        },
        MessageType::OUTPUT_STATE_QUERY => match roles {
            (Role::ProtocolServer, Role::Compositor) => Capabilities::OUTPUT_MANAGEMENT.bits(),
            (Role::DiagnosticTool, Role::ProtocolServer) => Capabilities::OUTPUT_CONTROL.bits(),
            _ => return Err(ApplicationError::Protocol),
        },
        MessageType::OUTPUT_CONFIGURATION_COMMIT => {
            if roles != (Role::DiagnosticTool, Role::ProtocolServer) {
                return Err(ApplicationError::Protocol);
            }
            Capabilities::OUTPUT_CONTROL.bits()
        }
        MessageType::OUTPUT_CONFIGURATION_ACKNOWLEDGED => match roles {
            (Role::Compositor, Role::ProtocolServer) => Capabilities::OUTPUT_MANAGEMENT.bits(),
            (Role::ProtocolServer, Role::DiagnosticTool) => Capabilities::OUTPUT_CONTROL.bits(),
            _ => return Err(ApplicationError::Protocol),
        },
        _ => return Err(ApplicationError::UnsupportedMessage),
    };
    Ok(caps)
}

fn validate_output_flags_and_snapshot(
    message_type: MessageType,
    flags: MessageFlags,
    snapshot: SnapshotState,
    roles: (Role, Role),
) -> Result<(), ApplicationError> {
    let item = flags.bits() == MessageFlags::SNAPSHOT_ITEM.bits();
    let valid = match message_type {
        MessageType::OUTPUT_DESCRIPTOR_UPSERT
        | MessageType::OUTPUT_MODE_UPSERT
        | MessageType::OUTPUT_VRR_CAPABILITY_UPSERT => {
            item && snapshot.active && snapshot.domain == Some(SnapshotDomain::Outputs)
        }
        MessageType::OUTPUT_VRR_POLICY_UPSERT => {
            let domain = if roles == (Role::ProtocolServer, Role::Compositor) {
                SnapshotDomain::CompleteSession
            } else {
                SnapshotDomain::Outputs
            };
            item && snapshot.active && snapshot.domain == Some(domain)
        }
        MessageType::OUTPUT_VRR_STATE_UPSERT => {
            (roles == (Role::Compositor, Role::ProtocolServer)
                && flags.bits() == MessageFlags::REPLY.bits()
                && !snapshot.active)
                || (item && snapshot.active && snapshot.domain == Some(SnapshotDomain::Outputs))
        }
        MessageType::SURFACE_OUTPUT_STATE => {
            item && snapshot.active
                && (snapshot.domain == Some(SnapshotDomain::CompleteSession)
                    || (snapshot.domain == Some(SnapshotDomain::Outputs)
                        && roles == (Role::ProtocolServer, Role::DiagnosticTool)))
        }
        MessageType::SURFACE_VRR_STATE => {
            let domain = if roles == (Role::ProtocolServer, Role::Compositor) {
                SnapshotDomain::CompleteSession
            } else {
                SnapshotDomain::Outputs
            };
            item && snapshot.active && snapshot.domain == Some(domain)
        }
        MessageType::POLICY_OUTPUT_UPSERT
        | MessageType::POLICY_WINDOW_OUTPUT_HINT
        | MessageType::POLICY_WINDOW_VRR_UPSERT
        | MessageType::POLICY_OUTPUT_VRR_UPSERT
        | MessageType::POLICY_WINDOW_VRR_STATE
        | MessageType::POLICY_OUTPUT_VRR_STATE => {
            item && snapshot.active && snapshot.domain == Some(SnapshotDomain::WindowPolicy)
        }
        MessageType::PRESENTATION_TIMING => {
            if roles == (Role::Compositor, Role::ProtocolServer) {
                flags.bits() == 0 && !snapshot.active
            } else {
                item && snapshot.active && snapshot.domain == Some(SnapshotDomain::Outputs)
            }
        }
        MessageType::OUTPUT_STATE_QUERY | MessageType::OUTPUT_CONFIGURATION_COMMIT => {
            flags.bits() == MessageFlags::ACK_REQUIRED.bits() && !snapshot.active
        }
        MessageType::OUTPUT_CONFIGURATION_ACKNOWLEDGED => {
            flags.bits() == MessageFlags::REPLY.bits() && !snapshot.active
        }
        _ => false,
    };
    valid.then_some(()).ok_or(ApplicationError::Protocol)
}

fn valid_output_payload(message_type: MessageType, payload: &[u8]) -> bool {
    match message_type {
        MessageType::OUTPUT_DESCRIPTOR_UPSERT => decode_output_descriptor_upsert(payload).is_ok(),
        MessageType::OUTPUT_MODE_UPSERT => decode_output_mode_upsert(payload).is_ok(),
        MessageType::OUTPUT_VRR_CAPABILITY_UPSERT => {
            decode_output_vrr_capability_upsert(payload).is_ok()
        }
        MessageType::OUTPUT_VRR_POLICY_UPSERT => decode_output_vrr_policy_upsert(payload).is_ok(),
        MessageType::OUTPUT_VRR_STATE_UPSERT => decode_output_vrr_state_upsert(payload).is_ok(),
        MessageType::SURFACE_OUTPUT_STATE => decode_surface_output_state(payload).is_ok(),
        MessageType::SURFACE_VRR_STATE => decode_surface_vrr_state(payload).is_ok(),
        MessageType::POLICY_OUTPUT_UPSERT => decode_policy_output_upsert(payload).is_ok(),
        MessageType::POLICY_WINDOW_OUTPUT_HINT => decode_policy_window_output_hint(payload).is_ok(),
        MessageType::POLICY_WINDOW_VRR_UPSERT => decode_policy_window_vrr_upsert(payload).is_ok(),
        MessageType::POLICY_OUTPUT_VRR_UPSERT => decode_policy_output_vrr_upsert(payload).is_ok(),
        MessageType::POLICY_WINDOW_VRR_STATE => decode_policy_window_vrr_state(payload).is_ok(),
        MessageType::POLICY_OUTPUT_VRR_STATE => decode_policy_output_vrr_state(payload).is_ok(),
        MessageType::PRESENTATION_TIMING => decode_presentation_timing(payload).is_ok(),
        MessageType::OUTPUT_STATE_QUERY => decode_output_state_query(payload).is_ok(),
        MessageType::OUTPUT_CONFIGURATION_COMMIT => {
            decode_output_configuration_commit(payload).is_ok()
        }
        MessageType::OUTPUT_CONFIGURATION_ACKNOWLEDGED => {
            decode_output_configuration_acknowledged(payload).is_ok()
        }
        _ => false,
    }
}

fn synthetic_input_id(
    message_type: MessageType,
    payload: &[u8],
) -> Result<Option<u64>, ApplicationError> {
    let value = match message_type {
        MessageType::SYNTHETIC_MOTION => {
            decode_synthetic_motion(payload).map(|value| value.input_id)
        }
        MessageType::SYNTHETIC_BUTTON => {
            decode_synthetic_button(payload).map(|value| value.input_id)
        }
        MessageType::SYNTHETIC_KEY => decode_synthetic_key(payload).map(|value| value.input_id),
        MessageType::SYNTHETIC_BARRIER => {
            decode_synthetic_barrier(payload).map(|value| value.input_id)
        }
        _ => return Ok(None),
    };
    value.map(Some).map_err(|_| ApplicationError::Protocol)
}

fn output_request_identity(
    message_type: MessageType,
    payload: &[u8],
) -> Result<Option<OutputCorrelation>, ApplicationError> {
    let request_id = match message_type {
        MessageType::OUTPUT_STATE_QUERY => decode_output_state_query(payload)
            .map(|value| value.query_id)
            .map_err(|_| ApplicationError::Protocol)?,
        MessageType::OUTPUT_CONFIGURATION_COMMIT => decode_output_configuration_commit(payload)
            .map(|value| value.configuration_id)
            .map_err(|_| ApplicationError::Protocol)?,
        _ => return Ok(None),
    };
    Ok(Some(OutputCorrelation { request_id }))
}

fn insert_limited<K, V>(
    map: &mut HashMap<K, V>,
    key: K,
    value: V,
    limit: usize,
) -> Result<(), ApplicationError>
where
    K: Eq + std::hash::Hash,
{
    if map.len() >= limit {
        return Err(ApplicationError::LimitExceeded);
    }
    if map.insert(key, value).is_some() {
        return Err(ApplicationError::InvalidState);
    }
    Ok(())
}

fn take_matching<T: Copy + Eq>(
    map: &mut HashMap<u64, T>,
    key: u64,
    value: T,
) -> Result<(), ApplicationError> {
    if map.get(&key).copied() != Some(value) {
        return Err(ApplicationError::UnexpectedReply);
    }
    map.remove(&key);
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use gw_types::{GWIPC_WIRE_VERSION, Generation, Sequence, SnapshotId};
    use gw_wire::compositor::{
        AlphaSemantics, BufferAttach, FrameAcknowledged, FrameCommit, FrameResult, PixelFormat,
        SdrColorMetadata, SynchronizationMode, encode_buffer_attach, encode_frame_acknowledged,
        encode_frame_commit,
    };
    use gw_wire::{
        Ping, Pong, SnapshotBegin, SnapshotEnd, encode_ping, encode_pong, encode_snapshot_begin,
        encode_snapshot_end,
    };

    fn capabilities(values: &[Capabilities]) -> Capabilities {
        Capabilities::from_bits_retain(values.iter().fold(0, |bits, value| bits | value.bits()))
    }

    fn envelope(
        message_type: MessageType,
        flags: MessageFlags,
        sequence: u64,
        reply_to: u64,
        payload_size: usize,
        fd_count: usize,
    ) -> Envelope {
        Envelope {
            version: GWIPC_WIRE_VERSION,
            message_type,
            flags,
            payload_size: payload_size as u32,
            fd_count: fd_count as u16,
            sequence: Sequence::new(sequence),
            reply_to: Sequence::new(reply_to),
        }
    }

    fn buffer(sync: SynchronizationMode) -> Vec<u8> {
        encode_buffer_attach(&BufferAttach {
            buffer_id: 1,
            surface_id: 2,
            width: 16,
            height: 16,
            stride: 64,
            byte_offset: 0,
            storage_size: 1024,
            pixel_format: PixelFormat::Xrgb8888,
            modifier: 0,
            alpha_semantics: AlphaSemantics::Opaque,
            color: SdrColorMetadata::default(),
            synchronization: sync,
            flags: 0,
        })
    }

    #[test]
    fn buffer_attach_fd_shape_matches_legacy_modes() {
        let caps = capabilities(&[
            Capabilities::FD_PASSING,
            Capabilities::MEMFD_BUFFERS,
            Capabilities::CPU_BUFFER_SYNCHRONIZATION,
        ]);
        let mut validator =
            ApplicationValidator::new(Role::TestProducer, Role::TestConsumer, caps, 8);
        let plain = buffer(SynchronizationMode::None);
        validator
            .validate_outgoing(
                &envelope(
                    MessageType::BUFFER_ATTACH,
                    MessageFlags::default(),
                    1,
                    0,
                    plain.len(),
                    1,
                ),
                &plain,
                1,
            )
            .unwrap();
        let synchronized = buffer(SynchronizationMode::EventFd);
        assert_eq!(
            validator.validate_outgoing(
                &envelope(
                    MessageType::BUFFER_ATTACH,
                    MessageFlags::default(),
                    2,
                    0,
                    synchronized.len(),
                    1
                ),
                &synchronized,
                1,
            ),
            Err(ApplicationError::Protocol)
        );
        validator
            .validate_outgoing(
                &envelope(
                    MessageType::BUFFER_ATTACH,
                    MessageFlags::default(),
                    2,
                    0,
                    synchronized.len(),
                    2,
                ),
                &synchronized,
                2,
            )
            .unwrap();
    }

    #[test]
    fn all_non_buffer_messages_reject_descriptors() {
        let payload = encode_ping(Ping { nonce: 9 });
        let mut validator = ApplicationValidator::new(
            Role::TestProducer,
            Role::TestConsumer,
            Capabilities::default(),
            8,
        );
        assert_eq!(
            validator.validate_outgoing(
                &envelope(
                    MessageType::PING,
                    MessageFlags::ACK_REQUIRED,
                    1,
                    0,
                    payload.len(),
                    1
                ),
                &payload,
                1,
            ),
            Err(ApplicationError::Protocol)
        );
    }

    #[test]
    fn snapshots_enforce_lifecycle_domain_and_exact_counts() {
        let mut validator = ApplicationValidator::new(
            Role::ProtocolServer,
            Role::Compositor,
            Capabilities::SNAPSHOTS,
            8,
        );
        let invalid = encode_snapshot_begin(SnapshotBegin {
            snapshot_id: SnapshotId::new(1),
            domain: SnapshotDomain::Outputs,
            flags: 0,
            generation: Generation::new(2),
            expected_item_count: 0,
        });
        assert_eq!(
            validator.validate_outgoing(
                &envelope(
                    MessageType::SNAPSHOT_BEGIN,
                    MessageFlags::default(),
                    1,
                    0,
                    invalid.len(),
                    0
                ),
                &invalid,
                0,
            ),
            Err(ApplicationError::Protocol)
        );

        let begin = encode_snapshot_begin(SnapshotBegin {
            snapshot_id: SnapshotId::new(1),
            domain: SnapshotDomain::CompleteSession,
            flags: 0,
            generation: Generation::new(2),
            expected_item_count: 0,
        });
        validator
            .validate_outgoing(
                &envelope(
                    MessageType::SNAPSHOT_BEGIN,
                    MessageFlags::default(),
                    1,
                    0,
                    begin.len(),
                    0,
                ),
                &begin,
                0,
            )
            .unwrap();
        assert_eq!(
            validator.outgoing_snapshot().domain,
            Some(SnapshotDomain::CompleteSession)
        );
        let end = encode_snapshot_end(SnapshotEnd {
            snapshot_id: SnapshotId::new(1),
            generation: Generation::new(2),
            actual_item_count: 1,
        });
        assert_eq!(
            validator.validate_outgoing(
                &envelope(
                    MessageType::SNAPSHOT_END,
                    MessageFlags::default(),
                    2,
                    0,
                    end.len(),
                    0
                ),
                &end,
                0,
            ),
            Err(ApplicationError::Protocol)
        );
    }

    #[test]
    fn exact_flags_and_receive_sequence_match_legacy_rules() {
        let payload = encode_ping(Ping { nonce: 11 });
        let mut validator = ApplicationValidator::new(
            Role::ProtocolServer,
            Role::Compositor,
            Capabilities::default(),
            8,
        );
        assert_eq!(
            validator.validate_incoming(
                &envelope(
                    MessageType::PING,
                    MessageFlags::default(),
                    1,
                    0,
                    payload.len(),
                    0
                ),
                &payload,
                0,
            ),
            Err(ApplicationError::Protocol)
        );
        assert_eq!(
            validator.validate_incoming(
                &envelope(
                    MessageType::PING,
                    MessageFlags::ACK_REQUIRED,
                    1,
                    0,
                    payload.len(),
                    0
                ),
                &payload,
                0,
            ),
            Err(ApplicationError::OutOfOrderSequence)
        );
    }

    #[test]
    fn pong_reply_must_correlate_sequence_and_nonce() {
        let ping = encode_ping(Ping { nonce: 0x1234 });
        let mut validator = ApplicationValidator::new(
            Role::TestProducer,
            Role::TestConsumer,
            Capabilities::default(),
            8,
        );
        validator
            .validate_outgoing(
                &envelope(
                    MessageType::PING,
                    MessageFlags::ACK_REQUIRED,
                    1,
                    0,
                    ping.len(),
                    0,
                ),
                &ping,
                0,
            )
            .unwrap();
        let wrong = encode_pong(Pong { nonce: 0x9999 });
        assert_eq!(
            validator.validate_incoming(
                &envelope(MessageType::PONG, MessageFlags::REPLY, 1, 1, wrong.len(), 0),
                &wrong,
                0,
            ),
            Err(ApplicationError::UnexpectedReply)
        );
    }

    #[test]
    fn output_extension_role_and_capability_checks_are_distinct() {
        let payload = encode_snapshot_begin(SnapshotBegin {
            snapshot_id: SnapshotId::new(1),
            domain: SnapshotDomain::Outputs,
            flags: 0,
            generation: Generation::new(1),
            expected_item_count: 0,
        });
        let mut wrong_role = ApplicationValidator::new(
            Role::WindowManager,
            Role::ProtocolServer,
            Capabilities::SNAPSHOTS,
            8,
        );
        wrong_role
            .validate_outgoing(
                &envelope(
                    MessageType::SNAPSHOT_BEGIN,
                    MessageFlags::default(),
                    1,
                    0,
                    payload.len(),
                    0,
                ),
                &payload,
                0,
            )
            .unwrap();
        let descriptor = [0_u8; 8];
        assert_eq!(
            wrong_role.validate_outgoing(
                &envelope(
                    MessageType::OUTPUT_DESCRIPTOR_UPSERT,
                    MessageFlags::SNAPSHOT_ITEM,
                    2,
                    0,
                    descriptor.len(),
                    0,
                ),
                &descriptor,
                0,
            ),
            Err(ApplicationError::Protocol)
        );
    }

    #[test]
    fn frame_acknowledgement_correlates_sequence_and_commit_identity() {
        let commit = encode_frame_commit(&FrameCommit {
            commit_id: 41,
            output_id: 7,
            producer_generation: 3,
            flags: 0,
        });
        let acknowledged = encode_frame_acknowledged(&FrameAcknowledged {
            commit_id: 41,
            output_id: 7,
            presented_generation: 4,
            result: FrameResult::Accepted,
        });
        let mut validator = ApplicationValidator::new(
            Role::TestProducer,
            Role::TestConsumer,
            Capabilities::FRAME_ACKNOWLEDGEMENT,
            8,
        );
        validator
            .validate_outgoing(
                &envelope(
                    MessageType::FRAME_COMMIT,
                    MessageFlags::ACK_REQUIRED,
                    1,
                    0,
                    commit.len(),
                    0,
                ),
                &commit,
                0,
            )
            .unwrap();
        validator
            .validate_incoming(
                &envelope(
                    MessageType::FRAME_ACKNOWLEDGED,
                    MessageFlags::REPLY,
                    1,
                    1,
                    acknowledged.len(),
                    0,
                ),
                &acknowledged,
                0,
            )
            .unwrap();

        assert_eq!(
            validator.validate_incoming(
                &envelope(
                    MessageType::FRAME_ACKNOWLEDGED,
                    MessageFlags::REPLY,
                    2,
                    1,
                    acknowledged.len(),
                    0,
                ),
                &acknowledged,
                0,
            ),
            Err(ApplicationError::UnexpectedReply)
        );
    }

    #[test]
    fn outgoing_acknowledgement_requires_an_incoming_request() {
        let acknowledged = encode_frame_acknowledged(&FrameAcknowledged {
            commit_id: 99,
            output_id: 7,
            presented_generation: 4,
            result: FrameResult::Accepted,
        });
        let mut validator = ApplicationValidator::new(
            Role::ProtocolServer,
            Role::Compositor,
            Capabilities::FRAME_ACKNOWLEDGEMENT,
            8,
        );
        assert_eq!(
            validator.validate_outgoing(
                &envelope(
                    MessageType::FRAME_ACKNOWLEDGED,
                    MessageFlags::REPLY,
                    1,
                    7,
                    acknowledged.len(),
                    0,
                ),
                &acknowledged,
                0,
            ),
            Err(ApplicationError::InvalidState)
        );
    }
}
