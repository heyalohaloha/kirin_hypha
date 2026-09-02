//! One ordered POST observation cycle over four explicit responsibility owners.

use std::time::Instant;

use super::analysis::PostAnalysisEndpoints;

#[path = "io_thread_post_observation_pair.rs"]
mod pair;
#[path = "io_thread_post_observation_runtime.rs"]
mod runtime;
#[path = "io_thread_post_observation_snapshot.rs"]
mod snapshot;

use pair::PostPairObservation;
use snapshot::PostSnapshotPublisher;

pub(super) use pair::PostPairObservationDeps;
pub(super) use runtime::PostObservationRuntime;

pub(super) struct PostObservationTick {
    pub(super) project_hash: String,
    pub(super) instance_id: String,
    pub(super) pair_pre_name: String,
}

pub(super) struct PostObservation {
    runtime: PostObservationRuntime,
    pair: PostPairObservation,
    snapshot: PostSnapshotPublisher,
    analysis: PostAnalysisEndpoints,
}

impl PostObservation {
    pub(super) fn new(
        runtime: PostObservationRuntime,
        pair: PostPairObservationDeps,
        analysis: PostAnalysisEndpoints,
        now: Instant,
    ) -> Self {
        let snapshot = PostSnapshotPublisher::new();
        runtime.prepare_startup(snapshot.kirin_root());
        Self {
            runtime,
            pair: PostPairObservation::new(pair, now),
            snapshot,
            analysis,
        }
    }

    pub(super) fn service(&mut self) -> PostObservationTick {
        self.service_at(Instant::now())
    }

    fn service_at(&mut self, cycle_now: Instant) -> PostObservationTick {
        let identity = self.runtime.identity_snapshot();
        self.pair
            .refresh_reservation(cycle_now, &self.runtime, &identity);
        let location = self.snapshot.prepare(&identity);
        let pair = self.pair.observe_binding(
            cycle_now,
            self.snapshot.kirin_root(),
            &self.runtime,
            &identity,
        );
        let snapshot_written = self.snapshot.publish(
            &location,
            &self.runtime,
            &identity,
            &pair,
            self.pair.latched_pre(),
        );
        self.analysis
            .service(self.pair.latched_pre(), &identity.instance_id, &pair.name);
        self.pair.publish_claim(
            self.snapshot.kirin_root(),
            &location.instance_dir,
            snapshot_written,
            &identity,
        );

        PostObservationTick {
            project_hash: identity.project_hash,
            instance_id: identity.instance_id,
            pair_pre_name: pair.name,
        }
    }
}

#[cfg(test)]
#[path = "io_thread_post_observation_tests.rs"]
mod tests;
