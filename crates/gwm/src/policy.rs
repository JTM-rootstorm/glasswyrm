use std::collections::BTreeMap;
use std::fmt;

use gw_types::{
    Capabilities, Generation, MessageFlags, MessageType, OutputId, Sequence, SnapshotDomain,
    WindowId,
};
use gw_wire::compositor::TriState as WireTriState;
use gw_wire::vrr::{
    PolicyOutputVrrState, PolicyOutputVrrUpsert, PolicyWindowVrrState, PolicyWindowVrrUpsert,
    VrrPolicyMode, VrrWindowPreference, decode_policy_output_vrr_upsert,
    decode_policy_window_vrr_upsert, encode_policy_output_vrr_state,
    encode_policy_window_vrr_state,
};
use gw_wire::{
    Envelope, PolicyAcknowledged, PolicyAppliedState, PolicyBindingsUpsert, PolicyCommit,
    PolicyContextUpsert, PolicyMapIntent, PolicyOutputUpsert, PolicyResult, PolicyStackMode,
    PolicyWindowOutputHint, PolicyWindowState, PolicyWindowType, SnapshotBegin, SnapshotEnd,
    decode_policy_commit, decode_policy_context_upsert, decode_policy_lifecycle_window_upsert,
    decode_policy_output_upsert, decode_policy_window_output_hint, decode_policy_window_remove,
    decode_policy_window_upsert, decode_snapshot_abort, decode_snapshot_begin, decode_snapshot_end,
    encode_policy_acknowledged, encode_policy_bindings_upsert, encode_policy_window_state,
    encode_snapshot_begin, encode_snapshot_end,
};
use gwm_core::{
    AppliedState, Context, DecorationPreference, EvaluationError, OutputContext, PolicyState,
    RawState, RawWindow, Rectangle, StackMode, TriState, VrrWindowInput,
    VrrWindowPreference as CoreVrrPreference, WindowOutputHint, WindowState, WindowType, evaluate,
};

const MAXIMUM_WINDOWS: usize = 4_096;
const MAXIMUM_OUTPUTS: usize = 8;
const VRR_MEMBERSHIP_TAG: u64 = 0x8000_4757_5252_1400;
const VRR_MEMBERSHIP_MASK: u64 = 0xffff_ffff_ffff_ff00;

