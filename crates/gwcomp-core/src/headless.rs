use std::collections::BTreeMap;

use gw_types::{Generation, OutputId};

use crate::{
    BackendCapabilities, BackendError, BackendEvent, CapabilityOrigin, CommitDisposition,
    CommitRequest, OutputBackend, OutputCommitResult, OutputInfo, OutputMode, PresentDisposition,
    PresentRequest, PresentResult, RejectionReason, ValidationResult, VrrCapability,
    VrrObservation,
};

/// A synchronous output backend for pure compositor tests.
#[derive(Clone, Debug)]
pub struct HeadlessBackend {
    outputs: BTreeMap<OutputId, OutputInfo>,
    capabilities: BTreeMap<OutputId, BackendCapabilities>,
    next_token: u64,
}

impl HeadlessBackend {
    #[must_use]
    pub fn unsupported_vrr(output_id: OutputId) -> Self {
        Self::one_output(output_id, VrrCapability::unsupported())
    }

    #[must_use]
    pub fn simulated_vrr(output_id: OutputId) -> Self {
        Self::one_output(
            output_id,
            VrrCapability {
                property_present: true,
                hardware_capable: false,
                atomic_kms_available: true,
                atomic_test_passed: true,
                kms_controllable: true,
                simulated: true,
                minimum_refresh_millihertz: 40_000,
                maximum_refresh_millihertz: 60_000,
            },
        )
    }

    fn one_output(output_id: OutputId, vrr: VrrCapability) -> Self {
        let output = OutputInfo {
            id: output_id,
            name: "HEADLESS-1".to_owned(),
            connected: true,
            enabled: true,
            generation: Generation::new(1),
            mode: OutputMode {
                width: 1280,
                height: 720,
                refresh_millihertz: 60_000,
            },
        };
        Self {
            outputs: BTreeMap::from([(output_id, output)]),
            capabilities: BTreeMap::from([(
                output_id,
                BackendCapabilities {
                    origin: CapabilityOrigin::HeadlessSimulation,
                    vrr,
                },
            )]),
            next_token: 1,
        }
    }

    fn validate(&self, request: &CommitRequest) -> ValidationResult {
        let Some(output) = self.outputs.get(&request.output_id) else {
            return ValidationResult::Rejected(RejectionReason::UnknownOutput);
        };
        if request.base_generation != output.generation {
            return ValidationResult::Rejected(RejectionReason::StaleGeneration {
                expected: request.base_generation,
                actual: output.generation,
            });
        }
        if request.vrr.desired_enabled {
            let capability = self.capabilities[&request.output_id].vrr;
            if !capability.property_present {
                return ValidationResult::Rejected(RejectionReason::VrrPropertyUnavailable);
            }
            if !capability.controllable() {
                return ValidationResult::Rejected(RejectionReason::VrrUnsupported);
            }
        }
        ValidationResult::Accepted
    }
}

impl OutputBackend for HeadlessBackend {
    fn enumerate_outputs(&self) -> Vec<OutputInfo> {
        self.outputs.values().cloned().collect()
    }

    fn read_capabilities(&self, output_id: OutputId) -> Result<BackendCapabilities, BackendError> {
        self.capabilities
            .get(&output_id)
            .copied()
            .ok_or(BackendError::UnknownOutput(output_id))
    }

    fn validate_commit(&mut self, request: &CommitRequest) -> ValidationResult {
        self.validate(request)
    }

    fn commit(&mut self, request: &CommitRequest) -> OutputCommitResult {
        if let ValidationResult::Rejected(reason) = self.validate(request) {
            let retained_generation = self
                .outputs
                .get(&request.output_id)
                .map_or(Generation::default(), |output| output.generation);
            return OutputCommitResult::Rejected {
                reason,
                retained_generation,
            };
        }
        let output = self
            .outputs
            .get_mut(&request.output_id)
            .expect("validated output");
        output.generation = Generation::new(output.generation.get() + 1);
        OutputCommitResult::Accepted {
            disposition: CommitDisposition::Complete,
            applied_generation: output.generation,
        }
    }

    fn present(&mut self, request: &PresentRequest) -> PresentResult {
        let Some(output) = self.outputs.get(&request.output_id) else {
            return PresentResult::Rejected(RejectionReason::UnknownOutput);
        };
        if request.generation != output.generation {
            return PresentResult::Rejected(RejectionReason::StaleGeneration {
                expected: request.generation,
                actual: output.generation,
            });
        }
        let capability = self.capabilities[&request.output_id];
        let effective_enabled = request.desired_vrr_enabled && capability.vrr.controllable();
        let token = self.next_token;
        self.next_token += 1;
        PresentResult::Accepted {
            disposition: PresentDisposition::Complete,
            token,
            vrr: VrrObservation {
                desired_enabled: request.desired_vrr_enabled,
                effective_enabled,
                origin: capability.origin,
            },
        }
    }

    fn poll_event(&mut self) -> Option<BackendEvent> {
        None
    }
}
