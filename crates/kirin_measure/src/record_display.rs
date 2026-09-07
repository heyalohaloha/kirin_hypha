//! Keep世代に結び付いたRecord表示スナップショット。
//!
//! 計測・TRACE・plugin_dataの正本とは独立したread-only presentation cache。
//! Measure/IO Threadが同じ`RecordStateMachine`経由で更新し、GUIは世代付きの
//! 1スナップショットだけを読む。Audio Threadからは呼ばない。

use crate::{DeltaMode, DeltaResult, DeltaSnapshot, MeasureResult, SessionSummary};
use std::sync::Mutex;

#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum RecordDisplayStatus {
    #[default]
    Empty,
    Live,
    Finalizing,
    Finalized,
    Unavailable,
    Dismissed,
}

#[derive(Debug, Clone, Default)]
pub struct RecordDisplaySnapshot {
    pub generation: u64,
    pub status: RecordDisplayStatus,
    pub measure: Option<MeasureResult>,
    pub summary: Option<SessionSummary>,
    pub delta: Option<DeltaResult>,
    /// このKeep世代のPOST差分を作ったPRE。表示整合性の照合だけに使う。
    pub pair_pre_instance_id: Option<String>,
    pub measure_started: bool,
}

#[derive(Debug, Default)]
pub struct RecordDisplayStore {
    inner: Mutex<RecordDisplaySnapshot>,
}

impl RecordDisplayStore {
    pub fn begin(&self, generation: u64) {
        if generation == 0 {
            return;
        }
        let mut state = self.lock();
        if state.generation == generation
            && matches!(
                state.status,
                RecordDisplayStatus::Live | RecordDisplayStatus::Finalizing
            )
        {
            return;
        }
        *state = RecordDisplaySnapshot {
            generation,
            status: RecordDisplayStatus::Live,
            ..RecordDisplaySnapshot::default()
        };
    }

    pub fn mark_measure_started(&self, generation: u64) {
        let mut state = self.lock();
        if state.generation != generation {
            return;
        }
        if state.status == RecordDisplayStatus::Live {
            state.measure_started = true;
        } else if state.status == RecordDisplayStatus::Finalized
            && !state.measure_started
            && state.measure.is_none()
            && state.summary.is_none()
        {
            // Measure ThreadがRecordを観測した直後にStopと競合した場合、Stop側はまだ
            // `measure_started`を見られず空結果へ閉じることがある。その同じ世代を実際に
            // 引き受けたMeasure ThreadだけがFinalizingへ戻し、次loopのtight-drainで閉じる。
            state.measure_started = true;
            state.status = RecordDisplayStatus::Finalizing;
        }
    }

    /// Stop要求を表示状態へ反映する。
    ///
    /// Measure ThreadがRecord入口をまだ観測していなければ、集計対象は0 sampleなので
    /// 空の確定結果へ直ちに閉じる。観測済みならtight-drain完了までFinalizingを維持する。
    pub fn request_stop(&self, generation: u64) {
        let mut state = self.lock();
        if state.generation != generation || state.status != RecordDisplayStatus::Live {
            return;
        }
        state.status = if state.measure_started {
            RecordDisplayStatus::Finalizing
        } else {
            RecordDisplayStatus::Finalized
        };
    }

    pub fn publish_measure(
        &self,
        generation: u64,
        measure: MeasureResult,
        summary: SessionSummary,
    ) {
        let mut state = self.lock();
        if state.generation != generation
            || !matches!(
                state.status,
                RecordDisplayStatus::Live | RecordDisplayStatus::Finalizing
            )
        {
            return;
        }
        state.measure = Some(measure);
        state.summary = Some(summary);
    }

    pub fn publish_delta(
        &self,
        generation: u64,
        delta: DeltaResult,
        pair_pre_instance_id: Option<String>,
    ) {
        let mut state = self.lock();
        if state.generation != generation
            || !state.measure_started
            || !matches!(
                state.status,
                RecordDisplayStatus::Live | RecordDisplayStatus::Finalizing
            )
        {
            return;
        }
        if state.pair_pre_instance_id.is_none() {
            state.pair_pre_instance_id =
                pair_pre_instance_id.filter(|instance_id| !instance_id.is_empty());
        }
        state.delta = Some(materialize_last_active(delta));
    }

