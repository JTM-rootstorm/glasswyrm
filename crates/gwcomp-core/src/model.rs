use core::fmt;

use gw_types::{CommitId, Generation, OutputId};

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct OutputMode {
    pub width: u32,
    pub height: u32,
    pub refresh_millihertz: u32,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct OutputInfo {
    pub id: OutputId,
    pub name: String,
    pub connected: bool,
    pub enabled: bool,
    pub generation: Generation,
    pub mode: OutputMode,
}

/// Provenance available to software-only migration tests.
///
/// Neither variant is physical acceptance evidence. A recorded fixture may
/// describe a hardware-capable connector, but replaying that description does
/// not observe a real page flip or panel.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum CapabilityOrigin {
    HeadlessSimulation,
    RecordedFixture,
}

impl CapabilityOrigin {
    #[must_use]
    pub const fn is_physical_hardware_evidence(self) -> bool {
        false
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct VrrCapability {
    pub property_present: bool,
    pub hardware_capable: bool,
    pub atomic_kms_available: bool,
    pub atomic_test_passed: bool,
    pub kms_controllable: bool,
    pub simulated: bool,
    pub minimum_refresh_millihertz: u32,
    pub maximum_refresh_millihertz: u32,
}

impl VrrCapability {
    #[must_use]
    pub const fn unsupported() -> Self {
        Self {
            property_present: true,
            hardware_capable: false,
            atomic_kms_available: true,
            atomic_test_passed: true,
            kms_controllable: true,
            simulated: false,
            minimum_refresh_millihertz: 0,
            maximum_refresh_millihertz: 0,
        }
    }

    #[must_use]
    pub const fn controllable(self) -> bool {
        self.property_present
            && self.atomic_kms_available
            && self.atomic_test_passed
            && self.kms_controllable
            && (self.hardware_capable || self.simulated)
            && self.minimum_refresh_millihertz > 0
            && self.minimum_refresh_millihertz < self.maximum_refresh_millihertz
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct BackendCapabilities {
    pub origin: CapabilityOrigin,
    pub vrr: VrrCapability,
}

impl BackendCapabilities {
    #[must_use]
    pub const fn provides_physical_hardware_evidence(self) -> bool {
        self.origin.is_physical_hardware_evidence()
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum VrrPolicy {
    Off,
    Fullscreen,
    Focused,
    AppRequested,
    AlwaysEligible,
}

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub enum VrrWindowPreference {
    #[default]
    Default,
    Disable,
    Allow,
    Prefer,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct VrrEligibility {
    pub output_enabled: bool,
    pub visible: bool,
    pub managed: bool,
    pub focused: bool,
    pub fullscreen: bool,
    pub borderless_fullscreen: bool,
    pub exclusive_output_membership: bool,
    pub preference: VrrWindowPreference,
}

impl VrrEligibility {
    #[must_use]
    pub const fn common_candidate(self) -> bool {
        self.output_enabled
            && self.visible
            && self.managed
            && self.focused
            && self.exclusive_output_membership
            && !matches!(self.preference, VrrWindowPreference::Disable)
    }

    #[must_use]
    pub const fn eligible_for(self, policy: VrrPolicy) -> bool {
        match policy {
            VrrPolicy::Off => false,
            VrrPolicy::Fullscreen => {
                self.common_candidate() && (self.fullscreen || self.borderless_fullscreen)
            }
            VrrPolicy::Focused => self.common_candidate(),
            VrrPolicy::AppRequested => {
                self.common_candidate() && matches!(self.preference, VrrWindowPreference::Prefer)
            }
            VrrPolicy::AlwaysEligible => self.output_enabled,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum VrrPolicyReason {
    PolicyOff,
    Ineligible,
    Unsupported,
    PropertyUnavailable,
    EnabledByPolicy,
}

/// Desired backend state, not an assertion about physical panel behavior.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct VrrPlan {
    pub desired_enabled: bool,
    pub reason: VrrPolicyReason,
}

#[must_use]
pub const fn plan_vrr(
    capabilities: BackendCapabilities,
    policy: VrrPolicy,
    eligibility: VrrEligibility,
) -> VrrPlan {
    if matches!(policy, VrrPolicy::Off) {
        return VrrPlan {
            desired_enabled: false,
            reason: VrrPolicyReason::PolicyOff,
        };
    }
    if !capabilities.vrr.property_present {
        return VrrPlan {
            desired_enabled: false,
            reason: VrrPolicyReason::PropertyUnavailable,
        };
    }
    if !capabilities.vrr.controllable() {
        return VrrPlan {
            desired_enabled: false,
            reason: VrrPolicyReason::Unsupported,
        };
    }
    if !eligibility.eligible_for(policy) {
        return VrrPlan {
            desired_enabled: false,
            reason: VrrPolicyReason::Ineligible,
        };
    }
    VrrPlan {
        desired_enabled: true,
        reason: VrrPolicyReason::EnabledByPolicy,
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct CommitRequest {
    pub commit_id: CommitId,
    pub output_id: OutputId,
    pub base_generation: Generation,
    pub vrr: VrrPlan,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum RejectionReason {
    UnknownOutput,
    StaleGeneration {
        expected: Generation,
        actual: Generation,
    },
    VrrUnsupported,
    VrrPropertyUnavailable,
    AtomicTestRejected,
    AtomicCommitRejected,
    PeerRestarted,
    Scripted(String),
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum ValidationResult {
    Accepted,
    Rejected(RejectionReason),
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum CommitDisposition {
    Complete,
    Pending,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum OutputCommitResult {
    Accepted {
        disposition: CommitDisposition,
        applied_generation: Generation,
    },
    Rejected {
        reason: RejectionReason,
        retained_generation: Generation,
    },
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct PresentRequest {
    pub commit_id: CommitId,
    pub output_id: OutputId,
    pub generation: Generation,
    pub desired_vrr_enabled: bool,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum PresentDisposition {
    Complete,
    Pending,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct VrrObservation {
    pub desired_enabled: bool,
    pub effective_enabled: bool,
    pub origin: CapabilityOrigin,
}

impl VrrObservation {
    #[must_use]
    pub const fn provides_physical_hardware_evidence(self) -> bool {
        self.origin.is_physical_hardware_evidence()
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum PresentResult {
    Accepted {
        disposition: PresentDisposition,
        token: u64,
        vrr: VrrObservation,
    },
    Rejected(RejectionReason),
    Fatal(String),
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum BackendEvent {
    PresentationComplete {
        token: u64,
        vrr: VrrObservation,
    },
    CapabilitiesChanged(OutputId),
    GenerationChanged {
        output_id: OutputId,
        generation: Generation,
    },
    Fatal(String),
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum BackendError {
    UnknownOutput(OutputId),
}

impl fmt::Display for BackendError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::UnknownOutput(output_id) => {
                write!(formatter, "unknown output {}", output_id.get())
            }
        }
    }
}

impl std::error::Error for BackendError {}
