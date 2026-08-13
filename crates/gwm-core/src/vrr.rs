use core::ops::{BitOr, BitOrAssign};

use gw_types::{OutputId, WindowId};

use crate::{AppliedState, DecorationPreference, PolicyState, RawState};

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub enum VrrWindowPreference {
    #[default]
    Default,
    Disable,
    Allow,
    Prefer,
}

#[derive(Clone, Debug, Default, Eq, PartialEq)]
pub struct VrrWindowInput {
    pub window_id: WindowId,
    pub preference: VrrWindowPreference,
    pub output_membership: Vec<OutputId>,
}

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
#[repr(transparent)]
pub struct VrrReason(u64);

impl VrrReason {
    pub const WINDOW_HIDDEN: Self = Self(1 << 13);
    pub const WINDOW_UNMANAGED: Self = Self(1 << 14);
    pub const WINDOW_UNFOCUSED: Self = Self(1 << 15);
    pub const WINDOW_SPANS_OUTPUTS: Self = Self(1 << 18);
    pub const WINDOW_PREFERENCE_DISABLED: Self = Self(1 << 19);
    pub const SURFACE_MEMBERSHIP_INVALID: Self = Self(1 << 26);

    #[must_use]
    pub const fn bits(self) -> u64 {
        self.0
    }

    #[must_use]
    pub const fn is_empty(self) -> bool {
        self.0 == 0
    }

    #[must_use]
    pub const fn contains(self, reason: Self) -> bool {
        self.0 & reason.0 == reason.0
    }
}

impl BitOr for VrrReason {
    type Output = Self;

    fn bitor(self, right: Self) -> Self::Output {
        Self(self.0 | right.0)
    }
}

impl BitOrAssign for VrrReason {
    fn bitor_assign(&mut self, right: Self) {
        self.0 |= right.0;
    }
}

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct VrrWindowClassification {
    pub window_id: WindowId,
    pub output_id: OutputId,
    pub preference: VrrWindowPreference,
    pub visible: bool,
    pub focused: bool,
    pub fullscreen: bool,
    pub borderless_fullscreen: bool,
    pub exclusive_output_membership: bool,
    /// Whether the window satisfies the common, mode-independent candidate facts.
    pub common_candidate: bool,
    pub reason: VrrReason,
}

fn exclusive_membership(input: &VrrWindowInput, output_id: OutputId) -> bool {
    input.output_membership.as_slice() == [output_id]
}

/// Classifies exact undecorated output geometry without deciding compositor VRR policy.
#[must_use]
pub fn classify_borderless_fullscreen(
    raw: &RawState,
    policy: &PolicyState,
    input: &VrrWindowInput,
) -> bool {
    let Some(raw_window) = raw.windows.get(&input.window_id) else {
        return false;
    };
    let Some(window) = policy.windows.get(&input.window_id) else {
        return false;
    };
    let Some(output) = policy
        .outputs
        .get(&window.output_id)
        .filter(|output| output.enabled)
    else {
        return false;
    };
    let transient_on_other_output = raw_window.transient_for.is_some_and(|parent| {
        policy
            .windows
            .get(&parent)
            .is_none_or(|parent| parent.output_id != window.output_id)
    });
    raw_window.parent_window_id == raw.context.root_window_id
        && window.managed
        && !window.override_redirect
        && window.visible
        && window.focused
        && window.applied_state != AppliedState::Minimized
        && (raw_window.decoration_preference == DecorationPreference::False
            || !window.decoration_eligible)
        && raw_window.border_width == 0
        && window.geometry == output.logical
        && exclusive_membership(input, window.output_id)
        && !transient_on_other_output
}

/// Produces the GWM-owned factual inputs used later by compositor VRR policy.
#[must_use]
pub fn classify_vrr_window(
    raw: &RawState,
    policy: &PolicyState,
    input: &VrrWindowInput,
) -> Option<VrrWindowClassification> {
    let window = policy.windows.get(&input.window_id)?;
    let exclusive_output_membership = exclusive_membership(input, window.output_id);
    let mut reason = VrrReason::default();
    if !window.managed || window.override_redirect {
        reason |= VrrReason::WINDOW_UNMANAGED;
    }
    if !window.visible {
        reason |= VrrReason::WINDOW_HIDDEN;
    }
    if !window.focused {
        reason |= VrrReason::WINDOW_UNFOCUSED;
    }
    if input.output_membership.len() > 1 {
        reason |= VrrReason::WINDOW_SPANS_OUTPUTS;
    } else if !exclusive_output_membership {
        reason |= VrrReason::SURFACE_MEMBERSHIP_INVALID;
    }
    if input.preference == VrrWindowPreference::Disable {
        reason |= VrrReason::WINDOW_PREFERENCE_DISABLED;
    }
    let common_candidate = window.managed
        && !window.override_redirect
        && window.visible
        && window.focused
        && exclusive_output_membership;
    Some(VrrWindowClassification {
        window_id: input.window_id,
        output_id: window.output_id,
        preference: input.preference,
        visible: window.visible,
        focused: window.focused,
        fullscreen: window.applied_state == AppliedState::Fullscreen,
        borderless_fullscreen: classify_borderless_fullscreen(raw, policy, input),
        exclusive_output_membership,
        common_candidate,
        reason,
    })
}
