use std::collections::{BTreeMap, VecDeque};

use gw_types::{Generation, OutputId};

use crate::{
    BackendCapabilities, BackendError, BackendEvent, CapabilityOrigin, CommitDisposition,
    CommitRequest, OutputBackend, OutputCommitResult, OutputInfo, OutputMode, PresentDisposition,
    PresentRequest, PresentResult, RejectionReason, ValidationResult, VrrCapability,
    VrrObservation,
};

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum RecordedMutation {
    RemoveVrrProperty(OutputId),
    ChangeGeneration {
        output_id: OutputId,
        generation: Generation,
    },
}

/// A deterministic recorded-state backend for rejection and replay tests.
#[derive(Clone, Debug)]
pub struct ScriptedMockBackend {
    outputs: BTreeMap<OutputId, OutputInfo>,
    capabilities: BTreeMap<OutputId, BackendCapabilities>,
    validations: VecDeque<ValidationResult>,
    commits: VecDeque<OutputCommitResult>,
    presentations: VecDeque<PresentResult>,
    events: VecDeque<(Option<RecordedMutation>, BackendEvent)>,
    next_token: u64,
}

impl ScriptedMockBackend {
    #[must_use]
    pub fn unsupported_vrr(output_id: OutputId) -> Self {
        Self::one_output(output_id, VrrCapability::unsupported())
    }

    #[must_use]
    pub fn supported_vrr(output_id: OutputId) -> Self {
        Self::one_output(
            output_id,
            VrrCapability {
                property_present: true,
                hardware_capable: true,
                atomic_kms_available: true,
                atomic_test_passed: true,
                kms_controllable: true,
                simulated: false,
                minimum_refresh_millihertz: 48_000,
                maximum_refresh_millihertz: 144_000,
            },
        )
    }

    fn one_output(output_id: OutputId, vrr: VrrCapability) -> Self {
        Self {
            outputs: BTreeMap::from([(
                output_id,
                OutputInfo {
                    id: output_id,
                    name: "RECORDED-DP-1".to_owned(),
                    connected: true,
                    enabled: true,
                    generation: Generation::new(1),
                    mode: OutputMode {
                        width: 2560,
                        height: 1440,
                        refresh_millihertz: 144_000,
                    },
                },
            )]),
            capabilities: BTreeMap::from([(
                output_id,
                BackendCapabilities {
                    origin: CapabilityOrigin::RecordedFixture,
                    vrr,
                },
            )]),
            validations: VecDeque::new(),
            commits: VecDeque::new(),
            presentations: VecDeque::new(),
            events: VecDeque::new(),
            next_token: 1,
        }
    }

    pub fn script_validation(&mut self, result: ValidationResult) {
        self.validations.push_back(result);
    }

    pub fn script_commit(&mut self, result: OutputCommitResult) {
        self.commits.push_back(result);
    }

    pub fn script_presentation(&mut self, result: PresentResult) {
        self.presentations.push_back(result);
    }

    pub fn script_event(&mut self, mutation: Option<RecordedMutation>, event: BackendEvent) {
        self.events.push_back((mutation, event));
    }

    fn validate_from_state(&self, request: &CommitRequest) -> ValidationResult {
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

    fn apply_mutation(&mut self, mutation: RecordedMutation) {
        match mutation {
            RecordedMutation::RemoveVrrProperty(output_id) => {
                if let Some(capabilities) = self.capabilities.get_mut(&output_id) {
                    capabilities.vrr.property_present = false;
                    capabilities.vrr.kms_controllable = false;
                }
            }
            RecordedMutation::ChangeGeneration {
                output_id,
                generation,
            } => {
                if let Some(output) = self.outputs.get_mut(&output_id) {
                    output.generation = generation;
                }
            }
        }
    }
}

impl OutputBackend for ScriptedMockBackend {
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
        self.validations
            .pop_front()
            .unwrap_or_else(|| self.validate_from_state(request))
    }

    fn commit(&mut self, request: &CommitRequest) -> OutputCommitResult {
        if let Some(result) = self.commits.pop_front() {
            if let OutputCommitResult::Accepted {
                applied_generation, ..
            } = result
                && let Some(output) = self.outputs.get_mut(&request.output_id)
            {
                output.generation = applied_generation;
            }
            return result;
        }
        if let ValidationResult::Rejected(reason) = self.validate_from_state(request) {
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
        if let Some(result) = self.presentations.pop_front() {
            return result;
        }
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
        let token = self.next_token;
        self.next_token += 1;
        PresentResult::Accepted {
            disposition: PresentDisposition::Complete,
            token,
            vrr: VrrObservation {
                desired_enabled: request.desired_vrr_enabled,
                effective_enabled: request.desired_vrr_enabled && capability.vrr.controllable(),
                origin: capability.origin,
            },
        }
    }

    fn poll_event(&mut self) -> Option<BackendEvent> {
        let (mutation, event) = self.events.pop_front()?;
        if let Some(mutation) = mutation {
            self.apply_mutation(mutation);
        }
        Some(event)
    }
}
