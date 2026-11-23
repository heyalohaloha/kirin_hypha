//! Native current values of ONE Session snapshot and PRE at the newest boundary both sides published.
use super::*;

#[repr(C)]
pub struct KirinReferenceLiveMetricFrame {
    pub version: u32,
    pub struct_size: u32,
    pub sample_rate: u32,
    pub channels: u32,
    pub measurement_epoch: u64,
    pub generation: u64,
    pub observed_frames: u64,
    pub own_crest_db: f64,
    pub own_psr_db: f64,
    pub pre_crest_db: f64,
    pub pre_psr_db: f64,
    pub valid_mask: u32,
    pub reserved: u32,
}

type CrestPsr = [Option<f64>; 2];

/// Ordinary contention is not a missing pair: the last confirmed boundary keeps its original
/// lifetime, and only for the same measurement and source span. UI thread only.
#[derive(Default)]
pub(crate) struct ReferencePairHold(Mutex<Option<(PairSource, time_ffi::PairedBoundary)>>);

#[derive(Clone, Copy, PartialEq)]
struct PairSource {
    measurement_epoch: u64,
    generation: u64,
    span_token: u64,
}

impl ReferencePairHold {
    fn clear(&self) {
        if let Ok(mut held) = self.0.try_lock() {
            *held = None;
        }
    }

    fn resolve(
        &self,
        source: PairSource,
        pair: Result<Option<time_ffi::PairedBoundary>, time_ffi::LevelBusy>,
        now: Instant,
    ) -> Option<time_ffi::PairedBoundary> {
        let mut held = self.0.try_lock().ok()?;
        if let Ok(pair) = pair {
            *held = pair.map(|pair| (source, pair));
        } else {
            *held = held.filter(|(from, pair)| *from == source && now < pair.until);
        }
        held.map(|(_, pair)| pair)
    }
}

/// `pair` is POST's own value and the POST − PRE difference of one boundary. While a pair is
/// confirmed, POST and PRE are both that boundary's values, so the two needles show one moment.
fn paired_values(
    current: &MeasureResult,
    pair: Option<(CrestPsr, CrestPsr)>,
    active: bool,
) -> [f64; 4] {
    let (own_at, difference) = pair.unwrap_or(([None, None], [None, None]));
    let values = |now: Option<f64>, own_at: Option<f64>, difference: Option<f64>| {
        let now = now
            .filter(|value| active && value.is_finite())
            .unwrap_or(f64::NAN);
        let own_at = own_at.unwrap_or(f64::NAN);
        let pre = own_at - difference.unwrap_or(f64::NAN);
        if now.is_finite() && own_at.is_finite() && pre.is_finite() {
            (own_at, pre)
        } else {
            (now, f64::NAN)
        }
    };
    let crest = values(current.crest, own_at[0], difference[0]);
    let psr = values(current.psr, own_at[1], difference[1]);
    [crest.0, psr.0, crest.1, psr.1]
}

fn confirmed_pair(
    engine: &KirinHyphaEngine,
    snapshot: &MeterSessionSnapshot,
) -> Option<time_ffi::PairedBoundary> {
    let source = PairSource {
        measurement_epoch: snapshot.measurement_epoch,
        generation: snapshot.generation,
        span_token: engine.meter_delta_history.as_ref()?.time_post_span_token(),
    };
    let pair = time_ffi::paired_boundary_values(engine, snapshot);
    engine
        .reference_pair_hold
        .resolve(source, pair, Instant::now())
}

