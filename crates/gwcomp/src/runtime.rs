use std::collections::{BTreeMap, BTreeSet};
use std::fmt;
use std::fs::File;
use std::io::{self, Read};
use std::os::fd::{AsFd, OwnedFd};
use std::os::unix::fs::FileExt;
use std::thread;
use std::time::{Duration, Instant};

use gw_ipc::{
    ApplicationValidator, HandshakeConfig, NegotiatedPeer, ServerHandshakeResponse, Transport,
    TransportError, TransportLimits, accept_hello,
};
use gw_platform_linux::{MapAccess, Mapping, descriptor_flags};
use gw_types::{
    Capabilities, ConnectionId, Generation, MessageFlags, MessageType, Role, Sequence,
    SnapshotDomain, SnapshotId,
};
use gw_wire::compositor::{
    AlphaSemantics, BufferAttach, BufferDetach, BufferRelease, BufferReleaseReason,
    DamageRectangle, FrameAcknowledged, FrameResult, OutputUpsert, PixelFormat as WirePixelFormat,
    SurfaceDamage, SurfaceUpsert, SynchronizationMode, decode_buffer_attach, decode_buffer_detach,
    decode_frame_commit, decode_output_remove, decode_output_upsert, decode_surface_damage,
    decode_surface_remove, decode_surface_upsert, encode_buffer_release, encode_frame_acknowledged,
};
use gw_wire::output::{
    OutputConfigurationResult, SurfaceOutputState, decode_output_configuration_commit,
    decode_output_state_query, decode_surface_output_state,
};
use gw_wire::vrr::{
    OutputVrrPolicyUpsert, PresentationTiming, SurfaceVrrState, VRR_REASON_SIMULATED_HEADLESS,
    VrrDecision, decode_output_vrr_policy_upsert, decode_surface_vrr_state,
    encode_output_vrr_state_upsert, encode_presentation_timing,
};
use gw_wire::{
    Envelope, Pong, SnapshotBegin, SnapshotEnd, SurfacePolicyUpsert, decode_ping,
    decode_snapshot_abort, decode_snapshot_begin, decode_snapshot_end,
    decode_surface_policy_upsert, encode_pong, encode_snapshot_begin, encode_snapshot_end,
};
use gwcomp_core::{
    DamageFilterFootprint, DamageRegion, PixelFormat, RationalScale, Rectangle, Scene, SceneOutput,
    SceneSurface, SoftwareFrameSet, SoftwareRenderRequest, SurfaceBuffer, SurfaceOutputMembership,
    SurfacePresentation, map_logical_damage_to_native, render_software_scene,
    select_sampling_filter,
};

use crate::Options;
use crate::dump::FrameDumper;
use crate::inventory::Inventory;
use crate::manifest::SceneManifest;
use crate::socket::{Listener, install_signal_handlers, stop_requested};
use crate::vrr_report::VrrReport;

const MAXIMUM_MESSAGES_PER_TURN: usize = 64;
const MAXIMUM_PAYLOAD_BYTES_PER_TURN: usize = 512 * 1024;
const MAXIMUM_BUFFER_BYTES: u64 = 256 * 1024 * 1024;
const MAXIMUM_TOTAL_BUFFER_BYTES: u64 = 512 * 1024 * 1024;
const MAXIMUM_BUFFERS: usize = 4096;
const MAXIMUM_SURFACES: usize = 4096;
const MAXIMUM_OUTPUTS: usize = 8;
const MAXIMUM_SNAPSHOT_ITEMS: u32 = 1024;
const MAXIMUM_PENDING_RELEASES: usize = MAXIMUM_BUFFERS * 2;
const AWAITING_HELLO_TIMEOUT: Duration = Duration::from_secs(5);
const INITIAL_FRAME_TIMEOUT: Duration = Duration::from_secs(10);
const SNAPSHOT_TIMEOUT: Duration = Duration::from_secs(10);
const VRR_REASON_POLICY_OFF: u64 = 1 << 10;
const VRR_REASON_NO_CANDIDATE: u64 = 1 << 11;
const VRR_REASON_MANUAL_ALWAYS_ELIGIBLE: u64 = 1 << 32;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
struct PeerTimeouts {
    awaiting_hello: Duration,
    initial_frame: Duration,
    snapshot: Duration,
}

