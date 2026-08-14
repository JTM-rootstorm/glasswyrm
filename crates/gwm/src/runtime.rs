use std::fmt;
use std::io;
use std::os::fd::OwnedFd;
use std::thread;
use std::time::{Duration, Instant};

use gw_ipc::{
    ApplicationError, ApplicationValidator, EndpointListener, HandshakeConfig, NegotiatedPeer,
    ServerHandshakeResponse, Transport, TransportError, TransportLimits, accept_hello,
};
use gw_types::{Capabilities, ConnectionId, MessageType, Role, Sequence};
use gw_wire::Envelope;

use crate::Options;
use crate::policy::{DispatchError, OutgoingRecord, PeerPolicy};
use crate::socket::{install_signal_handlers, stop_requested};

const MAXIMUM_MESSAGES_PER_TURN: usize = 64;
const MAXIMUM_PAYLOAD_BYTES_PER_TURN: usize = 512 * 1024;
const AWAITING_HELLO_TIMEOUT: Duration = Duration::from_secs(5);
const INITIAL_PROGRESS_TIMEOUT: Duration = Duration::from_secs(10);
const SNAPSHOT_TIMEOUT: Duration = Duration::from_secs(10);

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
struct PeerTimeouts {
    awaiting_hello: Duration,
    initial_progress: Duration,
    snapshot: Duration,
}

