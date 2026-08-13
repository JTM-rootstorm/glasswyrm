use gw_types::{CommitId, Generation, OutputId};
use gwcomp_core::{
    BackendEvent, CapabilityOrigin, CommitDisposition, CommitRequest, HeadlessBackend,
    OutputBackend, OutputCommitResult, PresentDisposition, PresentRequest, PresentResult,
    RecordedMutation, RejectionReason, ReplayLedger, ScriptedMockBackend, ValidationResult,
    VrrEligibility, VrrPlan, VrrPolicy, VrrPolicyReason, VrrWindowPreference, plan_vrr,
};

const OUTPUT: OutputId = OutputId::new(7);

fn eligible() -> VrrEligibility {
    VrrEligibility {
        output_enabled: true,
        visible: true,
        managed: true,
        focused: true,
        fullscreen: true,
        borderless_fullscreen: false,
        exclusive_output_membership: true,
        preference: VrrWindowPreference::Default,
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
    let unsupported_plan = plan_vrr(unsupported_capability, VrrPolicy::Fullscreen, eligible());
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
        plan_vrr(simulated_capability, VrrPolicy::Fullscreen, eligible()),
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
fn vrr_policy_matches_the_five_m14_modes() {
    let backend = HeadlessBackend::simulated_vrr(OUTPUT);
    let capabilities = backend.read_capabilities(OUTPUT).unwrap();
    let facts = eligible();

    assert_eq!(
        plan_vrr(capabilities, VrrPolicy::Off, facts).reason,
        VrrPolicyReason::PolicyOff
    );

    let ordinary_focused = VrrEligibility {
        fullscreen: false,
        ..facts
    };
    assert_eq!(
        plan_vrr(capabilities, VrrPolicy::Fullscreen, ordinary_focused).reason,
        VrrPolicyReason::Ineligible
    );
    assert!(
        plan_vrr(
            capabilities,
            VrrPolicy::Fullscreen,
            VrrEligibility {
                borderless_fullscreen: true,
                ..ordinary_focused
            },
        )
        .desired_enabled
    );
    assert!(plan_vrr(capabilities, VrrPolicy::Focused, ordinary_focused).desired_enabled);

    assert_eq!(
        plan_vrr(capabilities, VrrPolicy::AppRequested, facts).reason,
        VrrPolicyReason::Ineligible
    );
    assert!(
        plan_vrr(
            capabilities,
            VrrPolicy::AppRequested,
            VrrEligibility {
                preference: VrrWindowPreference::Prefer,
                ..facts
            },
        )
        .desired_enabled
    );

    let no_candidate = VrrEligibility {
        output_enabled: true,
        visible: false,
        managed: false,
        focused: false,
        fullscreen: false,
        borderless_fullscreen: false,
        exclusive_output_membership: false,
        preference: VrrWindowPreference::Disable,
    };
    assert!(plan_vrr(capabilities, VrrPolicy::AlwaysEligible, no_candidate,).desired_enabled);
    assert_eq!(
        plan_vrr(
            capabilities,
            VrrPolicy::AlwaysEligible,
            VrrEligibility {
                output_enabled: false,
                ..no_candidate
            },
        )
        .reason,
        VrrPolicyReason::Ineligible
    );
}

#[test]
fn candidate_modes_require_every_common_m14_fact_and_honor_disable() {
    let backend = HeadlessBackend::simulated_vrr(OUTPUT);
    let capabilities = backend.read_capabilities(OUTPUT).unwrap();
    let facts = eligible();
    let ineligible = [
        VrrEligibility {
            output_enabled: false,
            ..facts
        },
        VrrEligibility {
            visible: false,
            ..facts
        },
        VrrEligibility {
            managed: false,
            ..facts
        },
        VrrEligibility {
            focused: false,
            ..facts
        },
        VrrEligibility {
            exclusive_output_membership: false,
            ..facts
        },
        VrrEligibility {
            preference: VrrWindowPreference::Disable,
            ..facts
        },
    ];

    for facts in ineligible {
        for policy in [VrrPolicy::Fullscreen, VrrPolicy::Focused] {
            assert_eq!(
                plan_vrr(capabilities, policy, facts).reason,
                VrrPolicyReason::Ineligible
            );
        }
    }

    for preference in [
        VrrWindowPreference::Default,
        VrrWindowPreference::Disable,
        VrrWindowPreference::Allow,
    ] {
        assert_eq!(
            plan_vrr(
                capabilities,
                VrrPolicy::AppRequested,
                VrrEligibility {
                    preference,
                    ..facts
                },
            )
            .reason,
            VrrPolicyReason::Ineligible
        );
    }
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
    assert_eq!(restart.lost_peer_epoch.get(), 1);
    assert_eq!(restart.replacement_peer_epoch.get(), 2);
    assert_eq!(restart.rejected_queued.len(), 1);
    assert_eq!(restart.rejected_queued[0].request, queued);
    assert_eq!(restart.rejected_queued[0].peer_epoch.get(), 1);
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
fn completed_queued_work_is_removed_before_restart() {
    let policy_off = VrrPlan {
        desired_enabled: false,
        reason: VrrPolicyReason::PolicyOff,
    };
    let accepted = commit(22, 1, policy_off);
    let rejected = commit(23, 2, policy_off);
    let mut ledger = ReplayLedger::default();
    let epoch = ledger.queue(accepted);
    assert_eq!(ledger.queue(rejected), epoch);
    assert_eq!(ledger.queued_len(), 2);

    assert_eq!(
        ledger.accept_queued(epoch, accepted.commit_id, Generation::new(2)),
        Some(accepted)
    );
    assert_eq!(ledger.generation(), Generation::new(2));
    assert_eq!(ledger.queued_len(), 1);
    let rejection = ledger
        .reject_queued(
            epoch,
            rejected.commit_id,
            RejectionReason::AtomicCommitRejected,
            Generation::new(2),
        )
        .unwrap();
    assert_eq!(rejection.request, rejected);
    assert_eq!(rejection.peer_epoch, epoch);
    assert_eq!(ledger.generation(), Generation::new(2));
    assert_eq!(ledger.queued_len(), 0);

    let restart = ledger.restart();
    assert_eq!(restart.replay, Some(accepted));
    assert!(restart.rejected_queued.is_empty());
}

#[test]
fn stale_peer_completion_cannot_complete_replacement_peer_work() {
    let policy_off = VrrPlan {
        desired_enabled: false,
        reason: VrrPolicyReason::PolicyOff,
    };
    let lost = commit(24, 1, policy_off);
    let replacement = commit(25, 1, policy_off);
    let mut ledger = ReplayLedger::default();
    let lost_epoch = ledger.queue(lost);
    let restart = ledger.restart();
    let replacement_epoch = ledger.queue(replacement);

    assert_eq!(restart.lost_peer_epoch, lost_epoch);
    assert_eq!(restart.replacement_peer_epoch, replacement_epoch);
    assert_eq!(
        ledger.accept_queued(lost_epoch, replacement.commit_id, Generation::new(2)),
        None
    );
    assert_eq!(ledger.generation(), Generation::default());
    assert_eq!(ledger.queued_len(), 1);
    assert_eq!(
        ledger.accept_queued(replacement_epoch, replacement.commit_id, Generation::new(2),),
        Some(replacement)
    );
    assert_eq!(ledger.queued_len(), 0);
}

#[test]
fn accepted_headless_presentation_is_labeled_software_only() {
    let mut backend = HeadlessBackend::simulated_vrr(OUTPUT);
    let capabilities = backend.read_capabilities(OUTPUT).unwrap();
    let request = commit(
        30,
        1,
        plan_vrr(capabilities, VrrPolicy::Fullscreen, eligible()),
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