impl Default for PeerTimeouts {
    fn default() -> Self {
        Self {
            awaiting_hello: AWAITING_HELLO_TIMEOUT,
            initial_frame: INITIAL_FRAME_TIMEOUT,
            snapshot: SNAPSHOT_TIMEOUT,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
enum ExpiredDeadline {
    AwaitingHello,
    InitialFrame,
    Snapshot,
}

impl fmt::Display for ExpiredDeadline {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(match self {
            Self::AwaitingHello => "awaiting Hello",
            Self::InitialFrame => "awaiting initial accepted frame",
            Self::Snapshot => "awaiting snapshot completion",
        })
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
struct PeerLiveness {
    timeouts: PeerTimeouts,
    // These are absolute monotonic deadlines. Partial records and other
    // successful traffic must not extend exclusive ownership of the endpoint.
    awaiting_hello_deadline: Option<Instant>,
    initial_frame_deadline: Option<Instant>,
    snapshot_deadline: Option<Instant>,
}

impl PeerLiveness {
    fn awaiting_hello(now: Instant, timeouts: PeerTimeouts) -> Self {
        Self {
            timeouts,
            awaiting_hello_deadline: Some(deadline_after(now, timeouts.awaiting_hello)),
            initial_frame_deadline: None,
            snapshot_deadline: None,
        }
    }

    fn handshake_accepted(&mut self, now: Instant) {
        self.awaiting_hello_deadline = None;
        self.initial_frame_deadline = Some(deadline_after(now, self.timeouts.initial_frame));
    }

    fn dispatched(&mut self, now: Instant, accepted_frame: bool, snapshot_active: bool) {
        if snapshot_active && self.snapshot_deadline.is_none() {
            self.snapshot_deadline = Some(deadline_after(now, self.timeouts.snapshot));
        } else if !snapshot_active {
            self.snapshot_deadline = None;
        }
        if accepted_frame {
            self.initial_frame_deadline = None;
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
        self.initial_frame_deadline
            .is_some_and(|deadline| now >= deadline)
            .then_some(ExpiredDeadline::InitialFrame)
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
    Wire(&'static str),
}
impl fmt::Display for RuntimeError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::Io(error) => write!(formatter, "runtime I/O failed: {error}"),
            Self::Transport(error) => write!(formatter, "GWIPC transport failed: {error}"),
            Self::Handshake(error) => write!(formatter, "GWIPC handshake failed: {error}"),
            Self::Wire(error) => formatter.write_str(error),
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

#[derive(Debug)]
struct BufferRecord {
    id: u64,
    surface_id: u64,
    attachment: BufferAttach,
    storage: File,
    synchronization: Option<File>,
    buffer: Option<SurfaceBuffer>,
}

#[derive(Default)]
struct PeerState {
    snapshot: Option<SnapshotTransaction>,
    outputs: BTreeMap<u64, OutputUpsert>,
    surfaces: BTreeMap<u64, SurfaceUpsert>,
    policies: BTreeMap<u64, SurfacePolicyUpsert>,
    memberships: BTreeMap<u64, SurfaceOutputState>,
    surface_vrr: BTreeMap<u64, SurfaceVrrState>,
    buffers: BTreeMap<u64, BufferRecord>,
    surface_buffers: BTreeMap<u64, u64>,
    damaged_surfaces: BTreeMap<u64, Vec<DamageRectangle>>,
    mutated_buffers: BTreeSet<u64>,
    scene_changed: bool,
    releases: Vec<BufferRelease>,
    previous: Option<SoftwareFrameSet>,
    configuration_generation: u64,
}

impl PeerState {
    fn contains_current_surface(&self, surface_id: u64) -> bool {
        self.snapshot.as_ref().map_or_else(
            || self.surfaces.contains_key(&surface_id),
            |snapshot| snapshot.surfaces.contains_key(&surface_id),
        )
    }
}

struct SnapshotTransaction {
    begin: SnapshotBegin,
    outputs: BTreeMap<u64, OutputUpsert>,
    surfaces: BTreeMap<u64, SurfaceUpsert>,
    policies: BTreeMap<u64, SurfacePolicyUpsert>,
    memberships: BTreeMap<u64, SurfaceOutputState>,
    surface_vrr: BTreeMap<u64, SurfaceVrrState>,
    output_vrr_policies: BTreeMap<u64, OutputVrrPolicyUpsert>,
    buffer_ids: BTreeSet<u64>,
    buffer_storage: u64,
    mutations: Vec<SnapshotMutation>,
}

enum SnapshotMutation {
    SurfaceBuffersRemove(u64),
    BufferAttach(BufferRecord),
    BufferDetach(BufferDetach),
}

impl SnapshotTransaction {
    fn new(begin: SnapshotBegin) -> Result<Self, RuntimeError> {
        if begin.expected_item_count > MAXIMUM_SNAPSHOT_ITEMS {
            return Err(RuntimeError::Wire("snapshot item limit exceeded"));
        }
        Ok(Self {
            begin,
            outputs: BTreeMap::new(),
            surfaces: BTreeMap::new(),
            policies: BTreeMap::new(),
            memberships: BTreeMap::new(),
            surface_vrr: BTreeMap::new(),
            output_vrr_policies: BTreeMap::new(),
            buffer_ids: BTreeSet::new(),
            buffer_storage: 0,
            mutations: Vec::new(),
        })
    }
}

struct RuntimeState {
    inventory: Inventory,
    snapshot_id: u64,
    frame_ordinal: u64,
    accepted_frames: u64,
    dumper: Option<FrameDumper>,
    manifest: Option<SceneManifest>,
    vrr_report: Option<VrrReport>,
}

struct Connection {
    transport: Transport,
    negotiated: Option<NegotiatedPeer>,
    validator: Option<ApplicationValidator>,
    peer: PeerState,
    next_sequence: u64,
    liveness: PeerLiveness,
}

impl Connection {
    fn new(fd: OwnedFd, now: Instant, timeouts: PeerTimeouts) -> Result<Self, RuntimeError> {
        Ok(Self {
            transport: Transport::from_owned_fd(fd, TransportLimits::default())?,
            negotiated: None,
            validator: None,
            peer: PeerState::default(),
            next_sequence: 2,
            liveness: PeerLiveness::awaiting_hello(now, timeouts),
        })
    }

    fn handshake(
        &mut self,
        config: &HandshakeConfig,
        connection_id: ConnectionId,
        explicit_outputs: bool,
    ) -> Result<HandshakeProgress, RuntimeError> {
        let received = match self.transport.receive() {
            Ok(received) => received,
            Err(error) if would_block(&error) => return Ok(HandshakeProgress::Waiting),
            Err(error) => return Err(error.into()),
        };
        match accept_hello(&received, config, connection_id)? {
            ServerHandshakeResponse::Accepted { record, peer } => {
                send_with_retry(&self.transport, &record.envelope, &record.payload, &[])?;
                if !valid_peer_profile(&peer)
                    || (explicit_outputs
                        && !peer.capabilities.contains(output_model_capabilities()))
                {
                    return Ok(HandshakeProgress::Rejected);
                }
                self.transport.set_limits(peer.limits)?;
                self.validator = Some(ApplicationValidator::established(
                    Role::Compositor,
                    peer.role,
                    peer.capabilities,
                    MAXIMUM_SNAPSHOT_ITEMS as usize,
                ));
                self.negotiated = Some(peer);
                self.liveness.handshake_accepted(Instant::now());
                Ok(HandshakeProgress::Accepted)
            }
            ServerHandshakeResponse::Rejected { record, .. } => {
                send_with_retry(&self.transport, &record.envelope, &record.payload, &[])?;
                Ok(HandshakeProgress::Rejected)
            }
        }
    }

    fn process(&mut self, state: &mut RuntimeState) -> ProcessProgress {
        let Some(negotiated) = self.negotiated.clone() else {
            return ProcessProgress::Disconnected;
        };
        let mut messages = 0;
        let mut bytes = 0;
        let accepted_before = state.accepted_frames;
        while messages < MAXIMUM_MESSAGES_PER_TURN && bytes < MAXIMUM_PAYLOAD_BYTES_PER_TURN {
            if let Some(expired) = self.liveness.expired(Instant::now()) {
                return ProcessProgress::Expired(expired);
            }
            let mut received = match self.transport.receive() {
                Ok(record) => record,
                Err(error) if would_block(&error) => break,
                Err(TransportError::Disconnected) => return ProcessProgress::Disconnected,
                Err(error) => {
                    eprintln!("gwcomp: closing malformed GWIPC peer: {error}");
                    return ProcessProgress::Disconnected;
                }
            };
            messages += 1;
            bytes += received.payload.len();
            if let Some(validator) = self.validator.as_mut()
                && let Err(error) = validator.validate_incoming(
                    &received.envelope,
                    &received.payload,
                    received.fds.len(),
                )
            {
                eprintln!(
                    "gwcomp: closing invalid application peer type={:#06x} sequence={}: {error}",
                    received.envelope.message_type.get(),
                    received.envelope.sequence.get(),
                );
                return ProcessProgress::Disconnected;
            }
            let envelope = received.envelope;
            let accepted_before_message = state.accepted_frames;
            let result = self.dispatch(
                &envelope,
                &received.payload,
                &mut received.fds,
                &negotiated,
                state,
            );
            match result {
                Ok(()) => {
                    let release_point = matches!(
                        envelope.message_type,
                        MessageType::FRAME_COMMIT | MessageType::SNAPSHOT_ABORT
                    );
                    if release_point && let Err(error) = self.flush_releases() {
                        eprintln!("gwcomp: buffer release send failed: {error}");
                        return ProcessProgress::Disconnected;
                    }
                    self.liveness.dispatched(
                        Instant::now(),
                        state.accepted_frames != accepted_before_message,
                        self.peer.snapshot.is_some(),
                    );
                }
                Err(error) => {
                    eprintln!(
                        "gwcomp: rejected contract type={:#06x}: {error}",
                        envelope.message_type.get()
                    );
                    return ProcessProgress::Disconnected;
                }
            }
        }
        ProcessProgress::Live {
            accepted: state.accepted_frames - accepted_before,
        }
    }

    #[allow(clippy::too_many_lines)]
    fn dispatch(
        &mut self,
        envelope: &Envelope,
        payload: &[u8],
        fds: &mut Vec<OwnedFd>,
        negotiated: &NegotiatedPeer,
        state: &mut RuntimeState,
    ) -> Result<(), RuntimeError> {
        match envelope.message_type {
            MessageType::PING => {
                let ping = decode_ping(payload).map_err(|_| RuntimeError::Wire("invalid Ping"))?;
                self.send(
                    MessageType::PONG,
                    MessageFlags::REPLY,
                    envelope.sequence,
                    encode_pong(Pong { nonce: ping.nonce }),
                )
            }
            MessageType::OUTPUT_STATE_QUERY => {
                self.publish_inventory(envelope, payload, negotiated.capabilities, state)
            }
            MessageType::SNAPSHOT_BEGIN => {
                let begin = decode_snapshot_begin(payload)
                    .map_err(|_| RuntimeError::Wire("invalid SnapshotBegin"))?;
                self.peer.snapshot = Some(SnapshotTransaction::new(begin)?);
                Ok(())
            }
            MessageType::SNAPSHOT_END => self.finish_snapshot(payload, state),
            MessageType::SNAPSHOT_ABORT => {
                let abort = decode_snapshot_abort(payload)
                    .map_err(|_| RuntimeError::Wire("invalid SnapshotAbort"))?;
                if self
                    .peer
                    .snapshot
                    .as_ref()
                    .is_some_and(|snapshot| snapshot.begin.snapshot_id == abort.snapshot_id)
                {
                    let snapshot = self.peer.snapshot.take().expect("snapshot was checked");
                    self.abort_snapshot(snapshot)?;
                }
                Ok(())
            }
            MessageType::OUTPUT_UPSERT => {
                let value = decode_output_upsert(payload)
                    .map_err(|_| RuntimeError::Wire("invalid OutputUpsert"))?;
                if let Some(snapshot) = self.peer.snapshot.as_mut() {
                    if !snapshot.outputs.contains_key(&value.output_id)
                        && snapshot.outputs.len() == MAXIMUM_OUTPUTS
                    {
                        return Err(RuntimeError::Wire("snapshot has too many outputs"));
                    }
                    snapshot.outputs.insert(value.output_id, value);
                } else {
                    if !self.peer.outputs.contains_key(&value.output_id)
                        && self.peer.outputs.len() == MAXIMUM_OUTPUTS
                    {
                        return Err(RuntimeError::Wire("scene has too many outputs"));
                    }
                    self.peer.outputs.insert(value.output_id, value);
                    self.peer.scene_changed = true;
                }
                Ok(())
            }
            MessageType::OUTPUT_REMOVE => {
                let value = decode_output_remove(payload)
                    .map_err(|_| RuntimeError::Wire("invalid OutputRemove"))?;
                if let Some(snapshot) = self.peer.snapshot.as_mut() {
                    snapshot.outputs.remove(&value.output_id);
                    snapshot.output_vrr_policies.remove(&value.output_id);
                } else {
                    self.peer.outputs.remove(&value.output_id);
                    self.peer.scene_changed = true;
                }
                Ok(())
            }
            MessageType::SURFACE_UPSERT => {
                let value = decode_surface_upsert(payload)
                    .map_err(|_| RuntimeError::Wire("invalid SurfaceUpsert"))?;
                if let Some(snapshot) = self.peer.snapshot.as_mut() {
                    if !snapshot.surfaces.contains_key(&value.surface_id)
                        && snapshot.surfaces.len() == MAXIMUM_SURFACES
                    {
                        return Err(RuntimeError::Wire("snapshot has too many surfaces"));
                    }
                    snapshot.surfaces.insert(value.surface_id, value);
                } else {
                    if !self.peer.surfaces.contains_key(&value.surface_id)
                        && self.peer.surfaces.len() == MAXIMUM_SURFACES
                    {
                        return Err(RuntimeError::Wire("scene has too many surfaces"));
                    }
                    self.peer.surfaces.insert(value.surface_id, value);
                    self.peer.scene_changed = true;
                }
                Ok(())
            }
            MessageType::SURFACE_OUTPUT_STATE => {
                let value = decode_surface_output_state(payload)
                    .map_err(|_| RuntimeError::Wire("invalid SurfaceOutputState"))?;
                if !self.peer.contains_current_surface(value.surface_id) {
                    return Err(RuntimeError::Wire(
                        "surface membership references an unknown surface",
                    ));
                }
                if let Some(snapshot) = self.peer.snapshot.as_mut() {
                    snapshot.memberships.insert(value.surface_id, value);
                } else {
                    self.peer.memberships.insert(value.surface_id, value);
                    self.peer.scene_changed = true;
                }
                Ok(())
            }
            MessageType::SURFACE_REMOVE => {
                let value = decode_surface_remove(payload)
                    .map_err(|_| RuntimeError::Wire("invalid SurfaceRemove"))?;
                if let Some(snapshot) = self.peer.snapshot.as_mut() {
                    snapshot.surfaces.remove(&value.surface_id);
                    snapshot.policies.remove(&value.surface_id);
                    snapshot.memberships.remove(&value.surface_id);
                    snapshot.surface_vrr.remove(&value.surface_id);
                    stage_snapshot_mutation(
                        snapshot,
                        SnapshotMutation::SurfaceBuffersRemove(value.surface_id),
                    )?;
                } else {
                    self.remove_surface(value.surface_id, BufferReleaseReason::SurfaceRemoved)?;
                    self.peer.scene_changed = true;
                }
                Ok(())
            }
            MessageType::BUFFER_ATTACH => self.attach_buffer(payload, fds),
            MessageType::BUFFER_DETACH => {
                let value = decode_buffer_detach(payload)
                    .map_err(|_| RuntimeError::Wire("invalid BufferDetach"))?;
                if let Some(snapshot) = self.peer.snapshot.as_mut() {
                    stage_snapshot_mutation(snapshot, SnapshotMutation::BufferDetach(value))?;
                } else {
                    self.detach_buffer(value);
                }
                Ok(())
            }
            MessageType::SURFACE_DAMAGE => {
                let damage = decode_surface_damage(payload)
                    .map_err(|_| RuntimeError::Wire("invalid SurfaceDamage"))?;
                if !self.peer.contains_current_surface(damage.surface_id) {
                    return Err(RuntimeError::Wire("damage references an unknown surface"));
                }
                if self.peer.snapshot.is_some() {
                    // A committed complete snapshot forces full scene damage, so retaining
                    // per-item damage here would only grow transaction memory.
                } else {
                    self.apply_surface_damage(damage);
                }
                Ok(())
            }
            MessageType::OUTPUT_VRR_POLICY_UPSERT => {
                let policy = decode_output_vrr_policy_upsert(payload)
                    .map_err(|_| RuntimeError::Wire("invalid OutputVrrPolicyUpsert"))?;
                if !state.inventory.outputs.contains_key(&policy.output_id) {
                    return Err(RuntimeError::Wire(
                        "output VRR policy references an unknown output",
                    ));
                }
                if let Some(snapshot) = self.peer.snapshot.as_mut() {
                    if !snapshot.output_vrr_policies.contains_key(&policy.output_id)
                        && snapshot.output_vrr_policies.len() == MAXIMUM_OUTPUTS
                    {
                        return Err(RuntimeError::Wire(
                            "snapshot has too many output VRR policies",
                        ));
                    }
                    snapshot
                        .output_vrr_policies
                        .insert(policy.output_id, policy);
                } else {
                    Self::apply_output_vrr_policy(policy, state)?;
                    self.peer.scene_changed = true;
                }
                Ok(())
            }
            MessageType::SURFACE_VRR_STATE => {
                let value = decode_surface_vrr_state(payload)
                    .map_err(|_| RuntimeError::Wire("invalid SurfaceVrrState"))?;
                if !self.peer.contains_current_surface(value.surface_id) {
                    return Err(RuntimeError::Wire(
                        "surface VRR state references an unknown surface",
                    ));
                }
                if let Some(snapshot) = self.peer.snapshot.as_mut() {
                    snapshot.surface_vrr.insert(value.surface_id, value);
                } else {
                    self.peer.surface_vrr.insert(value.surface_id, value);
                    self.peer.scene_changed = true;
                }
                Ok(())
            }
            MessageType::OUTPUT_CONFIGURATION_COMMIT => {
                self.commit_output_configuration(envelope, payload, state)
            }
            MessageType::FRAME_COMMIT => self.commit_frame(envelope, payload, state),
            MessageType::SURFACE_POLICY_UPSERT => {
                let value = decode_surface_policy_upsert(payload)
                    .map_err(|_| RuntimeError::Wire("invalid SurfacePolicyUpsert"))?;
                if !self.peer.contains_current_surface(value.surface_id) {
                    return Err(RuntimeError::Wire(
                        "surface policy references an unknown surface",
                    ));
                }
                if let Some(snapshot) = self.peer.snapshot.as_mut() {
                    snapshot.policies.insert(value.surface_id, value);
                } else {
                    self.peer.policies.insert(value.surface_id, value);
                    self.peer.scene_changed = true;
                }
                Ok(())
            }
            _ => Err(RuntimeError::Wire("unsupported compositor message")),
        }
    }

    fn finish_snapshot(
        &mut self,
        payload: &[u8],
        state: &mut RuntimeState,
    ) -> Result<(), RuntimeError> {
        let end =
            decode_snapshot_end(payload).map_err(|_| RuntimeError::Wire("invalid SnapshotEnd"))?;
        let snapshot = self
            .peer
            .snapshot
            .take()
            .ok_or(RuntimeError::Wire("SnapshotEnd without begin"))?;
        if snapshot.begin.snapshot_id != end.snapshot_id
            || snapshot.begin.generation != end.generation
        {
            return Err(RuntimeError::Wire("snapshot identity mismatch"));
        }
        if snapshot.begin.domain == SnapshotDomain::CompleteSession {
            self.validate_snapshot_buffers(&snapshot)?;
            let retained: BTreeSet<_> = snapshot.surfaces.keys().copied().collect();
            let removed_buffers: Vec<_> = self
                .peer
                .buffers
                .values()
                .filter(|buffer| !retained.contains(&buffer.surface_id))
                .map(|buffer| buffer.id)
                .collect();
            for buffer_id in removed_buffers {
                self.release_buffer(buffer_id, BufferReleaseReason::SurfaceRemoved)?;
            }
            let removed: Vec<_> = self
                .peer
                .surfaces
                .keys()
                .filter(|id| !retained.contains(id))
                .copied()
                .collect();
            for surface_id in removed {
                self.remove_surface(surface_id, BufferReleaseReason::SurfaceRemoved)?;
            }
            self.peer.outputs = snapshot.outputs;
            self.peer.surfaces = snapshot.surfaces;
            self.peer.policies = snapshot.policies;
            self.peer.memberships = snapshot.memberships;
            self.peer.surface_vrr = snapshot.surface_vrr;
            self.peer.configuration_generation = snapshot.begin.generation.get();
            for policy in snapshot.output_vrr_policies.into_values() {
                Self::apply_output_vrr_policy(policy, state)?;
            }
            for mutation in snapshot.mutations {
                match mutation {
                    SnapshotMutation::SurfaceBuffersRemove(surface_id) => {
                        self.remove_surface_buffers(
                            surface_id,
                            BufferReleaseReason::SurfaceRemoved,
                        )?;
                    }
                    SnapshotMutation::BufferAttach(record) => {
                        self.apply_buffer_record(record)?;
                    }
                    SnapshotMutation::BufferDetach(detachment) => {
                        self.detach_buffer(detachment);
                    }
                }
            }
            self.peer.scene_changed = true;
        } else if snapshot.begin.domain == SnapshotDomain::Outputs {
            self.peer.snapshot = Some(snapshot);
        }
        Ok(())
    }

    fn validate_snapshot_buffers(
        &self,
        snapshot: &SnapshotTransaction,
    ) -> Result<(), RuntimeError> {
        let retained: BTreeSet<_> = snapshot.surfaces.keys().copied().collect();
        let mut projected: BTreeMap<u64, (u64, u64)> = self
            .peer
            .buffers
            .values()
            .filter(|buffer| retained.contains(&buffer.surface_id))
            .map(|buffer| {
                (
                    buffer.id,
                    (buffer.surface_id, buffer.attachment.storage_size),
                )
            })
            .collect();
        let mut surface_buffers: BTreeMap<_, _> = self
            .peer
            .surface_buffers
            .iter()
            .filter(|(surface_id, buffer_id)| {
                retained.contains(surface_id) && projected.contains_key(buffer_id)
            })
            .map(|(&surface_id, &buffer_id)| (surface_id, buffer_id))
            .collect();
        for mutation in &snapshot.mutations {
            match mutation {
                SnapshotMutation::SurfaceBuffersRemove(surface_id) => {
                    surface_buffers.remove(surface_id);
                    projected.retain(|_, (owner, _)| owner != surface_id);
                }
                SnapshotMutation::BufferAttach(record) => {
                    if let Some(previous) = surface_buffers.insert(record.surface_id, record.id) {
                        projected.remove(&previous);
                    }
                    projected.insert(
                        record.id,
                        (record.surface_id, record.attachment.storage_size),
                    );
                }
                SnapshotMutation::BufferDetach(detachment) => {
                    if surface_buffers.get(&detachment.surface_id) == Some(&detachment.buffer_id) {
                        surface_buffers.remove(&detachment.surface_id);
                    }
                }
            }
        }
        if projected.len() > MAXIMUM_BUFFERS {
            return Err(RuntimeError::Wire("snapshot has too many live buffers"));
        }
        let storage = projected
            .values()
            .try_fold(0_u64, |total, (_, size)| total.checked_add(*size));
        if storage.is_none_or(|total| total > MAXIMUM_TOTAL_BUFFER_BYTES) {
            return Err(RuntimeError::Wire(
                "snapshot live buffer storage limit exceeded",
            ));
        }
        Ok(())
    }

    fn abort_snapshot(&mut self, snapshot: SnapshotTransaction) -> Result<(), RuntimeError> {
        for mutation in snapshot.mutations {
            if let SnapshotMutation::BufferAttach(record) = mutation {
                self.queue_release(record.id, BufferReleaseReason::Invalid)?;
            }
        }
        Ok(())
    }

    fn apply_output_vrr_policy(
        policy: OutputVrrPolicyUpsert,
        state: &mut RuntimeState,
    ) -> Result<(), RuntimeError> {
        let output =
            state
                .inventory
                .outputs
                .get_mut(&policy.output_id)
                .ok_or(RuntimeError::Wire(
                    "output VRR policy references an unknown output",
                ))?;
        output.vrr_policy = policy;
        output.vrr_state.requested_mode = policy.mode;
        Ok(())
    }

    fn attach_buffer(
        &mut self,
        payload: &[u8],
        fds: &mut Vec<OwnedFd>,
    ) -> Result<(), RuntimeError> {
        let attachment = decode_buffer_attach(payload)
            .map_err(|_| RuntimeError::Wire("invalid BufferAttach"))?;
        let snapshot_buffer = self.peer.snapshot.as_ref();
        let duplicate_buffer = self.peer.buffers.contains_key(&attachment.buffer_id)
            || snapshot_buffer
                .is_some_and(|snapshot| snapshot.buffer_ids.contains(&attachment.buffer_id));
        let buffer_limit_reached = snapshot_buffer.map_or_else(
            || self.peer.buffers.len() == MAXIMUM_BUFFERS,
            |snapshot| snapshot.buffer_ids.len() == MAXIMUM_BUFFERS,
        );
        if fds.is_empty()
            || attachment.storage_size > MAXIMUM_BUFFER_BYTES
            || attachment.stride % 4 != 0
            || duplicate_buffer
            || buffer_limit_reached
        {
            return Err(RuntimeError::Wire("buffer descriptor or size invalid"));
        }
        let accounted = snapshot_buffer.map_or_else(
            || {
                self.peer
                    .buffers
                    .values()
                    .try_fold(0_u64, |total, buffer| {
                        total.checked_add(buffer.attachment.storage_size)
                    })
                    .and_then(|total| total.checked_add(attachment.storage_size))
            },
            |snapshot| snapshot.buffer_storage.checked_add(attachment.storage_size),
        );
        if accounted.is_none_or(|total| total > MAXIMUM_TOTAL_BUFFER_BYTES) {
            return Err(RuntimeError::Wire("active buffer storage limit exceeded"));
        }
        let storage = File::from(fds.remove(0));
        let map_size = usize::try_from(attachment.storage_size)
            .map_err(|_| RuntimeError::Wire("buffer is too large"))?;
        drop(Mapping::map(
            storage.as_fd(),
            map_size,
            0,
            MapAccess::ReadOnly,
        )?);
        let synchronization = if attachment.synchronization == SynchronizationMode::EventFd {
            let descriptor = File::from(
                fds.pop()
                    .ok_or(RuntimeError::Wire("missing synchronization descriptor"))?,
            );
            let flags = descriptor_flags(descriptor.as_fd())?;
            if !flags.nonblocking || !flags.close_on_exec {
                return Err(RuntimeError::Wire(
                    "synchronization descriptor is not nonblocking and close-on-exec",
                ));
            }
            Some(descriptor)
        } else {
            None
        };
        let record = BufferRecord {
            id: attachment.buffer_id,
            surface_id: attachment.surface_id,
            attachment,
            storage,
            synchronization,
            buffer: None,
        };
        if let Some(snapshot) = self.peer.snapshot.as_mut() {
            snapshot.buffer_ids.insert(record.id);
            snapshot.buffer_storage = accounted.expect("buffer storage was checked");
            stage_snapshot_mutation(snapshot, SnapshotMutation::BufferAttach(record))
        } else {
            self.apply_buffer_record(record)
        }
    }

    fn apply_buffer_record(&mut self, record: BufferRecord) -> Result<(), RuntimeError> {
        if let Some(previous) = self.peer.surface_buffers.get(&record.surface_id).copied() {
            self.release_buffer(previous, BufferReleaseReason::Replaced)?;
        }
        self.peer
            .surface_buffers
            .insert(record.surface_id, record.id);
        self.peer.mutated_buffers.insert(record.surface_id);
        self.peer.buffers.insert(record.id, record);
        Ok(())
    }

    fn detach_buffer(&mut self, detachment: BufferDetach) {
        if self.peer.surface_buffers.get(&detachment.surface_id) == Some(&detachment.buffer_id) {
            self.peer.surface_buffers.remove(&detachment.surface_id);
        }
    }

    fn refresh_buffers(&mut self) -> Result<(), RuntimeError> {
        let refresh: Vec<_> = self
            .peer
            .surface_buffers
            .iter()
            .filter_map(|(surface_id, buffer_id)| {
                self.peer
                    .buffers
                    .get(buffer_id)
                    .is_some_and(|buffer| {
                        buffer.buffer.is_none()
                            || self.peer.damaged_surfaces.contains_key(surface_id)
                    })
                    .then_some(*buffer_id)
            })
            .collect();
        for buffer_id in refresh {
            let record = self
                .peer
                .buffers
                .get_mut(&buffer_id)
                .ok_or(RuntimeError::Wire("attached buffer is absent"))?;
            if let Some(readiness) = record.synchronization.as_mut() {
                let mut bytes = [0_u8; 8];
                match readiness.read(&mut bytes) {
                    Ok(8) if u64::from_ne_bytes(bytes) == 1 => {}
                    Ok(8) => return Err(RuntimeError::Wire("invalid readiness token")),
                    Ok(_) => return Err(RuntimeError::Wire("short readiness token")),
                    Err(error) if error.kind() == io::ErrorKind::WouldBlock => {
                        return Err(RuntimeError::Wire("buffer readiness is not signaled"));
                    }
                    Err(error) => return Err(error.into()),
                }
            }
            let attachment = &record.attachment;
            record.buffer = Some(read_surface_buffer(&record.storage, attachment)?);
        }
        Ok(())
    }

    fn apply_surface_damage(&mut self, damage: SurfaceDamage) {
        let accumulated = self
            .peer
            .damaged_surfaces
            .entry(damage.surface_id)
            .or_default();
        if damage.rectangles.len()
            > DamageRegion::MAXIMUM_RECTANGLES.saturating_sub(accumulated.len())
        {
            accumulated.clear();
            self.peer.scene_changed = true;
        } else {
            accumulated.extend(damage.rectangles);
        }
    }

    fn remove_surface(
        &mut self,
        surface_id: u64,
        reason: BufferReleaseReason,
    ) -> Result<(), RuntimeError> {
        self.peer.surfaces.remove(&surface_id);
        self.peer.policies.remove(&surface_id);
        self.peer.memberships.remove(&surface_id);
        self.peer.surface_vrr.remove(&surface_id);
        self.peer.damaged_surfaces.remove(&surface_id);
        self.peer.mutated_buffers.remove(&surface_id);
        self.remove_surface_buffers(surface_id, reason)
    }

    fn remove_surface_buffers(
        &mut self,
        surface_id: u64,
        reason: BufferReleaseReason,
    ) -> Result<(), RuntimeError> {
        self.peer.surface_buffers.remove(&surface_id);
        let buffer_ids: Vec<_> = self
            .peer
            .buffers
            .values()
            .filter(|buffer| buffer.surface_id == surface_id)
            .map(|buffer| buffer.id)
            .collect();
        for buffer_id in buffer_ids {
            self.release_buffer(buffer_id, reason)?;
        }
        Ok(())
    }
    fn release_buffer(
        &mut self,
        buffer_id: u64,
        reason: BufferReleaseReason,
    ) -> Result<(), RuntimeError> {
        if let Some(buffer) = self.peer.buffers.remove(&buffer_id) {
            if self.peer.surface_buffers.get(&buffer.surface_id) == Some(&buffer.id) {
                self.peer.surface_buffers.remove(&buffer.surface_id);
            }
            self.queue_release(buffer.id, reason)?;
        }
        Ok(())
    }

    fn queue_release(
        &mut self,
        buffer_id: u64,
        reason: BufferReleaseReason,
    ) -> Result<(), RuntimeError> {
        if self.peer.releases.len() >= MAXIMUM_PENDING_RELEASES {
            return Err(RuntimeError::Wire("pending buffer release limit exceeded"));
        }
        self.peer.releases.push(BufferRelease { buffer_id, reason });
        Ok(())
    }

    fn publish_inventory(
        &mut self,
        envelope: &Envelope,
        payload: &[u8],
        capabilities: Capabilities,
        state: &mut RuntimeState,
    ) -> Result<(), RuntimeError> {
        let query = decode_output_state_query(payload)
            .map_err(|_| RuntimeError::Wire("invalid OutputStateQuery"))?;
        let mut flags = query.flags;
        if !capabilities.contains(Capabilities::VRR_METADATA.with(Capabilities::VRR_POLICY)) {
            flags &= !16;
        }
        let records = state
            .inventory
            .records(flags)
            .map_err(|_| RuntimeError::Wire("cannot encode output inventory"))?;
        state.snapshot_id = state.snapshot_id.wrapping_add(1).max(1);
        let begin = SnapshotBegin {
            snapshot_id: SnapshotId::new(state.snapshot_id),
            domain: SnapshotDomain::Outputs,
            flags: 0,
            generation: Generation::new(state.inventory.generation),
            expected_item_count: records
                .len()
                .try_into()
                .expect("bounded output inventory count fits u32"),
        };
        self.send(
            MessageType::SNAPSHOT_BEGIN,
            MessageFlags::default(),
            Sequence::new(0),
            encode_snapshot_begin(begin),
        )?;
        for record in records {
            self.send(
                record.message_type,
                MessageFlags::SNAPSHOT_ITEM,
                Sequence::new(0),
                record.payload,
            )?;
        }
        let end = SnapshotEnd {
            snapshot_id: begin.snapshot_id,
            generation: begin.generation,
            actual_item_count: begin.expected_item_count,
        };
        self.send(
            MessageType::SNAPSHOT_END,
            MessageFlags::default(),
            Sequence::new(0),
            encode_snapshot_end(end),
        )?;
        self.send(
            MessageType::OUTPUT_CONFIGURATION_ACKNOWLEDGED,
            MessageFlags::REPLY,
            envelope.sequence,
            state
                .inventory
                .encoded_acknowledgement(query.query_id, OutputConfigurationResult::Accepted),
        )
    }

    fn commit_output_configuration(
        &mut self,
        envelope: &Envelope,
        payload: &[u8],
        state: &mut RuntimeState,
    ) -> Result<(), RuntimeError> {
        let commit = decode_output_configuration_commit(payload)
            .map_err(|_| RuntimeError::Wire("invalid OutputConfigurationCommit"))?;
        let snapshot = self
            .peer
            .snapshot
            .take()
            .ok_or(RuntimeError::Wire("output commit lacks completed snapshot"))?;
        let outputs: Vec<_> = snapshot.outputs.into_values().collect();
        let accepted = commit.base_generation == state.inventory.generation
            && state
                .inventory
                .apply_configuration(&outputs, commit.primary_output_id);
        let result = if accepted {
            for policy in snapshot.output_vrr_policies.into_values() {
                Self::apply_output_vrr_policy(policy, state)?;
            }
            self.peer.configuration_generation = state.inventory.generation;
            self.peer.scene_changed = true;
            OutputConfigurationResult::Accepted
        } else if commit.base_generation != state.inventory.generation {
            OutputConfigurationResult::StaleGeneration
        } else {
            OutputConfigurationResult::InvalidLayout
        };
        self.send(
            MessageType::OUTPUT_CONFIGURATION_ACKNOWLEDGED,
            MessageFlags::REPLY,
            envelope.sequence,
            state
                .inventory
                .encoded_acknowledgement(commit.configuration_id, result),
        )
    }

    #[allow(clippy::too_many_lines)]
    fn commit_frame(
        &mut self,
        envelope: &Envelope,
        payload: &[u8],
        state: &mut RuntimeState,
    ) -> Result<(), RuntimeError> {
        let commit =
            decode_frame_commit(payload).map_err(|_| RuntimeError::Wire("invalid FrameCommit"))?;
        let protocol_server = self
            .negotiated
            .as_ref()
            .is_some_and(|peer| peer.role == Role::ProtocolServer);
        let buffered_protocol_server = protocol_server
            && self.negotiated.as_ref().is_some_and(|peer| {
                peer.capabilities.contains(
                    Capabilities::FD_PASSING
                        .with(Capabilities::MEMFD_BUFFERS)
                        .with(Capabilities::DAMAGE_REGIONS),
                )
            });
        if protocol_server
            && self.peer.surfaces.values().any(|surface| {
                surface.presentation_flags & 2 == 0
                    && self
                        .peer
                        .policies
                        .get(&surface.surface_id)
                        .is_none_or(|policy| policy.x11_window_id != surface.x11_window_id)
            })
        {
            return self.reject_frame(envelope, &commit, FrameResult::RejectedIncompleteMetadata);
        }
        if self
            .peer
            .policies
            .keys()
            .any(|surface_id| !self.peer.surfaces.contains_key(surface_id))
        {
            return self.reject_frame(envelope, &commit, FrameResult::RejectedUnknownSurface);
        }
        let metadata_only = protocol_server && !buffered_protocol_server;
        if metadata_only
            && (self
                .peer
                .surfaces
                .values()
                .any(|surface| surface.presentation_flags != 1)
                || !self.peer.buffers.is_empty())
        {
            return self.reject_frame(envelope, &commit, FrameResult::RejectedIncompleteMetadata);
        }
        if self
            .peer
            .buffers
            .values()
            .any(|buffer| !self.peer.surfaces.contains_key(&buffer.surface_id))
        {
            return self.reject_frame(envelope, &commit, FrameResult::RejectedUnknownSurface);
        }
        if self.peer.surfaces.values().any(|surface| {
            surface.presentation_flags & 1 == 0
                && !self.peer.surface_buffers.contains_key(&surface.surface_id)
        }) {
            return self.reject_frame(envelope, &commit, FrameResult::RejectedIncompleteMetadata);
        }
        if self.peer.surfaces.values().any(|surface| {
            if surface.presentation_flags & 1 != 0 {
                return false;
            }
            let Some(buffer) = self
                .peer
                .surface_buffers
                .get(&surface.surface_id)
                .and_then(|buffer_id| self.peer.buffers.get(buffer_id))
            else {
                return true;
            };
            let client_scale = self
                .peer
                .memberships
                .get(&surface.surface_id)
                .map_or(1, |membership| membership.client_buffer_scale);
            u64::from(surface.logical_width) * u64::from(client_scale)
                != u64::from(buffer.attachment.width)
                || u64::from(surface.logical_height) * u64::from(client_scale)
                    != u64::from(buffer.attachment.height)
        }) {
            return self.reject_frame(envelope, &commit, FrameResult::RejectedInvalidBuffer);
        }
        if !metadata_only {
            self.refresh_buffers()?;
        }
        if metadata_only {
            if let Some(manifest) = &state.manifest {
                let output =
                    manifest_output(commit.output_id, &self.peer.outputs, &state.inventory)
                        .ok_or(RuntimeError::Wire("scene manifest output is absent"))?;
                manifest.append(
                    commit.commit_id,
                    commit.producer_generation,
                    output,
                    &self.peer.surfaces,
                    &self.peer.policies,
                )?;
            }
            state.frame_ordinal += 1;
            state.accepted_frames += 1;
            let acknowledged = FrameAcknowledged {
                commit_id: commit.commit_id,
                output_id: commit.output_id,
                presented_generation: commit.producer_generation,
                result: FrameResult::Accepted,
            };
            return self.send(
                MessageType::FRAME_ACKNOWLEDGED,
                MessageFlags::REPLY,
                envelope.sequence,
                encode_frame_acknowledged(&acknowledged),
            );
        }

        let scene = self.build_scene(&state.inventory)?;
        let force_full_damage = self.peer.previous.is_none()
            || self.peer.scene_changed
            || !self
                .peer
                .mutated_buffers
                .iter()
                .all(|surface_id| self.peer.damaged_surfaces.contains_key(surface_id));
        let damage =
            calculate_output_damage(&scene, &self.peer.damaged_surfaces, force_full_damage);
        let ordinal = state.frame_ordinal + 1;
        let rendered = render_software_scene(SoftwareRenderRequest {
            scene: &scene,
            damage: &damage,
            previous: self.peer.previous.as_ref(),
            commit_id: commit.commit_id,
            generation: commit.producer_generation,
            ordinal,
        })
        .map_err(|error| {
            eprintln!("gwcomp: software renderer rejected scene: {error:?}");
            RuntimeError::Wire("software scene rendering rejected frame")
        })?;
        if let Some(dumper) = state.dumper.as_mut() {
            dumper.dump(
                &rendered.frames,
                ordinal,
                commit.commit_id,
                commit.producer_generation,
            )?;
        }
        if let Some(manifest) = &state.manifest
            && protocol_server
        {
            let output = manifest_output(commit.output_id, &self.peer.outputs, &state.inventory)
                .ok_or(RuntimeError::Wire("scene manifest output is absent"))?;
            manifest.append(
                commit.commit_id,
                commit.producer_generation,
                output,
                &self.peer.surfaces,
                &self.peer.policies,
            )?;
        }
        state.frame_ordinal = ordinal;
        state.accepted_frames += 1;
        self.peer.damaged_surfaces.clear();
        self.peer.mutated_buffers.clear();
        self.peer.scene_changed = false;
        self.peer.previous = Some(rendered.frames);
        if self
            .negotiated
            .as_ref()
            .is_some_and(|peer| peer.capabilities.contains(vrr_capabilities()))
        {
            let policy_generation = presentation_state_generation(&scene, &self.peer.surface_vrr);
            if self
                .peer
                .surface_vrr
                .values()
                .any(|state| state.policy_generation != policy_generation)
            {
                return Err(RuntimeError::Wire(
                    "surface VRR records have inconsistent policy generations",
                ));
            }
            let enabled_outputs: Vec<_> = scene
                .outputs
                .values()
                .filter(|output| output.enabled)
                .map(|output| output.output_id)
                .collect();
            for output_id in &enabled_outputs {
                let candidate = self
                    .peer
                    .surface_vrr
                    .values()
                    .find(|surface| surface.output_id == *output_id && surface.policy_selected);
                let record = state
                    .inventory
                    .outputs
                    .get_mut(output_id)
                    .ok_or(RuntimeError::Wire("VRR output is absent from inventory"))?;
                let candidate_required =
                    record.vrr_policy.mode != gw_wire::vrr::VrrPolicyMode::AlwaysEligible;
                let desired = record.vrr_capability.simulated
                    && record.vrr_policy.mode != gw_wire::vrr::VrrPolicyMode::Off
                    && (!candidate_required || candidate.is_some());
                let interval = refresh_interval_nanoseconds(record.state.refresh_millihertz);
                let decision = if record.vrr_capability.simulated {
                    if desired {
                        VrrDecision::Enabled
                    } else {
                        VrrDecision::Disabled
                    }
                } else {
                    VrrDecision::Unsupported
                };
                let candidate_window_id = candidate.map_or(0, |value| value.window_id);
                let candidate_surface_id = candidate.map_or(0, |value| value.surface_id);
                let reason_flags = if record.vrr_capability.simulated {
                    VRR_REASON_SIMULATED_HEADLESS
                        | if record.vrr_policy.mode == gw_wire::vrr::VrrPolicyMode::Off {
                            VRR_REASON_POLICY_OFF
                        } else if record.vrr_policy.mode
                            == gw_wire::vrr::VrrPolicyMode::AlwaysEligible
                        {
                            VRR_REASON_MANUAL_ALWAYS_ELIGIBLE
                        } else if let Some(candidate) = candidate {
                            candidate.reason_flags
                        } else {
                            VRR_REASON_NO_CANDIDATE
                        }
                } else {
                    record.vrr_capability.reason_flags
                };
                let changed = record.vrr_state.requested_mode != record.vrr_policy.mode
                    || record.vrr_state.decision != decision
                    || record.vrr_state.desired_enabled != desired
                    || record.vrr_state.candidate_window_id != candidate_window_id
                    || record.vrr_state.candidate_surface_id != candidate_surface_id
                    || record.vrr_state.reason_flags != reason_flags;
                record.vrr_state.requested_mode = record.vrr_policy.mode;
                record.vrr_state.decision = decision;
                record.vrr_state.desired_enabled = desired;
                record.vrr_state.effective_enabled = desired;
                record.vrr_state.property_readback_valid = record.vrr_capability.simulated;
                record.vrr_state.candidate_window_id = candidate_window_id;
                record.vrr_state.candidate_surface_id = candidate_surface_id;
                record.vrr_state.reason_flags = reason_flags;
                record.vrr_state.state_generation = policy_generation;
                if changed {
                    record.vrr_state.transition_serial =
                        record.vrr_state.transition_serial.saturating_add(1);
                }
                record.vrr_state.last_commit_id = commit.commit_id;
                record.vrr_state.last_presented_generation = commit.producer_generation;
                record.vrr_state.last_flip_sequence =
                    record.vrr_state.last_flip_sequence.saturating_add(1);
                record.vrr_state.last_interval_nanoseconds = interval;
                record.vrr_state.last_flip_timestamp_nanoseconds = record
                    .vrr_state
                    .last_flip_timestamp_nanoseconds
                    .saturating_add(interval);
                let published_state = record.vrr_state;
                self.send(
                    MessageType::OUTPUT_VRR_STATE_UPSERT,
                    MessageFlags::REPLY,
                    envelope.sequence,
                    encode_output_vrr_state_upsert(&published_state),
                )?;
                let timing = PresentationTiming {
                    output_id: *output_id,
                    commit_id: commit.commit_id,
                    presented_generation: commit.producer_generation,
                    flip_sequence: published_state.last_flip_sequence,
                    flags: gw_wire::vrr::PRESENTATION_TIMING_SIMULATED,
                    kernel_timestamp_nanoseconds: published_state.last_flip_timestamp_nanoseconds,
                    interval_nanoseconds: interval,
                    effective_vrr_enabled: desired,
                    timestamp_available: true,
                };
                self.send(
                    MessageType::PRESENTATION_TIMING,
                    MessageFlags::default(),
                    Sequence::new(0),
                    encode_presentation_timing(&timing),
                )?;
                if let Some(report) = state.vrr_report.as_mut()
                    && record.vrr_capability.simulated
                {
                    report.presentation(&published_state, &timing, interval)?;
                }
            }
        }
        let acknowledged = FrameAcknowledged {
            commit_id: commit.commit_id,
            output_id: commit.output_id,
            presented_generation: commit.producer_generation,
            result: FrameResult::Accepted,
        };
        self.send(
            MessageType::FRAME_ACKNOWLEDGED,
            MessageFlags::REPLY,
            envelope.sequence,
            encode_frame_acknowledged(&acknowledged),
        )?;
        eprintln!(
            "gwcomp: frame accepted commit={} frame={} hash={:016x}",
            commit.commit_id,
            ordinal,
            self.peer
                .previous
                .as_ref()
                .map_or(0, SoftwareFrameSet::aggregate_hash)
        );
        Ok(())
    }

    fn reject_frame(
        &mut self,
        envelope: &Envelope,
        commit: &gw_wire::compositor::FrameCommit,
        result: FrameResult,
    ) -> Result<(), RuntimeError> {
        eprintln!(
            "gwcomp: frame rejected commit={} generation={} result={result:?}",
            commit.commit_id, commit.producer_generation
        );
        let acknowledged = FrameAcknowledged {
            commit_id: commit.commit_id,
            output_id: commit.output_id,
            presented_generation: commit.producer_generation,
            result,
        };
        self.send(
            MessageType::FRAME_ACKNOWLEDGED,
            MessageFlags::REPLY,
            envelope.sequence,
            encode_frame_acknowledged(&acknowledged),
        )
    }

    fn flush_releases(&mut self) -> Result<(), RuntimeError> {
        let releases = std::mem::take(&mut self.peer.releases);
        for release in releases {
            self.send(
                MessageType::BUFFER_RELEASE,
                MessageFlags::default(),
                Sequence::new(0),
                encode_buffer_release(&release),
            )?;
        }
        Ok(())
    }

    #[allow(clippy::too_many_lines)]
    fn build_scene(&self, inventory: &Inventory) -> Result<Scene, RuntimeError> {
        let configuration_generation = if self.peer.configuration_generation == 0 {
            inventory.generation
        } else {
            self.peer.configuration_generation
        };
        let output_source: Vec<_> = if self.peer.outputs.is_empty() {
            inventory
                .outputs
                .values()
                .map(|output| output.state.clone())
                .collect()
        } else {
            self.peer.outputs.values().cloned().collect()
        };
        let primary = if output_source
            .iter()
            .any(|output| output.output_id == inventory.primary_output_id)
        {
            inventory.primary_output_id
        } else {
            output_source.first().map_or(0, |output| output.output_id)
        };
        let mut scene = Scene {
            primary_output_id: primary,
            configuration_generation,
            ..Scene::default()
        };
        for output in output_source {
            scene.outputs.insert(
                output.output_id,
                SceneOutput {
                    output_id: output.output_id,
                    enabled: output.enabled,
                    logical: Rectangle::new(
                        output.logical_x,
                        output.logical_y,
                        output.logical_width,
                        output.logical_height,
                    ),
                    physical_width: output.physical_pixel_width,
                    physical_height: output.physical_pixel_height,
                    refresh_millihertz: output.refresh_millihertz,
                    scale: RationalScale {
                        numerator: output.scale_numerator,
                        denominator: output.scale_denominator,
                    },
                    transform: convert_transform(output.transform),
                },
            );
        }
        for (&surface_id, metadata) in &self.peer.surfaces {
            if metadata.presentation_flags & 1 != 0 {
                continue;
            }
            let Some(buffer_id) = self.peer.surface_buffers.get(&surface_id) else {
                continue;
            };
            let Some(buffer) = self.peer.buffers.get(buffer_id) else {
                continue;
            };
            let assigned_output = if metadata.output_id == 0 {
                primary
            } else {
                metadata.output_id
            };
            let membership = self
                .peer
                .memberships
                .get(&surface_id)
                .cloned()
                .unwrap_or_else(|| {
                    let surface_bounds = Rectangle::new(
                        metadata.logical_x,
                        metadata.logical_y,
                        metadata.logical_width,
                        metadata.logical_height,
                    );
                    let mut output_ids: Vec<_> = scene
                        .outputs
                        .values()
                        .filter(|output| {
                            metadata.visible
                                && output.enabled
                                && surface_bounds.intersection(output.logical).is_some()
                        })
                        .map(|output| output.output_id)
                        .collect();
                    output_ids.sort_by_key(|output_id| {
                        let output = &scene.outputs[output_id];
                        (output.logical.y, output.logical.x, *output_id)
                    });
                    let scale = scene
                        .outputs
                        .get(&assigned_output)
                        .map_or_else(RationalScale::default, |output| output.scale);
                    SurfaceOutputState {
                        surface_id,
                        primary_output_id: assigned_output,
                        output_ids,
                        preferred_scale_numerator: scale.numerator,
                        preferred_scale_denominator: scale.denominator,
                        client_buffer_scale: 1,
                        scale_mode: gw_wire::output::SurfaceScaleMode::Legacy,
                        layout_generation: configuration_generation,
                        flags: 0,
                    }
                });
            let client_scale = membership.client_buffer_scale;
            scene.surfaces.insert(
                surface_id,
                SceneSurface {
                    surface_id,
                    output_id: membership.primary_output_id,
                    logical: Rectangle::new(
                        metadata.logical_x,
                        metadata.logical_y,
                        metadata.logical_width,
                        metadata.logical_height,
                    ),
                    stacking: metadata.stacking,
                    visible: metadata.visible,
                    clip: metadata.clipping.then(|| {
                        Rectangle::new(
                            metadata.clip_x,
                            metadata.clip_y,
                            metadata.clip_width,
                            metadata.clip_height,
                        )
                    }),
                    opacity: metadata.opacity,
                    client_buffer_scale: client_scale,
                    presentation: if metadata.presentation_flags & 2 != 0 {
                        SurfacePresentation::Cursor
                    } else {
                        SurfacePresentation::Ordinary
                    },
                    buffer: buffer
                        .buffer
                        .clone()
                        .ok_or(RuntimeError::Wire("buffer was not refreshed"))?,
                },
            );
            scene.surface_outputs.insert(
                surface_id,
                SurfaceOutputMembership {
                    primary_output_id: membership.primary_output_id,
                    output_ids: membership.output_ids,
                    preferred_scale: RationalScale {
                        numerator: membership.preferred_scale_numerator,
                        denominator: membership.preferred_scale_denominator,
                    },
                    client_buffer_scale: client_scale,
                    layout_generation: configuration_generation,
                },
            );
        }
        Ok(scene)
    }

    #[allow(clippy::needless_pass_by_value)]
    fn send(
        &mut self,
        message_type: MessageType,
        flags: MessageFlags,
        reply_to: Sequence,
        payload: Vec<u8>,
    ) -> Result<(), RuntimeError> {
        let mut envelope = Envelope::request(
            message_type,
            Sequence::new(self.next_sequence),
            payload
                .len()
                .try_into()
                .expect("GWIPC payload length fits u32"),
        );
        envelope.flags = flags;
        envelope.reply_to = reply_to;
        if let Some(validator) = self.validator.as_mut() {
            validator
                .validate_outgoing(&envelope, &payload, 0)
                .map_err(|_| RuntimeError::Wire("outgoing application validation failed"))?;
        }
        send_with_retry(&self.transport, &envelope, &payload, &[])?;
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
    Live { accepted: u64 },
    Disconnected,
    Expired(ExpiredDeadline),
}

fn stage_snapshot_mutation(
    snapshot: &mut SnapshotTransaction,
    mutation: SnapshotMutation,
) -> Result<(), RuntimeError> {
    if snapshot.mutations.len() >= MAXIMUM_SNAPSHOT_ITEMS as usize {
        return Err(RuntimeError::Wire("snapshot mutation limit exceeded"));
    }
    snapshot.mutations.push(mutation);
    Ok(())
}

#[cfg(test)]
fn required_buffer_bytes(attachment: &BufferAttach) -> Result<usize, RuntimeError> {
    let required = u64::from(attachment.height.saturating_sub(1))
        .checked_mul(u64::from(attachment.stride))
        .and_then(|padding| padding.checked_add(u64::from(attachment.width) * 4))
        .ok_or(RuntimeError::Wire("buffer byte count overflow"))?;
    usize::try_from(required).map_err(|_| RuntimeError::Wire("buffer is too large"))
}

fn try_zeroed_bytes(size: usize) -> Result<Vec<u8>, RuntimeError> {
    let mut bytes = Vec::new();
    bytes
        .try_reserve_exact(size)
        .map_err(|_| RuntimeError::Wire("buffer byte allocation failed"))?;
    bytes.resize(size, 0);
    Ok(bytes)
}

#[cfg(test)]
fn decode_surface_buffer(
    attachment: &BufferAttach,
    bytes: &[u8],
) -> Result<SurfaceBuffer, RuntimeError> {
    let required = required_buffer_bytes(attachment)?;
    if bytes.len() < required {
        return Err(RuntimeError::Wire("buffer contents are truncated"));
    }
    let mut buffer = allocate_surface_buffer(attachment)?;
    let width_bytes = usize::try_from(u64::from(attachment.width) * 4)
        .map_err(|_| RuntimeError::Wire("buffer row byte count overflow"))?;
    let stride = attachment.stride as usize;
    for y in 0..attachment.height as usize {
        let row_start = y
            .checked_mul(stride)
            .ok_or(RuntimeError::Wire("buffer row offset overflow"))?;
        decode_pixel_row(&mut buffer, y, &bytes[row_start..row_start + width_bytes]);
    }
    Ok(buffer)
}

fn read_surface_buffer(
    storage: &File,
    attachment: &BufferAttach,
) -> Result<SurfaceBuffer, RuntimeError> {
    let mut buffer = allocate_surface_buffer(attachment)?;
    let width_bytes = usize::try_from(u64::from(attachment.width) * 4)
        .map_err(|_| RuntimeError::Wire("buffer row byte count overflow"))?;
    let mut row = try_zeroed_bytes(width_bytes)?;
    for y in 0..attachment.height {
        let row_offset = u64::from(y)
            .checked_mul(u64::from(attachment.stride))
            .and_then(|offset| attachment.byte_offset.checked_add(offset))
            .ok_or(RuntimeError::Wire("buffer row offset overflow"))?;
        storage.read_exact_at(&mut row, row_offset)?;
        decode_pixel_row(&mut buffer, y as usize, &row);
    }
    Ok(buffer)
}

fn allocate_surface_buffer(attachment: &BufferAttach) -> Result<SurfaceBuffer, RuntimeError> {
    let word_count = usize::try_from(
        u64::from(attachment.width)
            .checked_mul(u64::from(attachment.height))
            .ok_or(RuntimeError::Wire("buffer word count overflow"))?,
    )
    .map_err(|_| RuntimeError::Wire("buffer word count overflow"))?;
    let mut pixels = Vec::new();
    pixels
        .try_reserve_exact(word_count)
        .map_err(|_| RuntimeError::Wire("buffer pixel allocation failed"))?;
    pixels.resize(word_count, 0_u32);
    let format = match (attachment.pixel_format, attachment.alpha_semantics) {
        (WirePixelFormat::Xrgb8888, AlphaSemantics::Opaque) => PixelFormat::Xrgb8888,
        (WirePixelFormat::Argb8888, AlphaSemantics::Premultiplied) => {
            PixelFormat::Argb8888Premultiplied
        }
        _ => return Err(RuntimeError::Wire("unsupported buffer format")),
    };
    Ok(SurfaceBuffer {
        width: attachment.width,
        height: attachment.height,
        stride_pixels: attachment.width,
        format,
        pixels,
    })
}

fn decode_pixel_row(buffer: &mut SurfaceBuffer, y: usize, source_row: &[u8]) {
    let width = buffer.width as usize;
    for x in 0..width {
        let offset = x * 4;
        buffer.pixels[y * width + x] = u32::from_ne_bytes(
            source_row[offset..offset + 4]
                .try_into()
                .expect("four bytes"),
        );
    }
}

pub fn run(options: &Options) -> Result<(), RuntimeError> {
    install_signal_handlers()?;
    run_with_timeouts(options, PeerTimeouts::default())
}

fn run_with_timeouts(options: &Options, timeouts: PeerTimeouts) -> Result<(), RuntimeError> {
    let dumper = options
        .dump_dir
        .as_deref()
        .map(FrameDumper::prepare)
        .transpose()?;
    let manifest = options
        .scene_manifest
        .as_deref()
        .map(SceneManifest::prepare)
        .transpose()?;
    let listener = Listener::bind(&options.ipc_socket)?;
    eprintln!("gwcomp: listening socket={}", options.ipc_socket.display());
    let config = handshake_config();
    let inventory = Inventory::build(&options.outputs, &options.vrr);
    let mut vrr_report = options
        .vrr_report
        .as_deref()
        .map(VrrReport::create)
        .transpose()?;
    if let Some(report) = vrr_report.as_mut() {
        for output in inventory
            .outputs
            .values()
            .filter(|output| output.vrr_capability.simulated)
        {
            report.capability(&output.vrr_capability)?;
        }
    }
    let mut state = RuntimeState {
        inventory,
        snapshot_id: 0,
        frame_ordinal: 0,
        accepted_frames: 0,
        dumper,
        manifest,
        vrr_report,
    };
    let mut connection = None;
    let mut connection_id = 1_u64;
    let mut accepted_any = false;
    let explicit_outputs = !(options.outputs.len() == 1
        && options.outputs[0].name == "HEADLESS-1"
        && options.outputs[0].width == 1024
        && options.outputs[0].height == 768);
    let mut stop_after_flush = false;
    while !stop_requested() && !stop_after_flush {
        if connection.is_none()
            && let Some(fd) = listener.accept()?
        {
            connection = Some(Connection::new(fd, Instant::now(), timeouts)?);
        }
        let mut disconnected = false;
        if let Some(active) = connection.as_mut() {
            if let Some(expired) = active.liveness.expired(Instant::now()) {
                eprintln!("gwcomp: closing peer after {expired} deadline expired");
                disconnected = true;
            } else if active.negotiated.is_none() {
                match active.handshake(&config, ConnectionId::new(connection_id), explicit_outputs)
                {
                    Ok(HandshakeProgress::Waiting) => {}
                    Ok(HandshakeProgress::Accepted) => {
                        connection_id = connection_id.wrapping_add(1).max(1);
                        eprintln!("gwcomp: producer connected");
                    }
                    Ok(HandshakeProgress::Rejected) | Err(_) => disconnected = true,
                }
            } else {
                match active.process(&mut state) {
                    ProcessProgress::Live { accepted } => {
                        accepted_any |= accepted != 0;
                        if options
                            .max_frames
                            .is_some_and(|maximum| state.accepted_frames >= maximum)
                        {
                            stop_after_flush = true;
                        }
                    }
                    ProcessProgress::Disconnected => disconnected = true,
                    ProcessProgress::Expired(expired) => {
                        eprintln!("gwcomp: closing peer after {expired} deadline expired");
                        disconnected = true;
                    }
                }
                if options
                    .max_frames
                    .is_some_and(|maximum| state.accepted_frames >= maximum)
                {
                    accepted_any = true;
                    stop_after_flush = true;
                }
            }
        }
        if disconnected {
            connection = None;
            eprintln!("gwcomp: producer disconnected, cleared scene state");
            if options.once && accepted_any {
                break;
            }
        }
        if !stop_after_flush {
            thread::sleep(Duration::from_millis(2));
        }
    }
    if stop_after_flush {
        thread::sleep(Duration::from_millis(100));
    }
    drop(connection);
    if let Some(report) = state.vrr_report.as_mut() {
        report.finish()?;
    }
    drop(listener);
    eprintln!("gwcomp: stopped");
    Ok(())
}

fn handshake_config() -> HandshakeConfig {
    let required = Capabilities::default()
        .with(Capabilities::SNAPSHOTS)
        .with(Capabilities::OUTPUT_STATE)
        .with(Capabilities::SURFACE_STATE)
        .with(Capabilities::SDR_COLOR_METADATA)
        .with(Capabilities::FRAME_ACKNOWLEDGEMENT);
    let offered = required
        .with(Capabilities::FD_PASSING)
        .with(Capabilities::MEMFD_BUFFERS)
        .with(Capabilities::DAMAGE_REGIONS)
        .with(Capabilities::WINDOW_LIFECYCLE)
        .with(Capabilities::SESSION_STATE)
        .with(Capabilities::CURSOR_SURFACE)
        .with(Capabilities::CPU_BUFFER_SYNCHRONIZATION)
        .with(Capabilities::OUTPUT_MANAGEMENT)
        .with(Capabilities::SURFACE_OUTPUT_MEMBERSHIP)
        .with(Capabilities::SCALE_METADATA)
        .with(Capabilities::VRR_METADATA)
        .with(Capabilities::VRR_POLICY)
        .with(Capabilities::PRESENTATION_TIMING);
    let mut instance = [0_u8; 16];
    instance[..11].copy_from_slice(b"gwcomp-rust");
    instance[12..].copy_from_slice(&std::process::id().to_le_bytes());
    HandshakeConfig::new(Role::Compositor, instance, "gwcomp-rust")
        .allow_peer_role(Role::ProtocolServer)
        .allow_peer_role(Role::TestProducer)
        .offer(offered)
        .require_peer(required)
}
fn output_model_capabilities() -> Capabilities {
    Capabilities::default()
        .with(Capabilities::OUTPUT_MANAGEMENT)
        .with(Capabilities::SURFACE_OUTPUT_MEMBERSHIP)
        .with(Capabilities::SCALE_METADATA)
}
fn vrr_capabilities() -> Capabilities {
    Capabilities::default()
        .with(Capabilities::VRR_METADATA)
        .with(Capabilities::VRR_POLICY)
        .with(Capabilities::PRESENTATION_TIMING)
}
fn valid_peer_profile(peer: &NegotiatedPeer) -> bool {
    let common = Capabilities::default()
        .with(Capabilities::SNAPSHOTS)
        .with(Capabilities::OUTPUT_STATE)
        .with(Capabilities::SURFACE_STATE)
        .with(Capabilities::SDR_COLOR_METADATA)
        .with(Capabilities::FRAME_ACKNOWLEDGEMENT);
    let buffered = Capabilities::default()
        .with(Capabilities::FD_PASSING)
        .with(Capabilities::MEMFD_BUFFERS)
        .with(Capabilities::DAMAGE_REGIONS);
    let output_model = output_model_capabilities();
    let vrr = vrr_capabilities();
    let bits = peer.capabilities.bits();
    let exact_bundle = |bundle: Capabilities| {
        let selected = bits & bundle.bits();
        selected == 0 || selected == bundle.bits()
    };
    match peer.role {
        Role::TestProducer => {
            peer.capabilities.contains(common.with(buffered))
                && bits & (Capabilities::CURSOR_SURFACE.bits() | output_model.bits() | vrr.bits())
                    == 0
        }
        Role::ProtocolServer => {
            peer.capabilities
                .contains(common.with(Capabilities::WINDOW_LIFECYCLE))
                && exact_bundle(buffered)
                && exact_bundle(output_model)
                && exact_bundle(vrr)
                && (bits & vrr.bits() == 0 || bits & output_model.bits() == output_model.bits())
                && (!peer.capabilities.contains(Capabilities::CURSOR_SURFACE)
                    || peer.capabilities.contains(buffered))
        }
        _ => false,
    }
}

fn calculate_output_damage(
    scene: &Scene,
    surface_damage: &BTreeMap<u64, Vec<DamageRectangle>>,
    force_full: bool,
) -> BTreeMap<u64, Vec<Rectangle>> {
    let mut regions: BTreeMap<u64, DamageRegion> = scene
        .outputs
        .values()
        .filter(|output| output.enabled)
        .map(|output| {
            (
                output.output_id,
                DamageRegion::new(Rectangle::new(
                    0,
                    0,
                    output.physical_width,
                    output.physical_height,
                )),
            )
        })
        .collect();
    if force_full {
        for region in regions.values_mut() {
            region.add_full_output();
        }
    } else {
        for (&surface_id, rectangles) in surface_damage {
            let Some(surface) = scene.surfaces.get(&surface_id) else {
                continue;
            };
            if !surface.visible || surface.opacity == 0 {
                continue;
            }
            let Some(membership) = scene.surface_outputs.get(&surface_id) else {
                continue;
            };
            let local_bounds = Rectangle::new(0, 0, surface.logical.width, surface.logical.height);
            for rectangle in rectangles {
                let local =
                    Rectangle::new(rectangle.x, rectangle.y, rectangle.width, rectangle.height);
                let Some(mut clipped) = local.intersection(local_bounds) else {
                    continue;
                };
                if let Some(clip) = surface.clip {
                    let Some(intersection) = clipped.intersection(clip) else {
                        continue;
                    };
                    clipped = intersection;
                }
                let Some(logical) = clipped.translate(surface.logical.x, surface.logical.y) else {
                    continue;
                };
                for output_id in &membership.output_ids {
                    let Some(output) = scene.outputs.get(output_id) else {
                        continue;
                    };
                    let Some(mapping) = output.mapping() else {
                        continue;
                    };
                    let footprint =
                        match select_sampling_filter(output.scale, surface.client_buffer_scale) {
                            gwcomp_core::SamplingFilter::Bilinear => {
                                DamageFilterFootprint::Bilinear
                            }
                            gwcomp_core::SamplingFilter::Direct
                            | gwcomp_core::SamplingFilter::Nearest => DamageFilterFootprint::Point,
                        };
                    let Some(native) = map_logical_damage_to_native(mapping, logical, footprint)
                    else {
                        continue;
                    };
                    if let Some(region) = regions.get_mut(output_id) {
                        region.add(Rectangle::new(
                            i32::try_from(native.x).unwrap_or(i32::MAX),
                            i32::try_from(native.y).unwrap_or(i32::MAX),
                            native.width,
                            native.height,
                        ));
                    }
                }
            }
        }
    }
    regions
        .into_iter()
        .filter_map(|(output_id, region)| {
            (!region.is_empty()).then(|| (output_id, region.rectangles().to_vec()))
        })
        .collect()
}

fn presentation_state_generation(
    scene: &Scene,
    surface_vrr: &BTreeMap<u64, SurfaceVrrState>,
) -> u64 {
    surface_vrr
        .values()
        .next()
        .map_or(scene.configuration_generation, |state| {
            state.policy_generation
        })
}

fn convert_transform(value: gw_wire::compositor::Transform) -> gwcomp_core::OutputTransform {
    match value {
        gw_wire::compositor::Transform::Normal => gwcomp_core::OutputTransform::Normal,
        gw_wire::compositor::Transform::Rotate90 => gwcomp_core::OutputTransform::Rotate90,
        gw_wire::compositor::Transform::Rotate180 => gwcomp_core::OutputTransform::Rotate180,
        gw_wire::compositor::Transform::Rotate270 => gwcomp_core::OutputTransform::Rotate270,
        gw_wire::compositor::Transform::Flipped => gwcomp_core::OutputTransform::Flipped,
        gw_wire::compositor::Transform::Flipped90 => gwcomp_core::OutputTransform::Flipped90,
        gw_wire::compositor::Transform::Flipped180 => gwcomp_core::OutputTransform::Flipped180,
        gw_wire::compositor::Transform::Flipped270 => gwcomp_core::OutputTransform::Flipped270,
    }
}

fn manifest_output<'a>(
    requested_output_id: u64,
    peer_outputs: &'a BTreeMap<u64, OutputUpsert>,
    inventory: &'a Inventory,
) -> Option<&'a OutputUpsert> {
    peer_outputs
        .get(&requested_output_id)
        .or_else(|| peer_outputs.values().next())
        .or_else(|| {
            inventory
                .outputs
                .get(&requested_output_id)
                .map(|output| &output.state)
        })
        .or_else(|| {
            inventory
                .outputs
                .get(&inventory.primary_output_id)
                .map(|output| &output.state)
        })
}

fn refresh_interval_nanoseconds(refresh_millihertz: u32) -> u64 {
    if refresh_millihertz == 0 {
        0
    } else {
        1_000_000_000_000_u64 / u64::from(refresh_millihertz)
    }
}
fn send_with_retry(
    transport: &Transport,
    envelope: &Envelope,
    payload: &[u8],
    fds: &[std::os::fd::BorrowedFd<'_>],
) -> Result<(), TransportError> {
    for _ in 0..5_000 {
        match transport.send(envelope, payload, fds) {
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

#[cfg(test)]
mod tests {
    use super::*;

    fn buffer_attachment(width: u32, height: u32, stride: u32) -> BufferAttach {
        BufferAttach {
            buffer_id: 1,
            surface_id: 2,
            width,
            height,
            stride,
            byte_offset: 0,
            storage_size: u64::from(height.saturating_sub(1)) * u64::from(stride)
                + u64::from(width) * 4,
            pixel_format: WirePixelFormat::Xrgb8888,
            modifier: 0,
            alpha_semantics: AlphaSemantics::Opaque,
            color: gw_wire::compositor::SdrColorMetadata::default(),
            synchronization: SynchronizationMode::None,
            flags: 0,
        }
    }

    fn snapshot_begin(expected_item_count: u32) -> SnapshotBegin {
        SnapshotBegin {
            snapshot_id: SnapshotId::new(1),
            domain: SnapshotDomain::CompleteSession,
            flags: 0,
            generation: Generation::new(1),
            expected_item_count,
        }
    }

    fn damage_scene(client_buffer_scale: u32) -> Scene {
        let output_id = 11;
        let surface_id = 41;
        let mut scene = Scene {
            primary_output_id: output_id,
            configuration_generation: 1,
            ..Scene::default()
        };
        scene.outputs.insert(
            output_id,
            SceneOutput {
                output_id,
                enabled: true,
                logical: Rectangle::new(0, 0, 800, 600),
                physical_width: 800,
                physical_height: 600,
                refresh_millihertz: 120_000,
                scale: RationalScale::default(),
                transform: gwcomp_core::OutputTransform::Normal,
            },
        );
        scene.surfaces.insert(
            surface_id,
            SceneSurface {
                surface_id,
                output_id,
                logical: Rectangle::new(20, 30, 200, 100),
                stacking: 0,
                visible: true,
                clip: None,
                opacity: u32::MAX,
                client_buffer_scale,
                presentation: SurfacePresentation::Ordinary,
                buffer: SurfaceBuffer {
                    width: 200 * client_buffer_scale,
                    height: 100 * client_buffer_scale,
                    stride_pixels: 200 * client_buffer_scale,
                    format: PixelFormat::Xrgb8888,
                    pixels: vec![
                        0;
                        (200 * client_buffer_scale * 100 * client_buffer_scale) as usize
                    ],
                },
            },
        );
        scene.surface_outputs.insert(
            surface_id,
            SurfaceOutputMembership {
                primary_output_id: output_id,
                output_ids: vec![output_id],
                preferred_scale: RationalScale::default(),
                client_buffer_scale,
                layout_generation: 1,
            },
        );
        scene
    }

    #[test]
    fn incremental_surface_damage_preserves_bounded_native_region() {
        let scene = damage_scene(1);
        let damage = BTreeMap::from([(
            41,
            vec![DamageRectangle {
                x: 2,
                y: 3,
                width: 64,
                height: 32,
            }],
        )]);

        assert_eq!(
            calculate_output_damage(&scene, &damage, false),
            BTreeMap::from([(11, vec![Rectangle::new(22, 33, 64, 32)])])
        );
    }

    #[test]
    fn structural_change_forces_complete_output_damage() {
        let scene = damage_scene(1);

        assert_eq!(
            calculate_output_damage(&scene, &BTreeMap::new(), true),
            BTreeMap::from([(11, vec![Rectangle::new(0, 0, 800, 600)])])
        );
    }

    #[test]
    fn empty_scene_presentation_uses_layout_generation() {
        let mut scene = damage_scene(1);
        scene.configuration_generation = 7;
        scene.surfaces.clear();
        scene.surface_outputs.clear();

        assert_eq!(presentation_state_generation(&scene, &BTreeMap::new()), 7);
    }

    #[test]
    fn padded_stride_import_keeps_only_visible_pixels() {
        let attachment = buffer_attachment(2, 2, 12);
        let bytes = [
            1, 0, 0, 0, 2, 0, 0, 0, 99, 99, 99, 99, 3, 0, 0, 0, 4, 0, 0, 0,
        ];

        let buffer = decode_surface_buffer(&attachment, &bytes).unwrap();

        assert_eq!(buffer.stride_pixels, 2);
        assert_eq!(buffer.pixels, vec![1, 2, 3, 4]);
    }

    #[test]
    fn single_row_import_does_not_allocate_from_padded_stride() {
        let attachment = buffer_attachment(1, 1, u32::MAX - 3);

        let buffer = decode_surface_buffer(&attachment, &[7, 0, 0, 0]).unwrap();

        assert_eq!(required_buffer_bytes(&attachment).unwrap(), 4);
        assert_eq!(buffer.stride_pixels, 1);
        assert_eq!(buffer.pixels, vec![7]);
    }

    #[test]
    fn snapshot_expected_item_count_is_bounded_before_staging() {
        assert!(SnapshotTransaction::new(snapshot_begin(MAXIMUM_SNAPSHOT_ITEMS)).is_ok());
        assert!(SnapshotTransaction::new(snapshot_begin(MAXIMUM_SNAPSHOT_ITEMS + 1)).is_err());
    }

    #[test]
    fn snapshot_mutation_queue_is_bounded() {
        let mut snapshot =
            SnapshotTransaction::new(snapshot_begin(MAXIMUM_SNAPSHOT_ITEMS)).unwrap();
        for item in 0..MAXIMUM_SNAPSHOT_ITEMS {
            stage_snapshot_mutation(
                &mut snapshot,
                SnapshotMutation::SurfaceBuffersRemove(u64::from(item)),
            )
            .unwrap();
        }

        assert!(
            stage_snapshot_mutation(
                &mut snapshot,
                SnapshotMutation::SurfaceBuffersRemove(u64::MAX)
            )
            .is_err()
        );
    }

    #[test]
    fn pending_release_queue_is_bounded() {
        let (transport, _peer) = Transport::pair(TransportLimits::default()).unwrap();
        let mut connection = Connection {
            transport,
            negotiated: None,
            validator: None,
            peer: PeerState::default(),
            next_sequence: 2,
            liveness: PeerLiveness::awaiting_hello(Instant::now(), PeerTimeouts::default()),
        };
        connection.peer.releases.resize(
            MAXIMUM_PENDING_RELEASES,
            BufferRelease {
                buffer_id: 1,
                reason: BufferReleaseReason::Invalid,
            },
        );

        assert!(
            connection
                .queue_release(2, BufferReleaseReason::Invalid)
                .is_err()
        );
    }

    fn peer_timeouts() -> PeerTimeouts {
        PeerTimeouts {
            awaiting_hello: Duration::from_millis(10),
            initial_frame: Duration::from_millis(20),
            snapshot: Duration::from_millis(30),
        }
    }

    #[test]
    fn awaiting_hello_and_initial_frame_deadlines_are_absolute() {
        let started = Instant::now();
        let mut liveness = PeerLiveness::awaiting_hello(started, peer_timeouts());
        assert_eq!(
            liveness.expired(started + Duration::from_millis(10)),
            Some(ExpiredDeadline::AwaitingHello)
        );

        let established = started + Duration::from_millis(2);
        liveness.handshake_accepted(established);
        liveness.dispatched(established + Duration::from_millis(19), false, false);
        assert_eq!(
            liveness.expired(established + Duration::from_millis(20)),
            Some(ExpiredDeadline::InitialFrame),
            "uncommitted traffic must not refresh the initial frame deadline"
        );
        liveness.dispatched(established + Duration::from_millis(19), true, false);
        assert_eq!(
            liveness.expired(established + Duration::from_secs(60)),
            None
        );
    }

    #[test]
    fn snapshot_deadline_is_not_refreshed_by_snapshot_items() {
        let started = Instant::now();
        let mut liveness = PeerLiveness::awaiting_hello(started, peer_timeouts());
        liveness.handshake_accepted(started);
        liveness.dispatched(started, true, false);

        let begin = started + Duration::from_millis(5);
        liveness.dispatched(begin, false, true);
        liveness.dispatched(begin + Duration::from_millis(29), false, true);
        assert_eq!(
            liveness.expired(begin + Duration::from_millis(30)),
            Some(ExpiredDeadline::Snapshot)
        );

        liveness.dispatched(begin + Duration::from_millis(20), false, false);
        assert_eq!(liveness.expired(begin + Duration::from_secs(1)), None);
        liveness.dispatched(begin + Duration::from_secs(2), false, true);
        assert_eq!(
            liveness.expired(begin + Duration::from_secs(2) + Duration::from_millis(30)),
            Some(ExpiredDeadline::Snapshot),
            "a later snapshot receives a fresh absolute deadline"
        );
    }
}
