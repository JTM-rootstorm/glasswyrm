use core::fmt;

use gw_types::{
    Capabilities, ConnectionId, GWIPC_WIRE_VERSION, MessageFlags, MessageType, RejectReason, Role,
    Sequence,
};
use gw_wire::{
    ControlDecodeError, ControlEncodeError, Envelope, Hello, Reject, Welcome, decode_hello,
    decode_reject, decode_welcome, encode_hello, encode_reject, encode_welcome,
};

use crate::{HARD_MAXIMUM_FDS, HARD_MAXIMUM_PAYLOAD, ReceivedRecord, TransportLimits};

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct HandshakeConfig {
    pub local_role: Role,
    /// Bit `1 << role` is set for every role this endpoint accepts.
    pub allowed_peer_roles: u64,
    pub offered_capabilities: Capabilities,
    pub required_peer_capabilities: Capabilities,
    pub limits: TransportLimits,
    pub instance_id: [u8; 16],
    pub label: String,
}

impl HandshakeConfig {
    #[must_use]
    pub fn new(local_role: Role, instance_id: [u8; 16], label: impl Into<String>) -> Self {
        Self {
            local_role,
            allowed_peer_roles: 0,
            offered_capabilities: Capabilities::default(),
            required_peer_capabilities: Capabilities::default(),
            limits: TransportLimits::default(),
            instance_id,
            label: label.into(),
        }
    }

    #[must_use]
    pub const fn allow_peer_role(mut self, role: Role) -> Self {
        self.allowed_peer_roles |= role_bit(role);
        self
    }

    #[must_use]
    pub const fn offer(mut self, capabilities: Capabilities) -> Self {
        self.offered_capabilities = capabilities;
        self
    }

    #[must_use]
    pub const fn require_peer(mut self, capabilities: Capabilities) -> Self {
        self.required_peer_capabilities = capabilities;
        self
    }

