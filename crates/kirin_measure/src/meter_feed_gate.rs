//! Direct Meter Session input, bound to producer-side activity.
use super::*;

#[derive(Default)]
pub(super) struct WatchDrain {
    pub samples: usize,
    pub fed: bool,
}

#[allow(clippy::too_many_arguments)]
pub(super) fn drain_watch_into_raw_pre_roll(
    consumer: &mut MeasureSampleConsumer,
    tracker: &RecordTakeTracker,
    history: &mut RawPreRollHistory,
    scratch: &mut Vec<f64>,
    native_frames_total: &mut u64,
    consumed_samples: &mut u64,
    n_channels: usize,
    sample_rate: u32,
    chunk_limit_samples: usize,
    signal_state: &AtomicU8,
    meter_session: Option<&Arc<Mutex<MeterSession>>>,
    meter_session_publication: Option<&Arc<MeterSessionPublication>>,
    resume: &mut Option<WatchResume>,
    until: Option<u64>,
) -> WatchDrain {
    drain_watch_with_progress(
        consumer,
        tracker,
        history,
        scratch,
        native_frames_total,
        consumed_samples,
        n_channels,
        sample_rate,
        chunk_limit_samples,
        signal_state,
        meter_session,
        meter_session_publication,
        resume,
        until,
        |_| {},
    )
}

#[allow(clippy::too_many_arguments)]
pub(super) fn drain_watch_with_progress(
    consumer: &mut MeasureSampleConsumer,
    tracker: &RecordTakeTracker,
    history: &mut RawPreRollHistory,
    scratch: &mut Vec<f64>,
    native_frames_total: &mut u64,
    consumed_samples: &mut u64,
    n_channels: usize,
    sample_rate: u32,
    chunk_limit_samples: usize,
    signal_state: &AtomicU8,
    meter_session: Option<&Arc<Mutex<MeterSession>>>,
    meter_session_publication: Option<&Arc<MeterSessionPublication>>,
    resume: &mut Option<WatchResume>,
    until: Option<u64>,
    mut progressed: impl FnMut(usize),
) -> WatchDrain {
    let mut drained = WatchDrain::default();
    loop {
        resume_watch_index(resume, native_frames_total);
        let available = consumer
            .slots()
            .min(chunk_limit_samples)
            .min(until.map_or(usize::MAX, |cutover| {
                samples_until_cursor_limit(*consumed_samples, cutover)
            }))
            .min(watch_chunk_limit(resume, *native_frames_total, n_channels));
        let plan = capture_chunk_plan(
            tracker,
            *native_frames_total,
            available,
            n_channels,
            sample_rate,
        );
        if plan.sample_count == 0 {
            break;
        }
        scratch.clear();
        for _ in 0..plan.sample_count {
            let Ok(sample) = consumer.pop() else {
                break;
            };
            *consumed_samples = (*consumed_samples).saturating_add(1);
            scratch.push(sample as f64);
        }
        let frames = (scratch.len() / n_channels.max(1)) as u64;
        if frames == 0 {
            break;
        }
        let _ = history.append_f64(trusted_pre_roll_epoch(plan), *native_frames_total, scratch);
        drained.samples += scratch.len();
        drained.fed |= MeterFeed {
            tracker,
            session: meter_session,
            publication: meter_session_publication,
            n_channels,
            fallback_active: load_signal_state(signal_state) == SignalState::Active,
        }
        .feed(scratch, *native_frames_total, plan, 0);
        *native_frames_total = (*native_frames_total).saturating_add(frames);
        progressed(scratch.len());
    }
    drained
}

pub(super) fn feed_meter_session(
    meter_session: Option<&Arc<Mutex<MeterSession>>>,
    publication: Option<&Arc<MeterSessionPublication>>,
    direct_active_samples: &[f64],
    clock: MeterClockStart,
) -> bool {
    if direct_active_samples.is_empty() {
        return false;
    }
    if let Some(session) = meter_session {
        let mut session = crate::sync_recovery::lock_recover(session, "MeterSession active push");
        let accepted = session.push_active_at(direct_active_samples, clock);
        if let Some(publication) = publication {
            // Publish while the session mutation is still serialized. Otherwise an explicit
            // CLEAR can publish its cleared holds and then be overwritten by this older frame.
            publication.publish(session.snapshot());
        }
        accepted
    } else {
        false
    }
}

pub(super) fn pause_meter_session(
    meter_session: Option<&Arc<Mutex<MeterSession>>>,
    publication: Option<&Arc<MeterSessionPublication>>,
) {
    if let Some(session) = meter_session {
        let mut session = crate::sync_recovery::lock_recover(session, "MeterSession pause");
        session.pause();
        if let Some(publication) = publication {
            publication.publish(session.snapshot());
        }
    }
}

pub(super) struct MeterFeed<'a> {
    pub tracker: &'a RecordTakeTracker,
    pub session: Option<&'a Arc<Mutex<MeterSession>>>,
    pub publication: Option<&'a Arc<MeterSessionPublication>>,
    pub n_channels: usize,
    pub fallback_active: bool,
}

impl MeterFeed<'_> {
    pub(super) fn feed(
        &self,
        samples: &[f64],
        captured_before: u64,
        plan: capture_plan::CaptureChunkPlan,
        prefix_samples: usize,
    ) -> bool {
        let channels = self.n_channels.max(1);
        let prefix = prefix_samples.min(samples.len());
        if self.session.is_none()
            || !samples.len().is_multiple_of(channels)
            || !prefix.is_multiple_of(channels)
        {
            return false;
        }
        let before = captured_before.saturating_add((prefix / channels) as u64);
        let after = before.saturating_add(((samples.len() - prefix) / channels) as u64);
        let mut fed = false;
        let mut visit = |start: u64, end: u64, active: bool| {
            if active {
                let first = prefix + (start - before) as usize * channels;
                let last = prefix + (end - before) as usize * channels;
                fed |= feed_meter_session(
                    self.session,
                    self.publication,
                    &samples[first..last],
                    plan.meter_clock_start((first / channels) as u64),
                );
            } else {
                pause_meter_session(self.session, self.publication);
            }
        };
        if let Some(edges) = self.tracker.meter_activity_snapshot() {
            edges.for_each_range(before, after, self.fallback_active, &mut visit);
        } else if before < after {
            visit(before, after, self.fallback_active);
        }
        fed
    }
}

#[cfg(test)]
#[path = "meter_feed_gate_tests.rs"]
mod tests;

#[cfg(test)]
#[path = "meter_watch_feed_tests.rs"]
mod watch_tests;

#[cfg(test)]
#[path = "measure_preparation_drain_tests.rs"]
mod preparation_tests;
