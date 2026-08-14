use std::fmt;
use std::io;
use std::os::fd::OwnedFd;
use std::thread;
use std::time::Duration;

use gw_ipc::{
    HandshakeConfig, NegotiatedPeer, ServerHandshakeResponse, Transport, TransportError,
    TransportLimits, accept_hello,
};
use gw_types::{Capabilities, ConnectionId, MessageType, Role, Sequence};
use gw_wire::Envelope;

use crate::Options;
use crate::policy::{DispatchError, OutgoingRecord, PeerPolicy};
use crate::socket::{Listener, install_signal_handlers, stop_requested};

const MAXIMUM_MESSAGES_PER_TURN: usize = 64;
const MAXIMUM_PAYLOAD_BYTES_PER_TURN: usize = 512 * 1024;

#[derive(Debug)]
pub enum RuntimeError {
    Io(io::Error),
    Transport(TransportError),
    Handshake(gw_ipc::HandshakeError),
}

impl fmt::Display for RuntimeError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::Io(error) => write!(formatter, "runtime I/O failed: {error}"),
            Self::Transport(error) => write!(formatter, "GWIPC transport failed: {error}"),
            Self::Handshake(error) => write!(formatter, "GWIPC handshake failed: {error}"),
        }
    }
}

impl std::error::Error for RuntimeError {}

impl From<io::Error> for RuntimeError {
    fn from(error: io::Error) -> Self {
        Self::Io(error)
    }
}

impl From<TransportError> for RuntimeError {
    fn from(error: TransportError) -> Self {
        Self::Transport(error)
    }
}

impl From<gw_ipc::HandshakeError> for RuntimeError {
    fn from(error: gw_ipc::HandshakeError) -> Self {
        Self::Handshake(error)
    }
}

struct Connection {
    transport: Transport,
    negotiated: Option<NegotiatedPeer>,
    policy: PeerPolicy,
    expected_sequence: u64,
    next_sequence: u64,
}

impl Connection {
    fn new(fd: OwnedFd) -> Result<Self, RuntimeError> {
        Ok(Self {
            transport: Transport::from_owned_fd(fd, TransportLimits::default())?,
            negotiated: None,
            policy: PeerPolicy::new(),
            expected_sequence: 2,
            next_sequence: 2,
        })
    }

    fn handshake(
        &mut self,
        config: &HandshakeConfig,
        connection_id: ConnectionId,
    ) -> Result<HandshakeProgress, RuntimeError> {
        let received = match self.transport.receive() {
            Ok(received) => received,
            Err(error) if would_block(&error) => return Ok(HandshakeProgress::Waiting),
            Err(error) => return Err(error.into()),
        };
        match accept_hello(&received, config, connection_id)? {
            ServerHandshakeResponse::Accepted { record, peer } => {
                send_with_retry(&self.transport, &record.envelope, &record.payload)?;
                self.transport.set_limits(peer.limits)?;
                self.negotiated = Some(peer);
                Ok(HandshakeProgress::Accepted)
            }
            ServerHandshakeResponse::Rejected { record, .. } => {
                send_with_retry(&self.transport, &record.envelope, &record.payload)?;
                Ok(HandshakeProgress::Rejected)
            }
        }
    }

    fn process(&mut self) -> ProcessProgress {
        let Some(negotiated) = &self.negotiated else {
            return ProcessProgress::Disconnected;
        };
        let capabilities = negotiated.capabilities;
        let mut messages = 0;
        let mut bytes = 0;
        let mut accepted = 0;
        while messages < MAXIMUM_MESSAGES_PER_TURN && bytes < MAXIMUM_PAYLOAD_BYTES_PER_TURN {
            let received = match self.transport.receive() {
                Ok(received) => received,
                Err(error) if would_block(&error) => break,
                Err(TransportError::Disconnected) => return ProcessProgress::Disconnected,
                Err(error) => {
                    eprintln!("gwm: closing malformed GWIPC peer: {error}");
                    return ProcessProgress::Disconnected;
                }
            };
            messages += 1;
            bytes += received.payload.len();
            if !received.fds.is_empty()
                || received.envelope.sequence.get() != self.expected_sequence
            {
                eprintln!("gwm: closing peer after descriptor or sequence violation");
                return ProcessProgress::Disconnected;
            }
            self.expected_sequence += 1;
            let message_type = received.envelope.message_type;
            match self
                .policy
                .dispatch(&received.envelope, &received.payload, capabilities)
            {
                Ok(outcome) => {
                    for record in outcome.records {
                        if let Err(error) = self.send(record) {
                            eprintln!("gwm: response send failed: {error}");
                            return ProcessProgress::Disconnected;
                        }
                    }
                    accepted += usize::from(outcome.accepted);
                }
                Err(error) => {
                    log_dispatch_rejection(message_type, error);
                    if message_type == MessageType::POLICY_COMMIT {
                        return ProcessProgress::Disconnected;
                    }
                }
            }
        }
        ProcessProgress::Live { accepted }
    }

