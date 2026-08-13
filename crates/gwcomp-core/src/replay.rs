use std::collections::VecDeque;

use gw_types::Generation;

use crate::{CommitRequest, RejectionReason};

#[derive(Clone, Copy, Debug, Eq, Ord, PartialEq, PartialOrd)]
pub struct PeerEpoch(u64);

impl PeerEpoch {
    pub const INITIAL: Self = Self(1);

    #[must_use]
    pub const fn new(value: u64) -> Self {
        Self(value)
    }

    #[must_use]
    pub const fn get(self) -> u64 {
        self.0
    }

    fn next(self) -> Self {
        Self(
            self.0
                .checked_add(1)
                .expect("compositor peer epoch exhausted"),
        )
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
struct QueuedCommit {
    request: CommitRequest,
    peer_epoch: PeerEpoch,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct QueuedRejection {
    pub request: CommitRequest,
    pub peer_epoch: PeerEpoch,
    pub reason: RejectionReason,
    pub retained_generation: Generation,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct RestartReplay {
    pub replay: Option<CommitRequest>,
    pub rejected_queued: Vec<QueuedRejection>,
    pub retained_generation: Generation,
    pub lost_peer_epoch: PeerEpoch,
    pub replacement_peer_epoch: PeerEpoch,
}

/// Tracks accepted and in-flight output work across a backend restart.
///
/// The last accepted request is replayable. Work queued against the lost peer
/// is rejected coherently and must be submitted again after replay.
#[derive(Clone, Debug)]
pub struct ReplayLedger {
    committed: Option<CommitRequest>,
    queued: VecDeque<QueuedCommit>,
    generation: Generation,
    peer_epoch: PeerEpoch,
}

impl Default for ReplayLedger {
    fn default() -> Self {
        Self {
            committed: None,
            queued: VecDeque::new(),
            generation: Generation::default(),
            peer_epoch: PeerEpoch::INITIAL,
        }
    }
}

impl ReplayLedger {
    #[must_use]
    pub const fn generation(&self) -> Generation {
        self.generation
    }

    #[must_use]
    pub const fn peer_epoch(&self) -> PeerEpoch {
        self.peer_epoch
    }

    #[must_use]
    pub fn queued_len(&self) -> usize {
        self.queued.len()
    }

    pub fn record_accepted(&mut self, request: CommitRequest, applied_generation: Generation) {
        self.remove_queued(self.peer_epoch, request.commit_id);
        self.committed = Some(request);
        self.generation = applied_generation;
    }

    pub fn queue(&mut self, request: CommitRequest) -> PeerEpoch {
        self.queued
            .retain(|queued| queued.request.commit_id != request.commit_id);
        self.queued.push_back(QueuedCommit {
            request,
            peer_epoch: self.peer_epoch,
        });
        self.peer_epoch
    }

    pub fn accept_queued(
        &mut self,
        peer_epoch: PeerEpoch,
        commit_id: gw_types::CommitId,
        applied_generation: Generation,
    ) -> Option<CommitRequest> {
        let queued = self.remove_queued(peer_epoch, commit_id)?;
        self.committed = Some(queued.request);
        self.generation = applied_generation;
        Some(queued.request)
    }

    pub fn reject_queued(
        &mut self,
        peer_epoch: PeerEpoch,
        commit_id: gw_types::CommitId,
        reason: RejectionReason,
        retained_generation: Generation,
    ) -> Option<QueuedRejection> {
        let queued = self.remove_queued(peer_epoch, commit_id)?;
        self.generation = retained_generation;
        Some(QueuedRejection {
            request: queued.request,
            peer_epoch: queued.peer_epoch,
            reason,
            retained_generation,
        })
    }

    #[must_use]
    pub fn restart(&mut self) -> RestartReplay {
        let retained_generation = self.generation;
        let lost_peer_epoch = self.peer_epoch;
        self.peer_epoch = self.peer_epoch.next();
        let rejected_queued = self
            .queued
            .drain(..)
            .map(|queued| QueuedRejection {
                request: queued.request,
                peer_epoch: queued.peer_epoch,
                reason: RejectionReason::PeerRestarted,
                retained_generation,
            })
            .collect();
        RestartReplay {
            replay: self.committed,
            rejected_queued,
            retained_generation,
            lost_peer_epoch,
            replacement_peer_epoch: self.peer_epoch,
        }
    }

    fn remove_queued(
        &mut self,
        peer_epoch: PeerEpoch,
        commit_id: gw_types::CommitId,
    ) -> Option<QueuedCommit> {
        let index = self.queued.iter().position(|queued| {
            queued.peer_epoch == peer_epoch && queued.request.commit_id == commit_id
        })?;
        self.queued.remove(index)
    }
}
