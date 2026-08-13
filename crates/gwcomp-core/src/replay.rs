use std::collections::VecDeque;

use gw_types::Generation;

use crate::{CommitRequest, RejectionReason};

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct QueuedRejection {
    pub request: CommitRequest,
    pub reason: RejectionReason,
    pub retained_generation: Generation,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct RestartReplay {
    pub replay: Option<CommitRequest>,
    pub rejected_queued: Vec<QueuedRejection>,
    pub retained_generation: Generation,
}

/// Tracks accepted and in-flight output work across a backend restart.
///
/// The last accepted request is replayable. Work queued against the lost peer
/// is rejected coherently and must be submitted again after replay.
#[derive(Clone, Debug, Default)]
pub struct ReplayLedger {
    committed: Option<CommitRequest>,
    queued: VecDeque<CommitRequest>,
    generation: Generation,
}

impl ReplayLedger {
    #[must_use]
    pub const fn generation(&self) -> Generation {
        self.generation
    }

    pub fn record_accepted(&mut self, request: CommitRequest, applied_generation: Generation) {
        self.committed = Some(request);
        self.generation = applied_generation;
    }

    pub fn queue(&mut self, request: CommitRequest) {
        self.queued.push_back(request);
    }

    #[must_use]
    pub fn restart(&mut self) -> RestartReplay {
        let retained_generation = self.generation;
        let rejected_queued = self
            .queued
            .drain(..)
            .map(|request| QueuedRejection {
                request,
                reason: RejectionReason::PeerRestarted,
                retained_generation,
            })
            .collect();
        RestartReplay {
            replay: self.committed,
            rejected_queued,
            retained_generation,
        }
    }
}