    pub fn finalize(&self, generation: u64, summary: Option<SessionSummary>) {
        let mut state = self.lock();
        if state.generation != generation
            || !matches!(
                state.status,
                RecordDisplayStatus::Live | RecordDisplayStatus::Finalizing
            )
        {
            return;
        }
        state.summary = summary;
        state.status = RecordDisplayStatus::Finalized;
    }

    pub fn mark_unavailable(&self, generation: u64) {
        let mut state = self.lock();
        if state.generation != generation
            || !matches!(
                state.status,
                RecordDisplayStatus::Live | RecordDisplayStatus::Finalizing
            )
        {
            return;
        }
        // Live I must never be presented as a final result after a failed drain.
        state.summary = None;
        state.status = RecordDisplayStatus::Unavailable;
    }

    /// 最終確定後に最初の実測Watch結果が届いた時だけ表示をWatchへ戻す。
    pub fn dismiss_on_watch_result(&self) {
        let mut state = self.lock();
        if matches!(
            state.status,
            RecordDisplayStatus::Finalized | RecordDisplayStatus::Unavailable
        ) {
            state.status = RecordDisplayStatus::Dismissed;
        }
    }

    #[cfg(test)]
    pub fn snapshot(&self) -> RecordDisplaySnapshot {
        self.lock().clone()
    }

    pub fn try_snapshot(&self) -> Option<RecordDisplaySnapshot> {
        self.inner.try_lock().ok().map(|state| state.clone())
    }

    fn lock(&self) -> std::sync::MutexGuard<'_, RecordDisplaySnapshot> {
        match self.inner.lock() {
            Ok(guard) => guard,
            Err(poisoned) => poisoned.into_inner(),
        }
    }
}

fn materialize_last_active(delta: DeltaResult) -> DeltaResult {
    if delta.mode != DeltaMode::Stale {
        return delta;
    }
    let Some(snapshot) = delta.last_active.clone() else {
        return delta;
    };
    delta_from_snapshot(snapshot, delta.mode)
}

