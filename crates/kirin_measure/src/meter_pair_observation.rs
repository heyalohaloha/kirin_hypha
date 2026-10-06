//! One producer for exact-pair history. Kept separate from transport and serialization.
use super::*;

#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
struct JoinedPoint {
    generation: u64,
    observed_frames: u64,
}

#[derive(Default)]
pub(super) struct DeltaHistoryState {
    pub(super) chain: chain_join::Admission,
    generation: u64,
    next_run_id: u64,
    current_source_run: Option<(u64, u64, u64, u64)>,
    last_joined_axis: Option<(u64, i64, u8)>,
    pair: Option<PairKey>,
    pub(super) history: MeterHistory,
    joined_order: VecDeque<JoinedPoint>,
    joined: HashSet<JoinedPoint>,
    consumed_pre_order: VecDeque<JoinedPoint>,
    consumed_pre: HashSet<JoinedPoint>,
}

impl DeltaHistoryState {
    pub(super) fn bind(&mut self, pair: PairKey) {
        if self.pair.as_ref() == Some(&pair) {
            return;
        }
        self.pair = Some(pair);
        self.discard();
    }

    pub(super) fn clear_pair(&mut self) {
        if self.pair.take().is_some() {
            self.discard();
        }
    }

    pub(super) fn clear_if_different_target(&mut self, target: &MeterHistoryTarget) {
        if self.pair.as_ref().is_some_and(|pair| {
            pair.instance_id != target.pre_instance_id
                || pair.instance_dir != target.instance_dir
                || pair.post_binding != target.post_binding
        }) {
            self.clear_pair();
        }
    }

    pub(super) fn reset(&mut self) {
        self.discard();
    }

    fn discard(&mut self) {
        self.generation = self.generation.wrapping_add(1).max(1);
        self.chain.clear(self.generation);
        self.next_run_id = 0;
        self.current_source_run = None;
        self.last_joined_axis = None;
        self.history.reset();
        self.joined_order.clear();
        self.joined.clear();
        self.consumed_pre_order.clear();
        self.consumed_pre.clear();
    }

    pub(super) fn ingest(
        &mut self,
        pre: &[WirePoint],
        post: &[MeterHistoryEntry],
        sample_rate: u32,
    ) {
        let available_pre = pre.iter().filter(|point| {
            !self.consumed_pre.contains(&JoinedPoint {
                generation: point.generation,
                observed_frames: point.observed_frames,
            })
        });
        let pre_counts = key_counts(available_pre.clone().map(wire_key));
        let post_counts = key_counts(
            post.iter()
                .filter(|point| {
                    !self.joined.contains(&JoinedPoint {
                        generation: point.generation,
                        observed_frames: point.last_observed_frames,
                    })
                })
                .filter_map(history_key),
        );
        let pre_by_key: HashMap<_, _> = available_pre
            .map(|point| (wire_key(point), point))
            .collect();

        for post_point in post {
            let joined_id = JoinedPoint {
                generation: post_point.generation,
                observed_frames: post_point.last_observed_frames,
            };
            if self.joined.contains(&joined_id) {
                continue;
            }
            let Some(key) = history_key(post_point) else {
                continue;
            };
            if pre_counts.get(&key) != Some(&1) || post_counts.get(&key) != Some(&1) {
                continue;
            }
            let Some(pre_point) = pre_by_key.get(&key) else {
                continue;
            };
            if self
                .last_joined_axis
                .is_some_and(|(observed, _, _)| post_point.last_observed_frames <= observed)
            {
                continue;
            }
            let source_run = (
                pre_point.generation,
                pre_point.run_id,
                post_point.generation,
                post_point.run_id,
            );
            let expected_step = u64::from(sample_rate / 10).max(1);
            let continuous = self
                .last_joined_axis
                .is_some_and(|(observed, endpoint, source)| {
                    post_point.last_observed_frames.saturating_sub(observed) == expected_step
                        && post_point
                            .last_timeline_endpoint_samples
                            .is_some_and(|current| {
                                current.saturating_sub(endpoint) == expected_step as i64
                            })
                        && source == key.0
                });
            if self.current_source_run != Some(source_run) || !continuous {
                self.current_source_run = Some(source_run);
                self.next_run_id = self.next_run_id.wrapping_add(1).max(1);
            }
            let delta = MeasureResult {
                lufs_m: difference(post_point.lufs_m.mean, pre_point.lufs_m),
                lufs_s: difference(post_point.lufs_s.mean, pre_point.lufs_s),
                true_peak: difference(post_point.true_peak.mean, pre_point.true_peak),
                psr: difference(post_point.psr.mean, pre_point.psr),
                ..MeasureResult::default()
            };
            self.history.push(
                // Δ は POST の測定区間に属する。POST が別 layout / rate で作り直されたら、
                // その前後の Δ は同じ測定の続きではない。
                post_point.measurement_epoch,
                self.generation.max(1),
                self.next_run_id,
                post_point.last_observed_frames,
                (
                    post_point.last_timeline_endpoint_samples,
                    post_point.timeline_source,
                ),
                &delta,
                MeterHistoryAux {
                    correlation: difference(post_point.correlation.mean, pre_point.correlation),
                    clip_event_count: [0; crate::meter_history::METER_HISTORY_CHANNELS],
                },
            );
            self.last_joined_axis = Some((post_point.last_observed_frames, key.1, key.0));
            self.remember_joined(joined_id);
            self.remember_consumed_pre(JoinedPoint {
                generation: pre_point.generation,
                observed_frames: pre_point.observed_frames,
            });
        }
    }

    fn remember_joined(&mut self, point: JoinedPoint) {
        self.joined.insert(point);
        self.joined_order.push_back(point);
        while self.joined_order.len() > JOINED_POINT_CAPACITY {
            if let Some(oldest) = self.joined_order.pop_front() {
                self.joined.remove(&oldest);
            }
        }
    }

    fn remember_consumed_pre(&mut self, point: JoinedPoint) {
        self.consumed_pre.insert(point);
        self.consumed_pre_order.push_back(point);
        while self.consumed_pre_order.len() > JOINED_POINT_CAPACITY {
            if let Some(oldest) = self.consumed_pre_order.pop_front() {
                self.consumed_pre.remove(&oldest);
            }
        }
    }
}