const REASON_OUTPUT_DISABLED: u64 = 1 << 0;
const REASON_OUTPUT_NOT_VRR_CAPABLE: u64 = 1 << 3;
const REASON_ATOMIC_KMS_UNAVAILABLE: u64 = 1 << 4;
const REASON_POLICY_OFF: u64 = 1 << 10;
const REASON_NO_CANDIDATE: u64 = 1 << 11;
const REASON_WINDOW_NOT_FULLSCREEN: u64 = 1 << 16;
const REASON_WINDOW_NOT_BORDERLESS_FULLSCREEN: u64 = 1 << 17;
const REASON_WINDOW_DID_NOT_REQUEST: u64 = 1 << 20;
const REASON_MANUAL_ALWAYS_ELIGIBLE: u64 = 1 << 32;

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct OutgoingRecord {
    pub message_type: MessageType,
    pub flags: MessageFlags,
    pub reply_to: Sequence,
    pub payload: Vec<u8>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct DispatchOutcome {
    pub records: Vec<OutgoingRecord>,
    pub accepted: bool,
}

impl DispatchOutcome {
    fn none() -> Self {
        Self {
            records: Vec::new(),
            accepted: false,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum DispatchError {
    UnsupportedMessage,
    MalformedPayload,
    SnapshotViolation,
    StateViolation,
}

impl fmt::Display for DispatchError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(match self {
            Self::UnsupportedMessage => "unsupported GWIPC policy message",
            Self::MalformedPayload => "malformed GWIPC policy payload",
            Self::SnapshotViolation => "invalid GWIPC policy snapshot transition",
            Self::StateViolation => "invalid GWIPC policy state transition",
        })
    }
}

impl std::error::Error for DispatchError {}

#[derive(Clone, Debug, Default)]
struct ExtendedRaw {
    raw: RawState,
    context_wire: Option<PolicyContextUpsert>,
    output_wire: BTreeMap<OutputId, PolicyOutputUpsert>,
    hint_wire: BTreeMap<WindowId, PolicyWindowOutputHint>,
    vrr_outputs: BTreeMap<OutputId, PolicyOutputVrrUpsert>,
    vrr_windows: BTreeMap<WindowId, PolicyWindowVrrUpsert>,
}

#[derive(Clone, Copy, Debug)]
struct ActiveSnapshot {
    id: u64,
    generation: u64,
}

#[derive(Clone, Debug, Default)]
pub struct PeerPolicy {
    pending: ExtendedRaw,
    committed: ExtendedRaw,
    committed_policy: Option<PolicyState>,
    committed_hash: u64,
    pre_snapshot: Option<ExtendedRaw>,
    snapshot: Option<ActiveSnapshot>,
    last_commit_id: u64,
    last_generation: u64,
}

impl PeerPolicy {
    #[must_use]
    pub fn new() -> Self {
        Self::default()
    }

    pub fn disconnect(&mut self) {
        *self = Self::default();
    }

    pub fn dispatch(
        &mut self,
        envelope: &Envelope,
        payload: &[u8],
        capabilities: Capabilities,
    ) -> Result<DispatchOutcome, DispatchError> {
        match envelope.message_type {
            MessageType::SNAPSHOT_BEGIN => self.begin(payload),
            MessageType::SNAPSHOT_END => self.end(payload),
            MessageType::SNAPSHOT_ABORT => self.abort(payload),
            MessageType::POLICY_CONTEXT_UPSERT => self.context(payload),
            MessageType::POLICY_WINDOW_UPSERT => self.window(payload, false),
            MessageType::POLICY_LIFECYCLE_WINDOW_UPSERT => self.window(payload, true),
            MessageType::POLICY_OUTPUT_UPSERT => self.output(payload, capabilities),
            MessageType::POLICY_WINDOW_OUTPUT_HINT => self.hint(payload, capabilities),
            MessageType::POLICY_OUTPUT_VRR_UPSERT => self.vrr_output(payload, capabilities),
            MessageType::POLICY_WINDOW_VRR_UPSERT => self.vrr_window(payload, capabilities),
            MessageType::POLICY_WINDOW_REMOVE => self.remove(payload),
            MessageType::POLICY_COMMIT => self.commit(envelope.sequence, payload, capabilities),
            _ => Err(DispatchError::UnsupportedMessage),
        }
    }

    fn begin(&mut self, payload: &[u8]) -> Result<DispatchOutcome, DispatchError> {
        let value = decode_snapshot_begin(payload).map_err(|_| DispatchError::MalformedPayload)?;
        if value.domain != SnapshotDomain::WindowPolicy || self.snapshot.is_some() {
            return Err(DispatchError::SnapshotViolation);
        }
        self.pre_snapshot = Some(std::mem::take(&mut self.pending));
        self.snapshot = Some(ActiveSnapshot {
            id: value.snapshot_id.get(),
            generation: value.generation.get(),
        });
        Ok(DispatchOutcome::none())
    }

    fn end(&mut self, payload: &[u8]) -> Result<DispatchOutcome, DispatchError> {
        let value = decode_snapshot_end(payload).map_err(|_| DispatchError::MalformedPayload)?;
        let Some(active) = self.snapshot else {
            return Err(DispatchError::SnapshotViolation);
        };
        if value.snapshot_id.get() != active.id
            || value.generation.get() != active.generation
            || self.pending.context_wire.is_none()
        {
            return Err(DispatchError::SnapshotViolation);
        }
        self.pending.raw.complete = true;
        self.snapshot = None;
        self.pre_snapshot = None;
        Ok(DispatchOutcome::none())
    }

    fn abort(&mut self, payload: &[u8]) -> Result<DispatchOutcome, DispatchError> {
        let value = decode_snapshot_abort(payload).map_err(|_| DispatchError::MalformedPayload)?;
        let Some(active) = self.snapshot else {
            return Err(DispatchError::SnapshotViolation);
        };
        if value.snapshot_id.get() != active.id {
            return Err(DispatchError::SnapshotViolation);
        }
        self.pending = self.pre_snapshot.take().unwrap_or_default();
        self.snapshot = None;
        Ok(DispatchOutcome::none())
    }

    fn can_mutate(&self) -> bool {
        self.snapshot.is_some() || self.committed.raw.complete
    }

    fn context(&mut self, payload: &[u8]) -> Result<DispatchOutcome, DispatchError> {
        let value =
            decode_policy_context_upsert(payload).map_err(|_| DispatchError::MalformedPayload)?;
        if !self.can_mutate() || (self.snapshot.is_some() && self.pending.context_wire.is_some()) {
            return Err(DispatchError::StateViolation);
        }
        self.pending.raw.context = Context {
            root_window_id: value.root_window_id.into(),
            workspace_id: value.workspace_id,
            primary_output_id: value.output_id.into(),
            work: Rectangle {
                x: value.work_x,
                y: value.work_y,
                width: value.work_width,
                height: value.work_height,
            },
        };
        self.pending.context_wire = Some(value);
        Ok(DispatchOutcome::none())
    }

    fn window(
        &mut self,
        payload: &[u8],
        lifecycle: bool,
    ) -> Result<DispatchOutcome, DispatchError> {
        if !self.can_mutate() {
            return Err(DispatchError::StateViolation);
        }
        let (value, geometry_serial, stack_serial, stack_sibling, stack_mode) = if lifecycle {
            let value = decode_policy_lifecycle_window_upsert(payload)
                .map_err(|_| DispatchError::MalformedPayload)?;
            (
                value.window,
                value.geometry_serial,
                value.stack_serial,
                value.stack_sibling,
                value.stack_mode,
            )
        } else {
            (
                decode_policy_window_upsert(payload)
                    .map_err(|_| DispatchError::MalformedPayload)?,
                0,
                0,
                0,
                PolicyStackMode::None,
            )
        };
        let id = WindowId::new(value.window_id);
        if !self.pending.raw.windows.contains_key(&id)
            && self.pending.raw.windows.len() >= MAXIMUM_WINDOWS
        {
            return Err(DispatchError::StateViolation);
        }
        self.pending.raw.windows.insert(
            id,
            RawWindow {
                window_id: id,
                parent_window_id: value.parent_window_id.into(),
                transient_for: (value.transient_for != 0).then(|| value.transient_for.into()),
                workspace_id: (value.workspace_id != 0).then_some(value.workspace_id),
                requested: Rectangle {
                    x: value.requested_x,
                    y: value.requested_y,
                    width: value.requested_width,
                    height: value.requested_height,
                },
                border_width: value.border_width,
                window_type: window_type(value.window_type),
                wants_map: value.map_intent == PolicyMapIntent::WantsMap,
                override_redirect: value.override_redirect,
                decoration_preference: decoration(value.decoration_preference),
                fullscreen_requested: value.fullscreen_requested,
                maximized_requested: value.maximized_requested,
                minimized_requested: value.minimized_requested,
                attention_requested: value.attention_requested,
                creation_serial: value.creation_serial,
                map_serial: value.map_serial,
                focus_serial: value.focus_serial,
                geometry_serial,
                stack_serial,
                stack_sibling: (stack_sibling != 0).then(|| stack_sibling.into()),
                stack_mode: stack_mode_from(stack_mode),
                flags: value.flags,
            },
        );
        Ok(DispatchOutcome::none())
    }

    fn output(
        &mut self,
        payload: &[u8],
        capabilities: Capabilities,
    ) -> Result<DispatchOutcome, DispatchError> {
        if !multi_output(capabilities) || !self.can_mutate() {
            return Err(DispatchError::StateViolation);
        }
        let value =
            decode_policy_output_upsert(payload).map_err(|_| DispatchError::MalformedPayload)?;
        let id = OutputId::new(value.output_id);
        if !self.pending.raw.outputs.contains_key(&id)
            && self.pending.raw.outputs.len() >= MAXIMUM_OUTPUTS
        {
            return Err(DispatchError::StateViolation);
        }
        self.pending.raw.outputs.insert(
            id,
            OutputContext {
                output_id: id,
                logical: Rectangle {
                    x: value.logical_x,
                    y: value.logical_y,
                    width: value.logical_width,
                    height: value.logical_height,
                },
                work: Rectangle {
                    x: value.work_x,
                    y: value.work_y,
                    width: value.work_width,
                    height: value.work_height,
                },
                enabled: value.enabled,
                primary: value.primary,
            },
        );
        self.pending.output_wire.insert(id, value);
        Ok(DispatchOutcome::none())
    }

    fn hint(
        &mut self,
        payload: &[u8],
        capabilities: Capabilities,
    ) -> Result<DispatchOutcome, DispatchError> {
        if !multi_output(capabilities) || !self.can_mutate() {
            return Err(DispatchError::StateViolation);
        }
        let value = decode_policy_window_output_hint(payload)
            .map_err(|_| DispatchError::MalformedPayload)?;
        let id = WindowId::new(value.window_id);
        if !self.pending.raw.output_hints.contains_key(&id)
            && self.pending.raw.output_hints.len() >= MAXIMUM_WINDOWS
        {
            return Err(DispatchError::StateViolation);
        }
        self.pending.raw.output_hints.insert(
            id,
            WindowOutputHint {
                previous_output_id: value.previous_output_id.into(),
                preferred_output_id: value.preferred_output_id.into(),
            },
        );
        self.pending.hint_wire.insert(id, value);
        Ok(DispatchOutcome::none())
    }

    fn vrr_output(
        &mut self,
        payload: &[u8],
        capabilities: Capabilities,
    ) -> Result<DispatchOutcome, DispatchError> {
        if !vrr_profile(capabilities) || self.snapshot.is_none() {
            return Err(DispatchError::StateViolation);
        }
        let value = decode_policy_output_vrr_upsert(payload)
            .map_err(|_| DispatchError::MalformedPayload)?;
        let id = OutputId::new(value.output_id);
        if self.pending.vrr_outputs.contains_key(&id)
            || self.pending.vrr_outputs.len() >= MAXIMUM_OUTPUTS
        {
            return Err(DispatchError::StateViolation);
        }
        self.pending.vrr_outputs.insert(id, value);
        Ok(DispatchOutcome::none())
    }

    fn vrr_window(
        &mut self,
        payload: &[u8],
        capabilities: Capabilities,
    ) -> Result<DispatchOutcome, DispatchError> {
        if !vrr_profile(capabilities) || self.snapshot.is_none() {
            return Err(DispatchError::StateViolation);
        }
        let value = decode_policy_window_vrr_upsert(payload)
            .map_err(|_| DispatchError::MalformedPayload)?;
        let id = WindowId::new(value.window_id);
        if self.pending.vrr_windows.contains_key(&id)
            || self.pending.vrr_windows.len() >= MAXIMUM_WINDOWS
        {
            return Err(DispatchError::StateViolation);
        }
        self.pending.vrr_windows.insert(id, value);
        Ok(DispatchOutcome::none())
    }

    fn remove(&mut self, payload: &[u8]) -> Result<DispatchOutcome, DispatchError> {
        let value =
            decode_policy_window_remove(payload).map_err(|_| DispatchError::MalformedPayload)?;
        let id = WindowId::new(value.window_id);
        if self.snapshot.is_some()
            || !self.committed.raw.complete
            || self.pending.raw.windows.remove(&id).is_none()
        {
            return Err(DispatchError::StateViolation);
        }
        self.pending.raw.output_hints.remove(&id);
        self.pending.hint_wire.remove(&id);
        self.pending.vrr_windows.remove(&id);
        Ok(DispatchOutcome::none())
    }

    fn commit(
        &mut self,
        reply_to: Sequence,
        payload: &[u8],
        capabilities: Capabilities,
    ) -> Result<DispatchOutcome, DispatchError> {
        let commit = decode_policy_commit(payload).map_err(|_| DispatchError::MalformedPayload)?;
        let previous_hash = self.committed_hash;
        if commit.commit_id <= self.last_commit_id
            || commit.producer_generation < self.last_generation
        {
            return Ok(self.rejection(
                reply_to,
                commit,
                PolicyResult::RejectedInvalidWindow,
                previous_hash,
            ));
        }
        self.last_commit_id = commit.commit_id;
        self.last_generation = commit.producer_generation;
        if multi_output(capabilities) && self.pending.raw.outputs.is_empty() {
            return Ok(self.rejection(
                reply_to,
                commit,
                PolicyResult::RejectedIncompleteSnapshot,
                previous_hash,
            ));
        }
        if self.snapshot.is_some() {
            return Ok(self.rejection(
                reply_to,
                commit,
                PolicyResult::RejectedIncompleteSnapshot,
                previous_hash,
            ));
        }
        let mut candidate = self.pending.clone();
        candidate.raw.producer_generation = Generation::new(commit.producer_generation);
        let policy = match evaluate(&candidate.raw, Generation::new(commit.producer_generation)) {
            Ok(policy) => policy,
            Err(error) => {
                return Ok(self.rejection(reply_to, commit, result_from(error), previous_hash));
            }
        };
        let mut base_hash = policy_hash(&candidate, &policy);
        let interactive = capabilities.contains(Capabilities::INTERACTIVE_POLICY);
        if interactive {
            base_hash = interactive_hash(base_hash, !policy.outputs.is_empty());
        }
        let vrr = if vrr_profile(capabilities) {
            match evaluate_vrr(&candidate, &policy, base_hash) {
                Ok(policy) => Some(policy),
                Err(result) => return Ok(self.rejection(reply_to, commit, result, previous_hash)),
            }
        } else {
            None
        };
        let result_hash = vrr.as_ref().map_or(base_hash, |policy| policy.hash);
        let records = response_records(
            reply_to,
            commit,
            &policy,
            interactive,
            vrr.as_ref(),
            result_hash,
        );
        self.committed = candidate.clone();
        self.pending = candidate;
        self.committed_policy = Some(policy);
        self.committed_hash = result_hash;
        Ok(DispatchOutcome {
            records,
            accepted: true,
        })
    }

    fn rejection(
        &self,
        reply_to: Sequence,
        commit: PolicyCommit,
        result: PolicyResult,
        hash: u64,
    ) -> DispatchOutcome {
        let applied_generation = self
            .committed_policy
            .as_ref()
            .map_or(0, |policy| policy.generation.get());
        let window_count = self.committed_policy.as_ref().map_or(0, |policy| {
            u32::try_from(policy.windows.len()).expect("window limit fits u32")
        });
        DispatchOutcome {
            records: vec![record(
                MessageType::POLICY_ACKNOWLEDGED,
                MessageFlags::REPLY,
                reply_to,
                encode_policy_acknowledged(&PolicyAcknowledged {
                    commit_id: commit.commit_id,
                    producer_generation: commit.producer_generation,
                    applied_generation,
                    policy_hash: hash,
                    window_count,
                    result,
                }),
            )],
            accepted: false,
        }
    }
}

fn response_records(
    reply_to: Sequence,
    commit: PolicyCommit,
    policy: &PolicyState,
    interactive: bool,
    vrr: Option<&VrrPolicy>,
    hash: u64,
) -> Vec<OutgoingRecord> {
    let vrr_count = vrr.map_or(0, |value| value.outputs.len() + value.windows.len());
    let count = policy.output_order.len() + usize::from(interactive) + vrr_count;
    let mut records = Vec::with_capacity(count + 3);
    records.push(record(
        MessageType::SNAPSHOT_BEGIN,
        no_flags(),
        Sequence::new(0),
        encode_snapshot_begin(SnapshotBegin {
            snapshot_id: commit.commit_id.into(),
            domain: SnapshotDomain::WindowPolicy,
            generation: policy.generation,
            expected_item_count: u32::try_from(count).expect("policy count fits u32"),
            flags: 0,
        }),
    ));
    if interactive {
        records.push(record(
            MessageType::POLICY_BINDINGS_UPSERT,
            MessageFlags::SNAPSHOT_ITEM,
            Sequence::new(0),
            encode_policy_bindings_upsert(&bindings()),
        ));
    }
    for id in &policy.output_order {
        records.push(record(
            MessageType::POLICY_WINDOW_STATE,
            MessageFlags::SNAPSHOT_ITEM,
            Sequence::new(0),
            encode_policy_window_state(&wire_window_state(&policy.windows[id])),
        ));
    }
    if let Some(vrr) = vrr {
        for state in vrr.outputs.values() {
            records.push(record(
                MessageType::POLICY_OUTPUT_VRR_STATE,
                MessageFlags::SNAPSHOT_ITEM,
                Sequence::new(0),
                encode_policy_output_vrr_state(state),
            ));
        }
        for state in vrr.windows.values() {
            records.push(record(
                MessageType::POLICY_WINDOW_VRR_STATE,
                MessageFlags::SNAPSHOT_ITEM,
                Sequence::new(0),
                encode_policy_window_vrr_state(state),
            ));
        }
    }
    records.push(record(
        MessageType::SNAPSHOT_END,
        no_flags(),
        Sequence::new(0),
        encode_snapshot_end(SnapshotEnd {
            snapshot_id: commit.commit_id.into(),
            generation: policy.generation,
            actual_item_count: u32::try_from(count).expect("policy count fits u32"),
        }),
    ));
    records.push(record(
        MessageType::POLICY_ACKNOWLEDGED,
        MessageFlags::REPLY,
        reply_to,
        encode_policy_acknowledged(&PolicyAcknowledged {
            commit_id: commit.commit_id,
            producer_generation: commit.producer_generation,
            applied_generation: policy.generation.get(),
            policy_hash: hash,
            window_count: u32::try_from(policy.windows.len()).expect("window limit fits u32"),
            result: PolicyResult::Accepted,
        }),
    ));
    records
}

fn record(
    message_type: MessageType,
    flags: MessageFlags,
    reply_to: Sequence,
    payload: Vec<u8>,
) -> OutgoingRecord {
    OutgoingRecord {
        message_type,
        flags,
        reply_to,
        payload,
    }
}

#[derive(Clone, Debug)]
struct VrrPolicy {
    outputs: BTreeMap<OutputId, PolicyOutputVrrState>,
    windows: BTreeMap<WindowId, PolicyWindowVrrState>,
    hash: u64,
}

fn evaluate_vrr(
    raw: &ExtendedRaw,
    base: &PolicyState,
    base_hash: u64,
) -> Result<VrrPolicy, PolicyResult> {
    if raw.vrr_outputs.len() != base.outputs.len() {
        return Err(PolicyResult::RejectedInvalidContext);
    }
    let managed_count = base
        .windows
        .values()
        .filter(|window| window.managed && !window.override_redirect)
        .count();
    if raw.vrr_windows.len() != managed_count {
        return Err(PolicyResult::RejectedInvalidWindow);
    }
    if raw
        .vrr_outputs
        .keys()
        .any(|id| !base.outputs.contains_key(id))
    {
        return Err(PolicyResult::RejectedUnknownReference);
    }
    let output_ids: Vec<_> = base.outputs.keys().copied().collect();
    let mut memberships = BTreeMap::new();
    for (id, input) in &raw.vrr_windows {
        let Some(window) = base
            .windows
            .get(id)
            .filter(|window| window.managed && !window.override_redirect)
        else {
            return Err(PolicyResult::RejectedUnknownReference);
        };
        let Some(hint) = raw.hint_wire.get(id) else {
            return Err(PolicyResult::RejectedInvalidWindow);
        };
        let membership = decode_membership(&output_ids, hint.preferred_output_id)
            .ok_or(PolicyResult::RejectedInvalidWindow)?;
        if membership.iter().any(|output| {
            base.outputs
                .get(output)
                .is_none_or(|output| !output.enabled)
        }) {
            return Err(PolicyResult::RejectedUnknownReference);
        }
        let core_input = VrrWindowInput {
            window_id: *id,
            preference: core_preference(input.preference),
            output_membership: membership.clone(),
        };
        let Some(classification) = gwm_core::classify_vrr_window(&raw.raw, base, &core_input)
        else {
            return Err(PolicyResult::RejectedInvalidWindow);
        };
        let mode = raw.vrr_outputs[&window.output_id].mode;
        let mut reason = classification.reason.bits();
        let common = classification.common_candidate;
        let eligible = match mode {
            VrrPolicyMode::Off => {
                reason |= REASON_POLICY_OFF;
                false
            }
            VrrPolicyMode::Fullscreen => {
                if !classification.fullscreen && !classification.borderless_fullscreen {
                    reason |= REASON_WINDOW_NOT_FULLSCREEN;
                    reason |= REASON_WINDOW_NOT_BORDERLESS_FULLSCREEN;
                }
                common
                    && (classification.fullscreen || classification.borderless_fullscreen)
                    && input.preference != VrrWindowPreference::Disable
            }
            VrrPolicyMode::Focused => common && input.preference != VrrWindowPreference::Disable,
            VrrPolicyMode::AppRequested => {
                if input.preference != VrrWindowPreference::Prefer {
                    reason |= REASON_WINDOW_DID_NOT_REQUEST;
                }
                common && input.preference == VrrWindowPreference::Prefer
            }
            VrrPolicyMode::AlwaysEligible => common,
        };
        memberships.insert(
            *id,
            (
                membership,
                PolicyWindowVrrState {
                    window_id: id.get(),
                    output_id: classification.output_id.get(),
                    preference: input.preference,
                    selected: false,
                    eligible,
                    focused: classification.focused,
                    fullscreen: classification.fullscreen,
                    borderless_fullscreen: classification.borderless_fullscreen,
                    exclusive_output_membership: classification.exclusive_output_membership,
                    reason_flags: reason,
                    flags: 0,
                },
            ),
        );
    }
    let mut windows: BTreeMap<_, _> = memberships
        .iter()
        .map(|(id, (_, state))| (*id, *state))
        .collect();
    let mut outputs = BTreeMap::new();
    for (id, input) in &raw.vrr_outputs {
        let output = base.outputs[id];
        let candidate_required = matches!(
            input.mode,
            VrrPolicyMode::Fullscreen | VrrPolicyMode::Focused | VrrPolicyMode::AppRequested
        );
        let mut reason = 0;
        if !output.enabled {
            reason |= REASON_OUTPUT_DISABLED;
        }
        if !input.hardware_capable {
            reason |= REASON_OUTPUT_NOT_VRR_CAPABLE;
        }
        if !input.kms_controllable {
            reason |= REASON_ATOMIC_KMS_UNAVAILABLE;
        }
        if input.mode == VrrPolicyMode::Off {
            reason |= REASON_POLICY_OFF;
        }
        if input.mode == VrrPolicyMode::AlwaysEligible {
            reason |= REASON_MANUAL_ALWAYS_ELIGIBLE;
        }
        let selected = windows.iter().find_map(|(window_id, window)| {
            (window.output_id == id.get() && window.eligible && window.focused)
                .then_some(*window_id)
        });
        if let Some(selected) = selected {
            windows
                .get_mut(&selected)
                .expect("selected window exists")
                .selected = true;
        } else if candidate_required {
            reason |= REASON_NO_CANDIDATE;
        }
        let selected_window_id = selected.map_or(0, WindowId::get);
        outputs.insert(
            *id,
            PolicyOutputVrrState {
                output_id: id.get(),
                mode: input.mode,
                selected_window_id,
                desired_enabled: output.enabled
                    && input.kms_controllable
                    && input.mode != VrrPolicyMode::Off
                    && (!candidate_required || selected_window_id != 0),
                candidate_required,
                reason_flags: reason,
                flags: 0,
            },
        );
    }
    let hash = vrr_hash(base_hash, raw, &memberships, &outputs, &windows);
    Ok(VrrPolicy {
        outputs,
        windows,
        hash,
    })
}

fn decode_membership(outputs: &[OutputId], encoded: u64) -> Option<Vec<OutputId>> {
    if outputs.is_empty()
        || outputs.len() > MAXIMUM_OUTPUTS
        || (encoded & VRR_MEMBERSHIP_MASK) != VRR_MEMBERSHIP_TAG
    {
        return None;
    }
    let bits = encoded as u8;
    let valid = ((1_u16 << outputs.len()) - 1) as u8;
    if bits & !valid != 0 {
        return None;
    }
    Some(
        outputs
            .iter()
            .enumerate()
            .filter_map(|(index, output)| (bits & (1 << index) != 0).then_some(*output))
            .collect(),
    )
}

fn policy_hash(raw: &ExtendedRaw, policy: &PolicyState) -> u64 {
    let mut hash = Fnv::new();
    hash.bytes(if policy.outputs.is_empty() {
        b"glasswyrm-policy-v1"
    } else {
        b"glasswyrm-policy-v3"
    });
    hash.u64(policy.generation.get());
    let context = raw.context_wire.expect("evaluated state has context");
    hash.u32(context.root_window_id);
    hash.u32(context.workspace_id);
    hash.u64(context.output_id);
    hash.i32(context.work_x);
    hash.i32(context.work_y);
    hash.u32(context.work_width);
    hash.u32(context.work_height);
    hash.u32(context.flags);
    if !policy.outputs.is_empty() {
        hash.u32(policy.outputs.len() as u32);
        for output in raw.output_wire.values() {
            hash.u64(output.output_id);
            hash.i32(output.logical_x);
            hash.i32(output.logical_y);
            hash.u32(output.logical_width);
            hash.u32(output.logical_height);
            hash.i32(output.work_x);
            hash.i32(output.work_y);
            hash.u32(output.work_width);
            hash.u32(output.work_height);
            hash.u32(output.scale_numerator);
            hash.u32(output.scale_denominator);
            hash.u8(output.transform as u8);
            hash.bool(output.enabled);
            hash.bool(output.primary);
            hash.u32(output.flags);
        }
        hash.u32(raw.hint_wire.len() as u32);
        for hint in raw.hint_wire.values() {
            hash.u32(hint.window_id);
            hash.u64(hint.previous_output_id);
            hash.u64(hint.preferred_output_id);
            hash.u32(hint.flags);
        }
    }
    for id in &policy.output_order {
        hash.bytes(&encode_policy_window_state(&wire_window_state(
            &policy.windows[id],
        )));
    }
    hash.finish()
}

fn interactive_hash(base: u64, multi_output: bool) -> u64 {
    let value = bindings();
    let mut hash = Fnv::new();
    hash.bytes(if multi_output {
        b"glasswyrm-policy-v3"
    } else {
        b"glasswyrm-policy-v2"
    });
    hash.u64(base);
    hash.u16(value.move_modifiers);
    hash.u16(value.resize_modifiers);
    hash.u16(value.close_modifiers);
    hash.u8(value.move_button);
    hash.u8(value.resize_button);
    hash.u32(value.close_keysym);
    hash.u32(value.minimum_width);
    hash.u32(value.minimum_height);
    hash.bool(value.raise_on_focus);
    hash.bool(value.consume_wm_bindings);
    hash.finish()
}

fn vrr_hash(
    base_hash: u64,
    raw: &ExtendedRaw,
    memberships: &BTreeMap<WindowId, (Vec<OutputId>, PolicyWindowVrrState)>,
    outputs: &BTreeMap<OutputId, PolicyOutputVrrState>,
    windows: &BTreeMap<WindowId, PolicyWindowVrrState>,
) -> u64 {
    let mut hash = Fnv::new();
    hash.bytes(b"glasswyrm-policy-v4");
    hash.u64(base_hash);
    hash.u32(raw.vrr_outputs.len() as u32);
    for output in raw.vrr_outputs.values() {
        hash.u64(output.output_id);
        hash.u16(output.mode as u16);
        hash.bool(output.hardware_capable);
        hash.bool(output.kms_controllable);
        hash.u32(output.flags);
    }
    hash.u32(raw.vrr_windows.len() as u32);
    for (id, input) in &raw.vrr_windows {
        hash.u32(id.get());
        hash.u16(input.preference as u16);
        let membership = &memberships[id].0;
        hash.u32(membership.len() as u32);
        for output in membership {
            hash.u64(output.get());
        }
        hash.u32(input.flags);
    }
    hash.u32(outputs.len() as u32);
    for output in outputs.values() {
        hash.u64(output.output_id);
        hash.u16(output.mode as u16);
        hash.u32(output.selected_window_id);
        hash.bool(output.desired_enabled);
        hash.bool(output.candidate_required);
        hash.u64(output.reason_flags);
        hash.u32(output.flags);
    }
    hash.u32(windows.len() as u32);
    for window in windows.values() {
        hash.u32(window.window_id);
        hash.u64(window.output_id);
        hash.u16(window.preference as u16);
        hash.bool(window.selected);
        hash.bool(window.eligible);
        hash.bool(window.focused);
        hash.bool(window.fullscreen);
        hash.bool(window.borderless_fullscreen);
        hash.bool(window.exclusive_output_membership);
        hash.u64(window.reason_flags);
        hash.u32(window.flags);
    }
    hash.finish()
}

struct Fnv(u64);

impl Fnv {
    const fn new() -> Self {
        Self(14_695_981_039_346_656_037)
    }

    fn bytes(&mut self, bytes: &[u8]) {
        for byte in bytes {
            self.0 ^= u64::from(*byte);
            self.0 = self.0.wrapping_mul(1_099_511_628_211);
        }
    }

    fn u8(&mut self, value: u8) {
        self.bytes(&value.to_le_bytes());
    }
    fn bool(&mut self, value: bool) {
        self.u8(u8::from(value));
    }
    fn u16(&mut self, value: u16) {
        self.bytes(&value.to_le_bytes());
    }
    fn u32(&mut self, value: u32) {
        self.bytes(&value.to_le_bytes());
    }
    fn i32(&mut self, value: i32) {
        self.bytes(&value.to_le_bytes());
    }
    fn u64(&mut self, value: u64) {
        self.bytes(&value.to_le_bytes());
    }
    const fn finish(self) -> u64 {
        self.0
    }
}

fn wire_window_state(value: &WindowState) -> PolicyWindowState {
    PolicyWindowState {
        window_id: value.window_id.get(),
        transient_for: value.transient_for.map_or(0, WindowId::get),
        workspace_id: value.workspace_id,
        output_id: value.output_id.get(),
        final_x: value.geometry.x,
        final_y: value.geometry.y,
        final_width: value.geometry.width,
        final_height: value.geometry.height,
        stacking: value.stacking.map_or(-1, |value| value as i32),
        window_type: wire_window_type(value.window_type),
        applied_state: wire_applied(value.applied_state),
        visible: value.visible,
        focused: value.focused,
        managed: value.managed,
        decoration_eligible: value.decoration_eligible,
        override_redirect: value.override_redirect,
        attention_requested: value.attention_requested,
        fullscreen_eligible: tri_state(value.fullscreen_eligible),
        direct_scanout_eligible: tri_state(value.direct_scanout_eligible),
        flags: 0,
    }
}

fn bindings() -> PolicyBindingsUpsert {
    PolicyBindingsUpsert {
        move_modifiers: 1 << 3,
        resize_modifiers: 1 << 3,
        close_modifiers: 1 << 3,
        move_button: 1,
        resize_button: 3,
        close_keysym: 0xffc1,
        minimum_width: 96,
        minimum_height: 64,
        raise_on_focus: true,
        consume_wm_bindings: true,
    }
}

fn result_from(error: EvaluationError) -> PolicyResult {
    match error {
        EvaluationError::IncompleteSnapshot => PolicyResult::RejectedIncompleteSnapshot,
        EvaluationError::InvalidGeneration | EvaluationError::InvalidWindow => {
            PolicyResult::RejectedInvalidWindow
        }
        EvaluationError::InvalidContext => PolicyResult::RejectedInvalidContext,
        EvaluationError::UnknownReference => PolicyResult::RejectedUnknownReference,
        EvaluationError::UnsupportedMetadata => PolicyResult::RejectedUnsupportedMetadata,
        EvaluationError::Limit => PolicyResult::RejectedLimit,
    }
}

fn multi_output(capabilities: Capabilities) -> bool {
    capabilities.contains(Capabilities::MULTI_OUTPUT_POLICY)
        && capabilities.contains(Capabilities::SCALE_METADATA)
}

fn vrr_profile(capabilities: Capabilities) -> bool {
    capabilities.contains(Capabilities::WINDOW_POLICY)
        && capabilities.contains(Capabilities::VRR_POLICY)
        && multi_output(capabilities)
}

fn window_type(value: PolicyWindowType) -> WindowType {
    match value {
        PolicyWindowType::Unknown => WindowType::Unknown,
        PolicyWindowType::Normal => WindowType::Normal,
        PolicyWindowType::Dialog => WindowType::Dialog,
        PolicyWindowType::Utility => WindowType::Utility,
    }
}

fn wire_window_type(value: WindowType) -> PolicyWindowType {
    match value {
        WindowType::Unknown => PolicyWindowType::Unknown,
        WindowType::Normal => PolicyWindowType::Normal,
        WindowType::Dialog => PolicyWindowType::Dialog,
        WindowType::Utility => PolicyWindowType::Utility,
    }
}

fn decoration(value: u16) -> DecorationPreference {
    match value {
        1 => DecorationPreference::False,
        2 => DecorationPreference::True,
        _ => DecorationPreference::Unknown,
    }
}

fn stack_mode_from(value: PolicyStackMode) -> StackMode {
    match value {
        PolicyStackMode::None => StackMode::None,
        PolicyStackMode::Above => StackMode::Above,
        PolicyStackMode::Below => StackMode::Below,
    }
}

fn wire_applied(value: AppliedState) -> PolicyAppliedState {
    match value {
        AppliedState::Normal => PolicyAppliedState::Normal,
        AppliedState::Maximized => PolicyAppliedState::Maximized,
        AppliedState::Fullscreen => PolicyAppliedState::Fullscreen,
        AppliedState::Minimized => PolicyAppliedState::Minimized,
    }
}

fn tri_state(value: TriState) -> u16 {
    match value {
        TriState::Unknown => WireTriState::Unknown as u16,
        TriState::False => WireTriState::False as u16,
        TriState::True => WireTriState::True as u16,
    }
}

fn core_preference(value: VrrWindowPreference) -> CoreVrrPreference {
    match value {
        VrrWindowPreference::Default => CoreVrrPreference::Default,
        VrrWindowPreference::Disable => CoreVrrPreference::Disable,
        VrrWindowPreference::Allow => CoreVrrPreference::Allow,
        VrrWindowPreference::Prefer => CoreVrrPreference::Prefer,
    }
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
    use gw_types::SnapshotId;
    use gw_wire::{
        PolicyWindowUpsert, SnapshotAbort, encode_policy_commit, encode_policy_context_upsert,
        encode_policy_window_upsert,
    };

    fn envelope(message_type: MessageType, sequence: u64) -> Envelope {
        Envelope::request(message_type, Sequence::new(sequence), 0)
    }

    fn basic_snapshot(peer: &mut PeerPolicy) {
        peer.dispatch(
            &envelope(MessageType::SNAPSHOT_BEGIN, 2),
            &encode_snapshot_begin(SnapshotBegin {
                snapshot_id: SnapshotId::new(1),
                domain: SnapshotDomain::WindowPolicy,
                generation: Generation::new(1),
                expected_item_count: 2,
                flags: 0,
            }),
            basic_capabilities(),
        )
        .unwrap();
        peer.dispatch(
            &envelope(MessageType::POLICY_CONTEXT_UPSERT, 3),
            &encode_policy_context_upsert(&PolicyContextUpsert {
                root_window_id: 1,
                workspace_id: 1,
                output_id: 1,
                work_x: 0,
                work_y: 0,
                work_width: 1024,
                work_height: 768,
                flags: 0,
            }),
            basic_capabilities(),
        )
        .unwrap();
        peer.dispatch(
            &envelope(MessageType::POLICY_WINDOW_UPSERT, 4),
            &encode_policy_window_upsert(&PolicyWindowUpsert {
                window_id: 1001,
                parent_window_id: 1,
                transient_for: 0,
                workspace_id: 1,
                requested_x: 0,
                requested_y: 0,
                requested_width: 320,
                requested_height: 240,
                border_width: 0,
                window_type: PolicyWindowType::Normal,
                map_intent: PolicyMapIntent::WantsMap,
                override_redirect: false,
                decoration_preference: 0,
                fullscreen_requested: false,
                maximized_requested: false,
                minimized_requested: false,
                attention_requested: false,
                creation_serial: 1,
                map_serial: 1,
                focus_serial: 0,
                flags: 0,
            }),
            basic_capabilities(),
        )
        .unwrap();
        peer.dispatch(
            &envelope(MessageType::SNAPSHOT_END, 5),
            &encode_snapshot_end(SnapshotEnd {
                snapshot_id: SnapshotId::new(1),
                generation: Generation::new(1),
                actual_item_count: 2,
            }),
            basic_capabilities(),
        )
        .unwrap();
    }

    fn basic_capabilities() -> Capabilities {
        Capabilities::default()
            .with(Capabilities::SNAPSHOTS)
            .with(Capabilities::WINDOW_POLICY)
    }

    #[test]
    fn snapshot_commit_emits_canonical_policy_and_ack() {
        let mut peer = PeerPolicy::new();
        basic_snapshot(&mut peer);
        let outcome = peer
            .dispatch(
                &envelope(MessageType::POLICY_COMMIT, 6),
                &encode_policy_commit(&PolicyCommit {
                    commit_id: 100,
                    producer_generation: 1,
                    flags: 0,
                }),
                basic_capabilities(),
            )
            .unwrap();
        assert!(outcome.accepted);
        assert_eq!(
            outcome
                .records
                .iter()
                .map(|record| record.message_type)
                .collect::<Vec<_>>(),
            [
                MessageType::SNAPSHOT_BEGIN,
                MessageType::POLICY_WINDOW_STATE,
                MessageType::SNAPSHOT_END,
                MessageType::POLICY_ACKNOWLEDGED,
            ]
        );
        let ack = gw_wire::decode_policy_acknowledged(&outcome.records[3].payload).unwrap();
        assert_eq!(ack.result, PolicyResult::Accepted);
        assert_ne!(ack.policy_hash, 0);
    }

    #[test]
    fn abort_restores_pre_snapshot_incremental_state() {
        let mut peer = PeerPolicy::new();
        basic_snapshot(&mut peer);
        let accepted = peer
            .dispatch(
                &envelope(MessageType::POLICY_COMMIT, 6),
                &encode_policy_commit(&PolicyCommit {
                    commit_id: 1,
                    producer_generation: 1,
                    flags: 0,
                }),
                basic_capabilities(),
            )
            .unwrap();
        let original = gw_wire::decode_policy_acknowledged(&accepted.records[3].payload)
            .unwrap()
            .policy_hash;
        peer.dispatch(
            &envelope(MessageType::SNAPSHOT_BEGIN, 7),
            &encode_snapshot_begin(SnapshotBegin {
                snapshot_id: SnapshotId::new(2),
                domain: SnapshotDomain::WindowPolicy,
                generation: Generation::new(2),
                expected_item_count: 0,
                flags: 0,
            }),
            basic_capabilities(),
        )
        .unwrap();
        peer.dispatch(
            &envelope(MessageType::SNAPSHOT_ABORT, 8),
            &gw_wire::encode_snapshot_abort(&SnapshotAbort {
                snapshot_id: SnapshotId::new(2),
                reason: 1,
                detail: "test".to_owned(),
            })
            .unwrap(),
            basic_capabilities(),
        )
        .unwrap();
        let replay = peer
            .dispatch(
                &envelope(MessageType::POLICY_COMMIT, 9),
                &encode_policy_commit(&PolicyCommit {
                    commit_id: 2,
                    producer_generation: 1,
                    flags: 0,
                }),
                basic_capabilities(),
            )
            .unwrap();
        assert_eq!(
            gw_wire::decode_policy_acknowledged(&replay.records[3].payload)
                .unwrap()
                .policy_hash,
            original
        );
    }
}