fn delta_from_snapshot(snapshot: DeltaSnapshot, mode: DeltaMode) -> DeltaResult {
    DeltaResult {
        lufs: snapshot.lufs,
        lufs_s: snapshot.lufs_s,
        psr: snapshot.psr,
        tp: snapshot.tp,
        n_prime_total: snapshot.n_prime_total,
        crest: snapshot.crest,
        sharpness: snapshot.sharpness,
        psb_bark: snapshot.psb_bark,
        mode,
        last_active: Some(snapshot),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn summary(i: f64) -> SessionSummary {
        SessionSummary {
            lufs_i: Some(i),
            lra: Some(4.0),
            max_true_peak: Some(-0.5),
        }
    }

    #[test]
    fn successful_generation_is_live_finalized_held_then_dismissed() {
        let store = RecordDisplayStore::default();
        store.begin(7);
        store.mark_measure_started(7);
        store.publish_measure(
            7,
            MeasureResult {
                lufs_m: Some(-14.0),
                computed: true,
                ..MeasureResult::default()
            },
            summary(-14.5),
        );
        store.request_stop(7);
        assert_eq!(store.snapshot().status, RecordDisplayStatus::Finalizing);
        store.finalize(7, Some(summary(-14.2)));
        let held = store.snapshot();
        assert_eq!(held.status, RecordDisplayStatus::Finalized);
        assert_eq!(held.summary.and_then(|value| value.lufs_i), Some(-14.2));
        store.dismiss_on_watch_result();
        assert_eq!(store.snapshot().status, RecordDisplayStatus::Dismissed);
    }

    #[test]
    fn stop_before_measure_entry_finalizes_empty_without_hanging() {
        let store = RecordDisplayStore::default();
        store.begin(1);
        store.request_stop(1);
        let snapshot = store.snapshot();
        assert_eq!(snapshot.status, RecordDisplayStatus::Finalized);
        assert!(snapshot.measure.is_none());
        assert!(snapshot.summary.is_none());
    }

    #[test]
    fn measure_entry_racing_with_stop_reopens_empty_result_for_finalization() {
        let store = RecordDisplayStore::default();
        store.begin(4);
        store.request_stop(4);
        assert_eq!(store.snapshot().status, RecordDisplayStatus::Finalized);
        store.mark_measure_started(4);
        let snapshot = store.snapshot();
        assert_eq!(snapshot.status, RecordDisplayStatus::Finalizing);
        assert!(snapshot.measure_started);
    }

    #[test]
    fn stale_generation_callbacks_cannot_overwrite_next_keep() {
        let store = RecordDisplayStore::default();
        store.begin(2);
        store.begin(3);
        store.publish_measure(2, MeasureResult::default(), summary(-30.0));
        store.finalize(2, Some(summary(-30.0)));
        let snapshot = store.snapshot();
        assert_eq!(snapshot.generation, 3);
        assert_eq!(snapshot.status, RecordDisplayStatus::Live);
        assert!(snapshot.summary.is_none());
    }

    #[test]
    fn failed_finalize_discards_live_integrated_value() {
        let store = RecordDisplayStore::default();
        store.begin(4);
        store.mark_measure_started(4);
        store.publish_measure(4, MeasureResult::default(), summary(-12.0));
        store.request_stop(4);
        store.mark_unavailable(4);
        let snapshot = store.snapshot();
        assert_eq!(snapshot.status, RecordDisplayStatus::Unavailable);
        assert!(snapshot.summary.is_none());
    }

    #[test]
    fn stale_delta_is_materialized_from_last_active_for_result_hold() {
        let store = RecordDisplayStore::default();
        store.begin(5);
        store.mark_measure_started(5);
        store.publish_delta(
            5,
            DeltaResult {
                mode: DeltaMode::Stale,
                last_active: Some(DeltaSnapshot {
                    lufs: Some(1.0),
                    lufs_s: Some(2.0),
                    ..DeltaSnapshot::default()
                }),
                ..DeltaResult::default()
            },
            Some("pre-a".to_string()),
        );
        let snapshot = store.snapshot();
        assert_eq!(snapshot.pair_pre_instance_id.as_deref(), Some("pre-a"));
        let delta = snapshot.delta.expect("delta");
        assert_eq!(delta.mode, DeltaMode::Stale);
        assert_eq!(delta.lufs, Some(1.0));
        assert_eq!(delta.lufs_s, Some(2.0));
    }

    #[test]
    fn delta_before_measure_entry_cannot_contaminate_empty_keep() {
        let store = RecordDisplayStore::default();
        store.begin(6);
        store.publish_delta(
            6,
            DeltaResult {
                lufs: Some(1.0),
                lufs_s: Some(2.0),
                ..DeltaResult::default()
            },
            Some("pre-old".to_string()),
        );
        store.request_stop(6);

        let snapshot = store.snapshot();
        assert_eq!(snapshot.status, RecordDisplayStatus::Finalized);
        assert!(!snapshot.measure_started);
        assert!(snapshot.delta.is_none());
        assert!(snapshot.pair_pre_instance_id.is_none());
    }

    #[test]
    fn pair_identity_is_frozen_per_keep_generation() {
        let store = RecordDisplayStore::default();
        store.begin(8);
        store.mark_measure_started(8);
        store.publish_delta(8, DeltaResult::default(), Some("pre-original".to_string()));
        store.publish_delta(
            8,
            DeltaResult::default(),
            Some("pre-replacement".to_string()),
        );
        assert_eq!(
            store.snapshot().pair_pre_instance_id.as_deref(),
            Some("pre-original")
        );

        store.begin(9);
        assert!(store.snapshot().pair_pre_instance_id.is_none());
    }
}
