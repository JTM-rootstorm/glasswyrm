use gw_types::{CommitId, Generation, OutputId};
use gwcomp_core::{
    BackendEvent, CapabilityOrigin, CommitDisposition, CommitRequest, HeadlessBackend,
    OutputBackend, OutputCommitResult, PresentDisposition, PresentRequest, PresentResult,
    RecordedMutation, RejectionReason, ReplayLedger, ScriptedMockBackend, ValidationResult,
    VrrEligibility, VrrPlan, VrrPolicy, VrrPolicyReason, plan_vrr,
};

const OUTPUT: OutputId = OutputId::new(7);

fn eligible() -> VrrEligibility {
    VrrEligibility {
        fullscreen: true,
        focused: true,
        single_visible_surface: true,
    }
}

fn commit(id: u64, base_generation: u64, vrr: VrrPlan) -> CommitRequest {
    CommitRequest {
        commit_id: CommitId::new(id),
        output_id: OUTPUT,
        base_generation: Generation::new(base_generation),
        vrr,
    }
}

#[test]
fn vrr_policy_uses_capability_but_never_claims_physical_acceptance() {
    let unsupported = HeadlessBackend::unsupported_vrr(OUTPUT);
    let unsupported_capability = unsupported.read_capabilities(OUTPUT).unwrap();
    let unsupported_plan = plan_vrr(unsupported_capability, VrrPolicy::Automatic, eligible());
    assert_eq!(
        unsupported_plan,
        VrrPlan {
            desired_enabled: false,
            reason: VrrPolicyReason::Unsupported,
        }
    );

    let simulated = HeadlessBackend::simulated_vrr(OUTPUT);
    let simulated_capability = simulated.read_capabilities(OUTPUT).unwrap();
    assert_eq!(
        plan_vrr(simulated_capability, VrrPolicy::Automatic, eligible()),
        VrrPlan {
            desired_enabled: true,
            reason: VrrPolicyReason::EnabledByPolicy,
        }
    );
    assert_eq!(
        simulated_capability.origin,
        CapabilityOrigin::HeadlessSimulation
    );
    assert!(!simulated_capability.provides_physical_hardware_evidence());

    let recorded = ScriptedMockBackend::supported_vrr(OUTPUT);
    let recorded_capability = recorded.read_capabilities(OUTPUT).unwrap();
    assert!(plan_vrr(recorded_capability, VrrPolicy::AlwaysEligible, eligible()).desired_enabled);
    assert_eq!(
        recorded_capability.origin,
        CapabilityOrigin::RecordedFixture
    );
    assert!(!recorded_capability.provides_physical_hardware_evidence());

    let recorded_unsupported = ScriptedMockBackend::unsupported_vrr(OUTPUT);
    assert_eq!(
        plan_vrr(
            recorded_unsupported.read_capabilities(OUTPUT).unwrap(),
            VrrPolicy::AlwaysEligible,
            eligible(),
        )
        .reason,
        VrrPolicyReason::Unsupported
    );
}

#[test]
fn recorded_atomic_rejection_retains_the_committed_generation() {
    let mut backend = ScriptedMockBackend::supported_vrr(OUTPUT);
    let capabilities = backend.read_capabilities(OUTPUT).unwrap();
    let request = commit(
        11,
        1,
        plan_vrr(capabilities, VrrPolicy::AlwaysEligible, eligible()),
    );
    backend.script_validation(ValidationResult::Rejected(
        RejectionReason::AtomicTestRejected,
    ));
    assert_eq!(
        backend.validate_commit(&request),
        ValidationResult::Rejected(RejectionReason::AtomicTestRejected)
    );

    backend.script_commit(OutputCommitResult::Rejected {
        reason: RejectionReason::AtomicCommitRejected,
        retained_generation: Generation::new(1),
    });
    assert_eq!(
        backend.commit(&request),
        OutputCommitResult::Rejected {
            reason: RejectionReason::AtomicCommitRejected,
            retained_generation: Generation::new(1),
        }
    );
    assert_eq!(
        backend.enumerate_outputs()[0].generation,
        Generation::new(1)
    );
}

