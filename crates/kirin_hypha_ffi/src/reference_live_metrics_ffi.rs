//! Native current values and exact-pair differences from ONE Session snapshot.
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

fn paired_values(
    current: &MeasureResult,
    delta: Option<[Option<f64>; 2]>,
    active: bool,
) -> [f64; 4] {
    let own = |value: Option<f64>| {
        value
            .filter(|value| active && value.is_finite())
            .unwrap_or(f64::NAN)
    };
    let crest = own(current.crest);
    let psr = own(current.psr);
    let pre = |value: f64, difference: Option<f64>| {
        let difference = difference.unwrap_or(f64::NAN);
        let result = value - difference;
        if value.is_finite() && difference.is_finite() && result.is_finite() {
            result
        } else {
            f64::NAN
        }
    };
    let delta = delta.unwrap_or([None, None]);
    [crest, psr, pre(crest, delta[0]), pre(psr, delta[1])]
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
        let delta = if active {
            time_ffi::exact_level_values(engine, &snapshot)
                .ok()
                .flatten()
                .map(|(values, crest)| [crest, values[3]])
        } else {
            None
        };
        if engine.signal_state_abi() != before {
            return false;
        }
        let values = paired_values(&snapshot.current, delta, active);
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
    fn reference_live_metrics_never_reconstruct_unconfirmed_or_missing_pre() {
        let current = MeasureResult {
            crest: Some(5.0),
            psr: Some(12.0),
            ..Default::default()
        };
        let mut delta = [Some(1.5), Some(-2.0)];
        assert_eq!(
            paired_values(&current, Some(delta), true),
            [5.0, 12.0, 3.5, 14.0]
        );
        let waiting = paired_values(&current, None, true);
        assert_eq!(&waiting[..2], &[5.0, 12.0]);
        assert!(waiting[2..].iter().all(|value| value.is_nan()));
        assert!(paired_values(&current, Some(delta), false)
            .iter()
            .all(|value| value.is_nan()));
        delta[1] = None;
        let partial = paired_values(&current, Some(delta), true);
        assert_eq!(partial[2], 3.5);
        assert!(partial[3].is_nan());
        let missing = MeasureResult {
            crest: Some(f64::INFINITY),
            ..Default::default()
        };
        assert!(paired_values(&missing, Some(delta), true)
            .iter()
            .all(|value| value.is_nan()));
    }
}