/// # Safety
/// Handle is null or live; out is null or writable for one complete frame. UI/non-RT only.
/// Never combine a separate poll_watch_display result with these deltas.
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_poll_reference_live_metrics(
    handle: *mut KirinHyphaEngine,
    out: *mut KirinReferenceLiveMetricFrame,
) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() || out.is_null() {
            return false;
        }
        let engine = unsafe { &*handle };
        let before = engine.signal_state_abi();
        let Some(snapshot) = engine.poll_meter_session() else {
            return false;
        };
        if !matches!(snapshot.layout.channel_count(), 1 | 2) {
            return false;
        }
        let active = before == KIRIN_SIGNAL_STATE_ACTIVE
            && snapshot.state == MeterSessionState::Active
            && !observatory_progress::stalled_meter(engine, &snapshot, before);
        // Busy/missing pairing does not erase independently acquired own measurements.
        let pair = if active {
            confirmed_pair(engine, &snapshot)
        } else {
            engine.reference_pair_hold.clear();
            None
        };
        if engine.signal_state_abi() != before {
            return false;
        }
        let values = paired_values(
            &snapshot.current,
            pair.as_ref().map(|pair| {
                (
                    [pair.own.1, pair.own.0[3]],
                    [pair.difference.1, pair.difference.0[3]],
                )
            }),
            active,
        );
        let valid_mask = values.iter().enumerate().fold(0, |mask, (index, value)| {
            mask | if value.is_finite() { 1 << index } else { 0 }
        });
        unsafe {
            *out = KirinReferenceLiveMetricFrame {
                version: 1,
                struct_size: std::mem::size_of::<KirinReferenceLiveMetricFrame>() as u32,
                sample_rate: snapshot.sample_rate,
                channels: snapshot.layout.channel_count() as u32,
                measurement_epoch: snapshot.measurement_epoch,
                generation: snapshot.generation,
                observed_frames: snapshot.observed_frames,
                own_crest_db: values[0],
                own_psr_db: values[1],
                pre_crest_db: values[2],
                pre_psr_db: values[3],
                valid_mask,
                reserved: 0,
            };
        }
        true
    }))
    .unwrap_or(false)
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn reference_live_metric_frame_is_append_only_and_null_safe() {
        assert_eq!(std::mem::size_of::<KirinReferenceLiveMetricFrame>(), 80);
        assert_eq!(
            std::mem::offset_of!(KirinReferenceLiveMetricFrame, own_crest_db),
            40
        );
        assert_eq!(
            std::mem::offset_of!(KirinReferenceLiveMetricFrame, valid_mask),
            72
        );
        assert!(!unsafe {
            kirin_hypha_poll_reference_live_metrics(std::ptr::null_mut(), std::ptr::null_mut())
        });
    }
    #[test]
    fn contention_keeps_only_the_same_source_until_its_original_end() {
        let start = Instant::now();
        let source = PairSource {
            measurement_epoch: 1,
            generation: 2,
            span_token: 3,
        };
        let pair = time_ffi::PairedBoundary {
            until: start + Duration::from_millis(1000),
            own: ([Some(1.0); 6], Some(9.0)),
            difference: ([Some(0.5); 6], Some(2.0)),
        };
        let busy = || Err(time_ffi::LevelBusy);
        let hold = ReferencePairHold::default();
        assert!(hold.resolve(source, busy(), start).is_none());
        assert!(hold.resolve(source, Ok(Some(pair)), start).is_some());
        for ms in [33, 500, 999] {
            let now = start + Duration::from_millis(ms);
            assert_eq!(hold.resolve(source, busy(), now).unwrap().own, pair.own);
        }
        // Expired, then gone for good: contention never renews the boundary.
        assert!(hold.resolve(source, busy(), pair.until).is_none());
        assert!(hold.resolve(source, busy(), start).is_none());
        for changed in [
            PairSource {
                measurement_epoch: 9,
                ..source
            },
            PairSource {
                generation: 9,
                ..source
            },
            PairSource {
                span_token: 9,
                ..source
            },
        ] {
            assert!(hold.resolve(source, Ok(Some(pair)), start).is_some());
            assert!(hold.resolve(changed, busy(), start).is_none());
            assert!(hold.resolve(source, busy(), start).is_none());
        }
        // A known missing pair is not contention.
        assert!(hold.resolve(source, Ok(Some(pair)), start).is_some());
        assert!(hold.resolve(source, Ok(None), start).is_none());
        assert!(hold.resolve(source, busy(), start).is_none());
    }

    #[test]
    fn reference_live_metrics_never_reconstruct_unconfirmed_or_missing_pre() {
        let current = MeasureResult {
            crest: Some(5.0),
            psr: Some(12.0),
            ..Default::default()
        };
        let own = [Some(5.0), Some(12.0)];
        let mut delta = [Some(1.5), Some(-2.0)];
        assert_eq!(
            paired_values(&current, Some((own, delta)), true),
            [5.0, 12.0, 3.5, 14.0]
        );
        // Both values belong to the boundary the difference was measured at, never a mix of two.
        assert_eq!(
            paired_values(&current, Some(([Some(4.0), Some(10.0)], delta)), true),
            [4.0, 10.0, 2.5, 12.0]
        );
        let waiting = paired_values(&current, None, true);
        assert_eq!(&waiting[..2], &[5.0, 12.0]);
        assert!(waiting[2..].iter().all(|value| value.is_nan()));
        assert!(paired_values(&current, Some((own, delta)), false)
            .iter()
            .all(|value| value.is_nan()));
        let no_own = paired_values(&current, Some(([None, Some(f64::INFINITY)], delta)), true);
        assert_eq!(&no_own[..2], &[5.0, 12.0]);
        assert!(no_own[2..].iter().all(|value| value.is_nan()));
        delta[1] = None;
        let partial = paired_values(&current, Some((own, delta)), true);
        assert_eq!(&partial[..3], &[5.0, 12.0, 3.5]);
        assert!(partial[3].is_nan());
        let missing = MeasureResult {
            crest: Some(f64::INFINITY),
            ..Default::default()
        };
        assert!(paired_values(&missing, Some((own, delta)), true)
            .iter()
            .all(|value| value.is_nan()));
    }
}