    fn send(&mut self, record: OutgoingRecord) -> Result<(), TransportError> {
        let mut envelope = Envelope::request(
            record.message_type,
            Sequence::new(self.next_sequence),
            u32::try_from(record.payload.len()).expect("wire payload size fits u32"),
        );
        envelope.flags = record.flags;
        envelope.reply_to = record.reply_to;
        send_with_retry(&self.transport, &envelope, &record.payload)?;
        self.next_sequence += 1;
        Ok(())
    }
}

enum HandshakeProgress {
    Waiting,
    Accepted,
    Rejected,
}

enum ProcessProgress {
    Live { accepted: usize },
    Disconnected,
}

pub fn run(options: &Options) -> Result<(), RuntimeError> {
    install_signal_handlers()?;
    let listener = Listener::bind(&options.ipc_socket)?;
    eprintln!("gwm: listening socket={}", options.ipc_socket.display());
    let config = handshake_config();
    let mut connection = None;
    let mut connection_id = 1_u64;
    let mut accepted_any = false;
    let mut accepted_commits = 0_u64;
    let mut stop_after_commit = false;
    while !stop_requested() && !stop_after_commit {
        if connection.is_none()
            && let Some(fd) = listener.accept()?
        {
            connection = Some(Connection::new(fd)?);
        }
        let mut disconnected = false;
        if let Some(active) = connection.as_mut() {
            if active.negotiated.is_none() {
                match active.handshake(&config, ConnectionId::new(connection_id)) {
                    Ok(HandshakeProgress::Waiting) => {}
                    Ok(HandshakeProgress::Accepted) => {
                        connection_id = connection_id.wrapping_add(1).max(1);
                        eprintln!("gwm: protocol server connected");
                    }
                    Ok(HandshakeProgress::Rejected) | Err(_) => disconnected = true,
                }
            } else {
                match active.process() {
                    ProcessProgress::Live { accepted } => {
                        if accepted != 0 {
                            accepted_any = true;
                            accepted_commits += accepted as u64;
                            if options
                                .max_commits
                                .is_some_and(|maximum| accepted_commits >= maximum)
                            {
                                stop_after_commit = true;
                            }
                        }
                    }
                    ProcessProgress::Disconnected => disconnected = true,
                }
            }
        }
        if disconnected {
            if let Some(active) = connection.as_mut() {
                active.policy.disconnect();
            }
            connection = None;
            eprintln!("gwm: protocol server disconnected");
            if options.once && accepted_any {
                break;
            }
        }
        if !stop_after_commit {
            thread::sleep(Duration::from_millis(2));
        }
    }
    // Match the legacy listener's post-flush grace period. Although
    // SOCK_SEQPACKET sends are atomic, closing immediately after the final
    // acknowledgement makes a poll-driven legacy peer observe HUP before it
    // drains the already queued reply records.
    if stop_after_commit {
        thread::sleep(Duration::from_secs(1));
    }
    drop(connection);
    drop(listener);
    eprintln!("gwm: stopped");
    Ok(())
}

fn handshake_config() -> HandshakeConfig {
    let required = Capabilities::default()
        .with(Capabilities::SNAPSHOTS)
        .with(Capabilities::WINDOW_POLICY);
    let offered = required
        .with(Capabilities::WINDOW_LIFECYCLE)
        .with(Capabilities::INTERACTIVE_POLICY)
        .with(Capabilities::MULTI_OUTPUT_POLICY)
        .with(Capabilities::SCALE_METADATA)
        .with(Capabilities::VRR_POLICY);
    let mut instance = [0_u8; 16];
    instance[..8].copy_from_slice(b"gwm-rust");
    instance[8..12].copy_from_slice(&std::process::id().to_le_bytes());
    HandshakeConfig::new(Role::WindowManager, instance, "gwm-rust")
        .allow_peer_role(Role::ProtocolServer)
        .offer(offered)
        .require_peer(required)
}

fn send_with_retry(
    transport: &Transport,
    envelope: &Envelope,
    payload: &[u8],
) -> Result<(), TransportError> {
    for _ in 0..5_000 {
        match transport.send(envelope, payload, &[]) {
            Ok(()) => return Ok(()),
            Err(error) if would_block(&error) => thread::sleep(Duration::from_millis(1)),
            Err(error) => return Err(error),
        }
    }
    Err(TransportError::Io(io::Error::new(
        io::ErrorKind::TimedOut,
        "GWIPC send remained blocked",
    )))
}

fn would_block(error: &TransportError) -> bool {
    matches!(error, TransportError::Io(error) if error.kind() == io::ErrorKind::WouldBlock)
}

fn log_dispatch_rejection(message_type: MessageType, error: DispatchError) {
    eprintln!(
        "gwm: rejected message type={:#06x}: {error}",
        message_type.get()
    );
}
