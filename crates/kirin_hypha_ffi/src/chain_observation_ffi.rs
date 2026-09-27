//! Versioned, bounded exact-pair observation snapshot. UI only; never the audio callback.
use super::*;
use kirin_measure::meter_delta_history::chain;

/// Called only while configuring a fresh handle, before the IO publisher begins.
/// # Safety
/// `handle` must be null or a valid live engine pointer, and no other thread may destroy it.
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_set_chain_clock_policy(
    handle: *mut KirinHyphaEngine,
    policy: u8,
) {
    let _ = catch_unwind(AssertUnwindSafe(|| {
        if let Some(exchange) =
            unsafe { handle.as_ref() }.and_then(|engine| engine.meter_delta_history.as_ref())
        {
            exchange.set_clock_policy(policy);
        }
    }));
}

pub const KIRIN_CHAIN_VERSION: u32 = 1;
pub const KIRIN_CHAIN_VERSION_LATEST: u32 = 2;
pub const KIRIN_CHAIN_SUPPRESSED: u8 = 5;

#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct KirinChainPoint {
    pub pre_epoch: u64,
    pub post_epoch: u64,
    pub pre_incarnation: u64,
    pub pre_generation: u64,
    pub post_generation: u64,
    pub pre_run: u64,
    pub post_run: u64,
    pub pre_observed: u64,
    pub post_observed: u64,
    pub endpoint: i64,
    pub pre_m: f64,
    pub post_m: f64,
    pub pre_tp: f64,
    pub post_tp: f64,
    pub delta_m: f64,
    pub delta_tp: f64,
    pub relation: f64,
    pub source: u8,
    pub crossing: u8,
    pub pre_severity: u8,
    pub post_severity: u8,
    pub reserved: [u8; 4],
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct KirinChainSnapshot {
    pub revision: u64,
    pub binding: u64,
    pub post_observed: u64,
    pub version: u32,
    pub sample_rate: u32,
    pub count: u32,
    pub status: u8,
    pub reserved: [u8; 3],
}

fn suppressed_snapshot(known_revision: u64, version: u32) -> Option<KirinChainSnapshot> {
    (known_revision != u64::MAX).then_some(KirinChainSnapshot {
        version,
        revision: u64::MAX,
        status: KIRIN_CHAIN_SUPPRESSED,
        ..Default::default()
    })
}

fn valid_request(version: u32, capacity: u32) -> bool {
    (version == KIRIN_CHAIN_VERSION && capacity as usize == chain::CAPACITY)
        || (version == KIRIN_CHAIN_VERSION_LATEST
            && (capacity == 1 || capacity as usize == chain::CAPACITY))
}

impl From<chain::MatchedPoint> for KirinChainPoint {
    fn from(matched: chain::MatchedPoint) -> Self {
        let p = matched.raw;
        Self {
            pre_epoch: p.pre_epoch,
            post_epoch: p.post_epoch,
            pre_incarnation: p.pre_incarnation,
            pre_generation: p.pre_generation,
            post_generation: p.post_generation,
            pre_run: p.pre_run,
            post_run: p.post_run,
            pre_observed: p.pre_observed,
            post_observed: p.post_observed,
            endpoint: p.endpoint,
            pre_m: opt_f64(p.pre_m),
            post_m: opt_f64(p.post_m),
            pre_tp: opt_f64(p.pre_tp),
            post_tp: opt_f64(p.post_tp),
            delta_m: opt_f64(matched.delta_m),
            delta_tp: opt_f64(matched.delta_tp),
            relation: opt_f64(matched.relation),
            source: p.source,
            crossing: matched.crossing as u8,
            pre_severity: matched.pre_severity,
            post_severity: matched.post_severity,
            reserved: [0; 4],
        }
    }
}

/// Returns false for unchanged revision, lock contention, wrong ABI or invalid arguments.
/// On true, metadata and all points belong to one producer revision; no truncated batch.
/// # Safety
/// UI thread only. Handle and output pointers must be valid, with capacity writable points.
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_poll_chain_observation(
    handle: *mut KirinHyphaEngine,
    version: u32,
    known_revision: u64,
    out: *mut KirinChainSnapshot,
    points: *mut KirinChainPoint,
    capacity: u32,
) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null()
            || out.is_null()
            || points.is_null()
            || !valid_request(version, capacity)
        {
            return false;
        }
        let engine = unsafe { &*handle };
        let audition_epoch = engine.audition.epoch();
        if engine.write_role.try_lock().ok().and_then(|role| *role) != Some(PluginDataRole::Post) {
            return false;
        }
        if engine.audition.is_active() {
            let Some(suppressed) = suppressed_snapshot(known_revision, version) else {
                return false;
            };
            unsafe {
                *out = suppressed;
            }
            return true;
        }
        let Some(meter) = engine.poll_meter_session() else {
            return false;
        };
        let Some(exchange) = engine.meter_delta_history.as_ref() else {
            return false;
        };
        let Some(snapshot) =
            exchange.chain_snapshot_limit(known_revision, &meter, capacity as usize)
        else {
            return false;
        };
        // Also catch a complete enter/leave transition during assembly.
        if engine.audition.is_active() || engine.audition.epoch() != audition_epoch {
            return false;
        }
        let metadata = KirinChainSnapshot {
            revision: snapshot.revision,
            binding: snapshot.binding,
            post_observed: snapshot.post_observed,
            version,
            sample_rate: snapshot.sample_rate,
            count: snapshot.points.len() as u32,
            status: snapshot.status as u8,
            reserved: [0; 3],
        };
        for (index, point) in snapshot.points.into_iter().enumerate() {
            unsafe {
                points.add(index).write(point.into());
            }
        }
        unsafe {
            *out = metadata;
        }
        true
    }))
    .unwrap_or(false)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn audition_suppression_has_no_values_or_repeated_notifications() {
        let state = suppressed_snapshot(0, KIRIN_CHAIN_VERSION_LATEST).unwrap();
        assert_eq!(state.version, KIRIN_CHAIN_VERSION_LATEST);
        assert_eq!(state.status, KIRIN_CHAIN_SUPPRESSED);
        assert_eq!(state.count, 0);
        assert_eq!(state.binding, 0);
        for _ in 0..1_000 {
            assert!(suppressed_snapshot(state.revision, KIRIN_CHAIN_VERSION_LATEST).is_none());
        }
    }

    #[test]
    fn layout_and_invalid_call_are_stable() {
        assert!(valid_request(KIRIN_CHAIN_VERSION, 600));
        assert!(!valid_request(KIRIN_CHAIN_VERSION, 1));
        assert!(valid_request(KIRIN_CHAIN_VERSION_LATEST, 1));
        assert!(valid_request(KIRIN_CHAIN_VERSION_LATEST, 600));
        assert!(!valid_request(KIRIN_CHAIN_VERSION_LATEST, 2));
        assert!(!valid_request(0, 600));
        assert_eq!(std::mem::size_of::<KirinChainSnapshot>(), 40);
        assert_eq!(std::mem::size_of::<KirinChainPoint>(), 144);
        assert_eq!(std::mem::offset_of!(KirinChainPoint, source), 136);
        let mut metadata = KirinChainSnapshot {
            revision: 77,
            ..Default::default()
        };
        assert!(!unsafe {
            kirin_hypha_poll_chain_observation(
                std::ptr::null_mut(),
                KIRIN_CHAIN_VERSION,
                0,
                &mut metadata,
                std::ptr::null_mut(),
                600,
            )
        });
        assert_eq!(metadata.revision, 77);
    }
}