#[test]
fn property_disappearance_changes_policy_and_rejects_stale_desire() {
    let mut backend = ScriptedMockBackend::supported_vrr(OUTPUT);
    let before = backend.read_capabilities(OUTPUT).unwrap();
    let desired = plan_vrr(before, VrrPolicy::AlwaysEligible, eligible());
    assert!(desired.desired_enabled);

    backend.script_event(
        Some(RecordedMutation::RemoveVrrProperty(OUTPUT)),
        BackendEvent::CapabilitiesChanged(OUTPUT),
    );
    assert_eq!(
        backend.poll_event(),
        Some(BackendEvent::CapabilitiesChanged(OUTPUT))
    );

    let after = backend.read_capabilities(OUTPUT).unwrap();
    assert_eq!(
        plan_vrr(after, VrrPolicy::AlwaysEligible, eligible()),
        VrrPlan {
            desired_enabled: false,
            reason: VrrPolicyReason::PropertyUnavailable,
        }
    );
    assert_eq!(
        backend.validate_commit(&commit(12, 1, desired)),
        ValidationResult::Rejected(RejectionReason::VrrPropertyUnavailable)
    );
}

#[test]
fn generation_change_rejects_a_commit_built_from_the_old_inventory() {
    let mut backend = ScriptedMockBackend::supported_vrr(OUTPUT);
    backend.script_event(
        Some(RecordedMutation::ChangeGeneration {
            output_id: OUTPUT,
            generation: Generation::new(2),
        }),
        BackendEvent::GenerationChanged {
            output_id: OUTPUT,
            generation: Generation::new(2),
        },
    );
    assert!(matches!(
        backend.poll_event(),
        Some(BackendEvent::GenerationChanged { generation, .. })
            if generation == Generation::new(2)
    ));

    let request = commit(
        13,
        1,
        VrrPlan {
            desired_enabled: false,
            reason: VrrPolicyReason::PolicyOff,
        },
    );
    assert_eq!(
        backend.validate_commit(&request),
        ValidationResult::Rejected(RejectionReason::StaleGeneration {
            expected: Generation::new(1),
            actual: Generation::new(2),
        })
    );
}

#[test]
fn restart_replays_only_committed_state_and_rejects_queued_work() {
    let policy_off = VrrPlan {
        desired_enabled: false,
        reason: VrrPolicyReason::PolicyOff,
    };
    let accepted = commit(20, 1, policy_off);
    let queued = commit(21, 2, policy_off);
    let mut ledger = ReplayLedger::default();
    ledger.record_accepted(accepted, Generation::new(2));
    ledger.queue(queued);

    let restart = ledger.restart();
    assert_eq!(restart.replay, Some(accepted));
    assert_eq!(restart.retained_generation, Generation::new(2));
    assert_eq!(restart.rejected_queued.len(), 1);
    assert_eq!(restart.rejected_queued[0].request, queued);
    assert_eq!(
        restart.rejected_queued[0].reason,
        RejectionReason::PeerRestarted
    );
    assert_eq!(
        restart.rejected_queued[0].retained_generation,
        Generation::new(2)
    );
    assert!(ledger.restart().rejected_queued.is_empty());

    let mut replacement = ScriptedMockBackend::supported_vrr(OUTPUT);
    assert_eq!(
        replacement.validate_commit(&restart.replay.unwrap()),
        ValidationResult::Accepted
    );
    assert!(matches!(
        replacement.commit(&accepted),
        OutputCommitResult::Accepted {
            applied_generation,
            ..
        } if applied_generation == Generation::new(2)
    ));
    assert_eq!(
        replacement.validate_commit(&queued),
        ValidationResult::Accepted
    );
}

#[test]
fn accepted_headless_presentation_is_labeled_software_only() {
    let mut backend = HeadlessBackend::simulated_vrr(OUTPUT);
    let capabilities = backend.read_capabilities(OUTPUT).unwrap();
    let request = commit(
        30,
        1,
        plan_vrr(capabilities, VrrPolicy::Automatic, eligible()),
    );
    assert_eq!(
        backend.validate_commit(&request),
        ValidationResult::Accepted
    );
    assert_eq!(
        backend.commit(&request),
        OutputCommitResult::Accepted {
            disposition: CommitDisposition::Complete,
            applied_generation: Generation::new(2),
        }
    );
    let presented = backend.present(&PresentRequest {
        commit_id: CommitId::new(30),
        output_id: OUTPUT,
        generation: Generation::new(2),
        desired_vrr_enabled: true,
    });
    let PresentResult::Accepted {
        disposition, vrr, ..
    } = presented
    else {
        panic!("headless presentation should complete");
    };
    assert_eq!(disposition, PresentDisposition::Complete);
    assert!(vrr.effective_enabled);
    assert!(!vrr.provides_physical_hardware_evidence());
}