    fn validate(&self) -> Result<(), HandshakeError> {
        if self.local_role == Role::Unknown
            || self.instance_id == [0; 16]
            || self.label.len() > 64
            || self.limits.maximum_payload == 0
            || self.limits.maximum_payload > HARD_MAXIMUM_PAYLOAD
            || self.limits.maximum_fd_count > HARD_MAXIMUM_FDS
            || self.offered_capabilities.bits() & !Capabilities::KNOWN_MASK != 0
            || self.required_peer_capabilities.bits() & !Capabilities::KNOWN_MASK != 0
        {
            return Err(HandshakeError::InvalidConfig);
        }
        Ok(())
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct HandshakeRecord {
    pub envelope: Envelope,
    pub payload: Vec<u8>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct NegotiatedPeer {
    pub role: Role,
    pub capabilities: Capabilities,
    pub limits: TransportLimits,
    pub connection_id: ConnectionId,
    pub instance_id: [u8; 16],
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum ServerHandshakeResponse {
    Accepted {
        record: HandshakeRecord,
        peer: NegotiatedPeer,
    },
    Rejected {
        record: HandshakeRecord,
        reason: RejectReason,
    },
}

#[derive(Debug)]
pub enum HandshakeError {
    InvalidConfig,
    InvalidRecord,
    Encode(ControlEncodeError),
    Decode(ControlDecodeError),
    PeerRejected {
        reason: RejectReason,
        detail: String,
    },
}

impl fmt::Display for HandshakeError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::InvalidConfig => formatter.write_str("GWIPC handshake configuration is invalid"),
            Self::InvalidRecord => formatter.write_str("GWIPC handshake record is invalid"),
            Self::Encode(error) => write!(formatter, "cannot encode GWIPC handshake: {error}"),
            Self::Decode(error) => write!(formatter, "cannot decode GWIPC handshake: {error}"),
            Self::PeerRejected { reason, detail } => {
                write!(
                    formatter,
                    "GWIPC peer rejected the handshake ({reason:?}): {detail}"
                )
            }
        }
    }
}

impl std::error::Error for HandshakeError {
    fn source(&self) -> Option<&(dyn std::error::Error + 'static)> {
        match self {
            Self::Encode(error) => Some(error),
            Self::Decode(error) => Some(error),
            _ => None,
        }
    }
}

impl From<ControlEncodeError> for HandshakeError {
    fn from(error: ControlEncodeError) -> Self {
        Self::Encode(error)
    }
}

impl From<ControlDecodeError> for HandshakeError {
    fn from(error: ControlDecodeError) -> Self {
        Self::Decode(error)
    }
}

pub fn make_hello(config: &HandshakeConfig) -> Result<HandshakeRecord, HandshakeError> {
    config.validate()?;
    let payload = encode_hello(&Hello {
        minimum_version: GWIPC_WIRE_VERSION,
        maximum_version: GWIPC_WIRE_VERSION,
        sender_role: config.local_role,
        offered_capabilities: config.offered_capabilities,
        required_capabilities: config.required_peer_capabilities,
        maximum_payload: config.limits.maximum_payload,
        maximum_fd_count: config.limits.maximum_fd_count,
        sender_instance_id: config.instance_id,
        name: config.label.clone(),
    })?;
    Ok(HandshakeRecord {
        envelope: Envelope::request(MessageType::HELLO, Sequence::new(1), payload.len() as u32),
        payload,
    })
}

pub fn accept_hello(
    received: &ReceivedRecord,
    config: &HandshakeConfig,
    connection_id: ConnectionId,
) -> Result<ServerHandshakeResponse, HandshakeError> {
    config.validate()?;
    if connection_id.get() == 0 {
        return Err(HandshakeError::InvalidConfig);
    }
    if received.envelope.message_type != MessageType::HELLO
        || received.envelope.flags != no_flags()
        || received.envelope.reply_to.get() != 0
        || received.envelope.sequence.get() != 1
        || !received.fds.is_empty()
    {
        return rejection(
            received.envelope.sequence,
            RejectReason::InvalidHello,
            "invalid hello",
        );
    }

    let hello = match decode_hello(&received.payload) {
        Ok(hello) => hello,
        Err(_) => {
            return rejection(
                received.envelope.sequence,
                RejectReason::InvalidHello,
                "invalid hello",
            );
        }
    };
    if hello.minimum_version > GWIPC_WIRE_VERSION || hello.maximum_version < GWIPC_WIRE_VERSION {
        return rejection(
            received.envelope.sequence,
            RejectReason::IncompatibleVersion,
            "wire version mismatch",
        );
    }
    if config.allowed_peer_roles & role_bit(hello.sender_role) == 0 {
        return rejection(
            received.envelope.sequence,
            RejectReason::RoleNotAllowed,
            "peer role is not allowed",
        );
    }

    let negotiated_bits = hello.offered_capabilities.bits()
        & config.offered_capabilities.bits()
        & Capabilities::KNOWN_MASK;
    let negotiated = Capabilities::from_bits(negotiated_bits)
        .expect("intersection with the known mask contains only known capabilities");
    if !negotiated.contains(hello.required_capabilities)
        || !negotiated.contains(config.required_peer_capabilities)
    {
        return rejection(
            received.envelope.sequence,
            RejectReason::CapabilityMismatch,
            "required capability is unavailable",
        );
    }

    let limits = TransportLimits::new_unchecked(
        config.limits.maximum_payload.min(hello.maximum_payload),
        config.limits.maximum_fd_count.min(hello.maximum_fd_count),
    );
    let welcome = Welcome {
        selected_version: GWIPC_WIRE_VERSION,
        sender_role: config.local_role,
        negotiated_capabilities: negotiated,
        negotiated_maximum_payload: limits.maximum_payload,
        negotiated_maximum_fd_count: limits.maximum_fd_count,
        connection_id,
        sender_instance_id: config.instance_id,
    };
    let payload = encode_welcome(&welcome);
    let mut envelope =
        Envelope::request(MessageType::WELCOME, Sequence::new(1), payload.len() as u32);
    envelope.flags = MessageFlags::REPLY;
    envelope.reply_to = received.envelope.sequence;
    Ok(ServerHandshakeResponse::Accepted {
        record: HandshakeRecord { envelope, payload },
        peer: NegotiatedPeer {
            role: hello.sender_role,
            capabilities: negotiated,
            limits,
            connection_id,
            instance_id: hello.sender_instance_id,
        },
    })
}

pub fn validate_server_response(
    received: &ReceivedRecord,
    config: &HandshakeConfig,
) -> Result<NegotiatedPeer, HandshakeError> {
    config.validate()?;
    if received.envelope.flags != MessageFlags::REPLY
        || received.envelope.reply_to.get() != 1
        || received.envelope.sequence.get() != 1
        || !received.fds.is_empty()
    {
        return Err(HandshakeError::InvalidRecord);
    }
    if received.envelope.message_type == MessageType::REJECT {
        let reject = decode_reject(&received.payload)?;
        return Err(HandshakeError::PeerRejected {
            reason: reject.reason,
            detail: reject.detail,
        });
    }
    if received.envelope.message_type != MessageType::WELCOME {
        return Err(HandshakeError::InvalidRecord);
    }

    let welcome = decode_welcome(&received.payload)?;
    if config.allowed_peer_roles & role_bit(welcome.sender_role) == 0
        || welcome.negotiated_capabilities.bits() & !config.offered_capabilities.bits() != 0
        || !welcome
            .negotiated_capabilities
            .contains(config.required_peer_capabilities)
        || welcome.negotiated_maximum_payload > config.limits.maximum_payload
        || welcome.negotiated_maximum_fd_count > config.limits.maximum_fd_count
    {
        return Err(HandshakeError::InvalidRecord);
    }
    Ok(NegotiatedPeer {
        role: welcome.sender_role,
        capabilities: welcome.negotiated_capabilities,
        limits: TransportLimits::new_unchecked(
            welcome.negotiated_maximum_payload,
            welcome.negotiated_maximum_fd_count,
        ),
        connection_id: welcome.connection_id,
        instance_id: welcome.sender_instance_id,
    })
}

fn rejection(
    reply_to: Sequence,
    reason: RejectReason,
    detail: &str,
) -> Result<ServerHandshakeResponse, HandshakeError> {
    let payload = encode_reject(&Reject {
        reason,
        supported_minimum_version: GWIPC_WIRE_VERSION,
        supported_maximum_version: GWIPC_WIRE_VERSION,
        detail: detail.to_owned(),
    })?;
    let mut envelope =
        Envelope::request(MessageType::REJECT, Sequence::new(1), payload.len() as u32);
    envelope.flags = MessageFlags::REPLY;
    envelope.reply_to = reply_to;
    Ok(ServerHandshakeResponse::Rejected {
        record: HandshakeRecord { envelope, payload },
        reason,
    })
}

const fn role_bit(role: Role) -> u64 {
    1_u64 << role as u16
}

const fn no_flags() -> MessageFlags {
    match MessageFlags::from_bits(0) {
        Some(flags) => flags,
        None => unreachable!(),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::Transport;

    fn config(role: Role, id: u8, peer: Role) -> HandshakeConfig {
        HandshakeConfig::new(role, [id; 16], format!("{role:?}"))
            .allow_peer_role(peer)
            .offer(Capabilities::FD_PASSING.with(Capabilities::SNAPSHOTS))
            .require_peer(Capabilities::SNAPSHOTS)
    }

    fn transfer(record: &HandshakeRecord) -> ReceivedRecord {
        let (sender, receiver) = Transport::pair(TransportLimits::default()).unwrap();
        sender.send(&record.envelope, &record.payload, &[]).unwrap();
        loop {
            match receiver.receive() {
                Err(crate::TransportError::Io(error))
                    if error.kind() == std::io::ErrorKind::WouldBlock =>
                {
                    std::thread::yield_now();
                }
                result => return result.unwrap(),
            }
        }
    }

    #[test]
    fn hello_welcome_negotiates_legacy_limits_and_capabilities() {
        let client = config(Role::WindowManager, 1, Role::Compositor);
        let mut server = config(Role::Compositor, 2, Role::WindowManager);
        server.limits = TransportLimits::new(4096, 2).unwrap();
        let hello = transfer(&make_hello(&client).unwrap());
        let response = accept_hello(&hello, &server, ConnectionId::new(7)).unwrap();
        let (welcome, server_peer) = match response {
            ServerHandshakeResponse::Accepted { record, peer } => (record, peer),
            ServerHandshakeResponse::Rejected { .. } => panic!("valid hello was rejected"),
        };
        assert_eq!(server_peer.role, Role::WindowManager);
        assert_eq!(server_peer.limits, TransportLimits::new(4096, 2).unwrap());

        let client_peer = validate_server_response(&transfer(&welcome), &client).unwrap();
        assert_eq!(client_peer.role, Role::Compositor);
        assert_eq!(client_peer.connection_id, ConnectionId::new(7));
        assert_eq!(
            client_peer.capabilities,
            Capabilities::SNAPSHOTS.with(Capabilities::FD_PASSING)
        );
    }

    #[test]
    fn disallowed_role_gets_explicit_rejection() {
        let client = config(Role::DiagnosticTool, 1, Role::Compositor);
        let server = config(Role::Compositor, 2, Role::WindowManager);
        let hello = transfer(&make_hello(&client).unwrap());
        let response = accept_hello(&hello, &server, ConnectionId::new(1)).unwrap();
        let rejection = match response {
            ServerHandshakeResponse::Rejected { record, reason } => {
                assert_eq!(reason, RejectReason::RoleNotAllowed);
                record
            }
            ServerHandshakeResponse::Accepted { .. } => panic!("disallowed role was accepted"),
        };
        assert!(matches!(
            validate_server_response(&transfer(&rejection), &client),
            Err(HandshakeError::PeerRejected {
                reason: RejectReason::RoleNotAllowed,
                ..
            })
        ));
    }

    #[test]
    fn malformed_hello_is_rejected_without_decoding_partial_state() {
        let server = config(Role::Compositor, 2, Role::WindowManager);
        let mut client = config(Role::WindowManager, 1, Role::Compositor);
        client.required_peer_capabilities = Capabilities::default();
        let mut hello = transfer(&make_hello(&client).unwrap());
        hello.payload.truncate(10);
        assert!(matches!(
            accept_hello(&hello, &server, ConnectionId::new(1)).unwrap(),
            ServerHandshakeResponse::Rejected {
                reason: RejectReason::InvalidHello,
                ..
            }
        ));
    }

    #[test]
    fn capability_mismatch_is_explicit() {
        let client =
            config(Role::WindowManager, 1, Role::Compositor).require_peer(Capabilities::VRR_POLICY);
        let server = config(Role::Compositor, 2, Role::WindowManager);
        let hello = transfer(&make_hello(&client).unwrap());
        assert!(matches!(
            accept_hello(&hello, &server, ConnectionId::new(1)).unwrap(),
            ServerHandshakeResponse::Rejected {
                reason: RejectReason::CapabilityMismatch,
                ..
            }
        ));
    }
}