impl Default for PeerTimeouts {
    fn default() -> Self {
        Self {
            awaiting_hello: AWAITING_HELLO_TIMEOUT,
            initial_progress: INITIAL_PROGRESS_TIMEOUT,
            snapshot: SNAPSHOT_TIMEOUT,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
enum ExpiredDeadline {
    AwaitingHello,
    InitialProgress,
    Snapshot,
}

impl fmt::Display for ExpiredDeadline {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(match self {
            Self::AwaitingHello => "awaiting Hello",
            Self::InitialProgress => "awaiting initial policy commit",
            Self::Snapshot => "awaiting snapshot completion",
        })
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
struct PeerLiveness {
    timeouts: PeerTimeouts,
    // These are absolute monotonic deadlines. Receiving a partial or otherwise
    // valid record must not let a peer extend its ownership of the endpoint.
    awaiting_hello_deadline: Option<Instant>,
    initial_progress_deadline: Option<Instant>,
    snapshot_deadline: Option<Instant>,
}

impl PeerLiveness {
    fn awaiting_hello(now: Instant, timeouts: PeerTimeouts) -> Self {
        Self {
            timeouts,
            awaiting_hello_deadline: Some(deadline_after(now, timeouts.awaiting_hello)),
            initial_progress_deadline: None,
            snapshot_deadline: None,
        }
    }

    fn handshake_accepted(&mut self, now: Instant) {
        self.awaiting_hello_deadline = None;
        self.initial_progress_deadline = Some(deadline_after(now, self.timeouts.initial_progress));
    }

    fn dispatched(&mut self, now: Instant, accepted: bool, snapshot_active: bool) {
        // This is called only after application validation and policy dispatch
        // succeed, so invalid End/Abort records cannot disarm a snapshot timer.
        if snapshot_active && self.snapshot_deadline.is_none() {
            self.snapshot_deadline = Some(deadline_after(now, self.timeouts.snapshot));
        } else if !snapshot_active {
            self.snapshot_deadline = None;
        }
        if accepted {
            self.initial_progress_deadline = None;
        }
    }

    fn expired(&self, now: Instant) -> Option<ExpiredDeadline> {
        if self
            .awaiting_hello_deadline
            .is_some_and(|deadline| now >= deadline)
        {
            return Some(ExpiredDeadline::AwaitingHello);
        }
        if self
            .snapshot_deadline
            .is_some_and(|deadline| now >= deadline)
        {
            return Some(ExpiredDeadline::Snapshot);
        }
        self.initial_progress_deadline
            .is_some_and(|deadline| now >= deadline)
            .then_some(ExpiredDeadline::InitialProgress)
    }
}

fn deadline_after(now: Instant, duration: Duration) -> Instant {
    now.checked_add(duration).unwrap_or(now)
}

#[derive(Debug)]
pub enum RuntimeError {
    Io(io::Error),
    Transport(TransportError),
    Handshake(gw_ipc::HandshakeError),
    Application(ApplicationError),
}

impl fmt::Display for RuntimeError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::Io(error) => write!(formatter, "runtime I/O failed: {error}"),
            Self::Transport(error) => write!(formatter, "GWIPC transport failed: {error}"),
            Self::Handshake(error) => write!(formatter, "GWIPC handshake failed: {error}"),
            Self::Application(error) => {
                write!(formatter, "GWIPC application validation failed: {error}")
            }
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

impl From<ApplicationError> for RuntimeError {
    fn from(error: ApplicationError) -> Self {
        Self::Application(error)
    }
}

struct Connection {
    transport: Transport,
    negotiated: Option<NegotiatedPeer>,
    validator: Option<ApplicationValidator>,
    policy: PeerPolicy,
    next_sequence: u64,
    liveness: PeerLiveness,
}

impl Connection {
    fn new(fd: OwnedFd, now: Instant, timeouts: PeerTimeouts) -> Result<Self, RuntimeError> {
        Ok(Self {
            transport: Transport::from_owned_fd(fd, TransportLimits::default())?,
            negotiated: None,
            validator: None,
            policy: PeerPolicy::new(),
            next_sequence: 2,
            liveness: PeerLiveness::awaiting_hello(now, timeouts),
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
                self.validator = Some(ApplicationValidator::established(
                    config.local_role,
                    peer.role,
                    peer.capabilities,
                    MAXIMUM_MESSAGES_PER_TURN,
                ));
                self.negotiated = Some(peer);
                self.liveness.handshake_accepted(Instant::now());
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
            if let Some(expired) = self.liveness.expired(Instant::now()) {
                return ProcessProgress::Expired(expired);
            }
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
            let Some(validator) = self.validator.as_mut() else {
                return ProcessProgress::Disconnected;
            };
            if let Err(error) = validator.validate_incoming(
                &received.envelope,
                &received.payload,
                received.fds.len(),
            ) {
                eprintln!("gwm: closing peer after application violation: {error}");
                return ProcessProgress::Disconnected;
            }
            let message_type = received.envelope.message_type;
            match self
                .policy
                .dispatch(&received.envelope, &received.payload, capabilities)
            {
                Ok(outcome) => {
                    self.liveness.dispatched(
                        Instant::now(),
                        outcome.accepted,
                        self.policy.snapshot_active(),
                    );
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
        self.validator
            .as_mut()
            .ok_or_else(|| {
                TransportError::Io(io::Error::new(
                    io::ErrorKind::NotConnected,
                    "GWIPC application validator is not established",
                ))
            })?
            .validate_outgoing(&envelope, &record.payload, 0)
            .map_err(|error| {
                TransportError::Io(io::Error::new(io::ErrorKind::InvalidData, error))
            })?;
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
    Expired(ExpiredDeadline),
}

pub fn run(options: &Options) -> Result<(), RuntimeError> {
    install_signal_handlers()?;
    run_with_timeouts(options, PeerTimeouts::default())
}

fn run_with_timeouts(options: &Options, timeouts: PeerTimeouts) -> Result<(), RuntimeError> {
    let listener = EndpointListener::bind(&options.ipc_socket)?;
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
            connection = Some(Connection::new(fd, Instant::now(), timeouts)?);
        }
        let mut disconnected = false;
        if let Some(active) = connection.as_mut() {
            if let Some(expired) = active.liveness.expired(Instant::now()) {
                eprintln!("gwm: closing peer after {expired} deadline expired");
                disconnected = true;
            } else if active.negotiated.is_none() {
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
                    ProcessProgress::Expired(expired) => {
                        eprintln!("gwm: closing peer after {expired} deadline expired");
                        disconnected = true;
                    }
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

#[cfg(test)]
mod tests {
    use super::*;

    fn timeouts() -> PeerTimeouts {
        PeerTimeouts {
            awaiting_hello: Duration::from_millis(10),
            initial_progress: Duration::from_millis(20),
            snapshot: Duration::from_millis(30),
        }
    }

    #[test]
    fn awaiting_hello_deadline_is_absolute() {
        let started = Instant::now();
        let liveness = PeerLiveness::awaiting_hello(started, timeouts());

        assert_eq!(liveness.expired(started + Duration::from_millis(9)), None);
        assert_eq!(
            liveness.expired(started + Duration::from_millis(10)),
            Some(ExpiredDeadline::AwaitingHello)
        );
    }

    #[test]
    fn initial_progress_requires_an_accepted_commit() {
        let started = Instant::now();
        let mut liveness = PeerLiveness::awaiting_hello(started, timeouts());
        let established = started + Duration::from_millis(2);
        liveness.handshake_accepted(established);

        liveness.dispatched(established + Duration::from_millis(15), false, false);
        assert_eq!(
            liveness.expired(established + Duration::from_millis(19)),
            None
        );
        assert_eq!(
            liveness.expired(established + Duration::from_millis(20)),
            Some(ExpiredDeadline::InitialProgress)
        );

        liveness.dispatched(established + Duration::from_millis(19), true, false);
        assert_eq!(
            liveness.expired(established + Duration::from_secs(60)),
            None,
            "an established peer with accepted state may remain idle"
        );
    }

    #[test]
    fn snapshot_deadline_is_absolute_and_rearmed_by_the_next_snapshot() {
        let started = Instant::now();
        let mut liveness = PeerLiveness::awaiting_hello(started, timeouts());
        liveness.handshake_accepted(started);
        liveness.dispatched(started, true, false);

        let first_begin = started + Duration::from_millis(5);
        liveness.dispatched(first_begin, false, true);
        liveness.dispatched(first_begin + Duration::from_millis(29), false, true);
        assert_eq!(
            liveness.expired(first_begin + Duration::from_millis(30)),
            Some(ExpiredDeadline::Snapshot),
            "snapshot records must not refresh the deadline"
        );

        liveness.dispatched(first_begin + Duration::from_millis(20), false, false);
        assert_eq!(
            liveness.expired(first_begin + Duration::from_millis(50)),
            None,
            "a validated snapshot end or abort clears the deadline"
        );

        let second_begin = first_begin + Duration::from_millis(50);
        liveness.dispatched(second_begin, false, true);
        assert_eq!(
            liveness.expired(second_begin + Duration::from_millis(29)),
            None
        );
        assert_eq!(
            liveness.expired(second_begin + Duration::from_millis(30)),
            Some(ExpiredDeadline::Snapshot)
        );
    }
}
