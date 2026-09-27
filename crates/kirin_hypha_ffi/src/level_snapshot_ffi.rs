//! One bounded, UI-only LEVEL acquisition. History and meter share a session lock and cutoff;
//! the exact-pair chain is admitted only against that same POST session snapshot.
use super::*;

pub const KIRIN_LEVEL_SNAPSHOT_VERSION: u32 = 1;
// Keep startup/unconfigured distinct from the audition-suppressed revision (u64::MAX).
const UNCONFIGURED_ROLE_REVISION: u64 = u64::MAX - 1;

#[repr(C)]
pub struct KirinLevelSnapshot {
    pub version: u32,
    pub history_count: u32,
    pub chain_updated: u8,
    pub reserved: [u8; 7],
    pub frame: KirinObservatoryFrame,
    pub chain: KirinChainSnapshot,
}

fn valid_request(
    history_max: u32,
    history_capacity: u32,
    chain_capacity: u32,
    history: *mut KirinMeterHistoryEntry,
    chain: *mut KirinChainPoint,
) -> bool {
    history_max <= 600
        && history_capacity <= history_max
        && (history_capacity == 0 || !history.is_null())
        && matches!(chain_capacity, 0 | 1 | 600)
        && (chain_capacity == 0 || !chain.is_null())
}

/// Acquire a LEVEL frame, absolute 10 Hz history and optional exact-pair chain as one packet.
/// On false, all caller outputs remain untouched. Never called from the audio thread.
///
/// # Safety
/// The handle and packet pointer are valid. Nonzero capacities name writable arrays of that
/// many entries. No pointer may alias another output or live engine memory.
#[no_mangle]
#[allow(clippy::too_many_arguments)] // Fixed, versioned C ABI: independently bounded output arrays.
pub unsafe extern "C" fn kirin_hypha_poll_level_snapshot(
    handle: *mut KirinHyphaEngine,
    version: u32,
    history_max: u32,
    history: *mut KirinMeterHistoryEntry,
    history_capacity: u32,
    known_chain_revision: u64,
    chain: *mut KirinChainPoint,
    chain_capacity: u32,
    out: *mut KirinLevelSnapshot,
) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null()
            || out.is_null()
            || version != KIRIN_LEVEL_SNAPSHOT_VERSION
            || !valid_request(
                history_max,
                history_capacity,
                chain_capacity,
                history,
                chain,
            )
        {
            return false;
        }
        let engine = unsafe { &*handle };
        let signal_before = engine.signal_state_abi();
        let Some(session) = engine
            .meter_session
            .as_ref()
            .and_then(|meter| meter.try_lock().ok())
        else {
            return false;
        };
        let snapshot = session.snapshot();
        let entries = session.recent_history_decimated(
            MeterHistoryResolution::Hz10,
            history_max as usize,
            history_capacity as usize,
        );
        drop(session);
        // Never attach an old measurement span or a future endpoint to this frame.
        if entries.iter().any(|point| {
            point.measurement_epoch != snapshot.measurement_epoch
                || point.generation != snapshot.generation
                || point.last_observed_frames > snapshot.observed_frames
        }) {
            return false;
        }
        let Some(frame) = build_observatory_frame(engine, &snapshot, signal_before) else {
            return false;
        };

        let mut chain_snapshot = KirinChainSnapshot::default();
        let mut chain_points = Vec::new();
        let mut chain_updated = false;
        if chain_capacity > 0 {
            let Some(role) = engine.write_role.try_lock().ok().map(|role| *role) else {
                return false;
            };
            if role != Some(PluginDataRole::Post) {
                if known_chain_revision != UNCONFIGURED_ROLE_REVISION {
                    chain_snapshot = KirinChainSnapshot {
                        version: if chain_capacity == 1 {
                            KIRIN_CHAIN_VERSION_LATEST
                        } else {
                            KIRIN_CHAIN_VERSION
                        },
                        revision: UNCONFIGURED_ROLE_REVISION,
                        ..Default::default()
                    };
                    chain_updated = true;
                }
            } else {
                let audition_epoch = engine.audition.epoch();
                let audition_active = engine.audition.is_active();
                if audition_active {
                    if known_chain_revision != u64::MAX {
                        chain_snapshot = KirinChainSnapshot {
                            version: KIRIN_CHAIN_VERSION,
                            revision: u64::MAX,
                            status: KIRIN_CHAIN_SUPPRESSED,
                            ..Default::default()
                        };
                        chain_updated = true;
                    }
                } else {
                    let Some(exchange) = engine.meter_delta_history.as_ref() else {
                        return false;
                    };
                    if let Some(joined) = exchange.chain_snapshot_limit(
                        known_chain_revision,
                        &snapshot,
                        chain_capacity as usize,
                    ) {
                        if joined.points.iter().any(|point| {
                            point.raw.post_epoch != snapshot.measurement_epoch
                                || point.raw.post_generation != snapshot.generation
                                || point.raw.post_observed > snapshot.observed_frames
                        }) {
                            return false;
                        }
                        chain_snapshot = KirinChainSnapshot {
                            revision: joined.revision,
                            binding: joined.binding,
                            post_observed: joined.post_observed,
                            version: if chain_capacity == 1 {
                                KIRIN_CHAIN_VERSION_LATEST
                            } else {
                                KIRIN_CHAIN_VERSION
                            },
                            sample_rate: joined.sample_rate,
                            count: joined.points.len() as u32,
                            status: joined.status as u8,
                            reserved: [0; 3],
                        };
                        chain_points = joined
                            .points
                            .into_iter()
                            .map(KirinChainPoint::from)
                            .collect();
                        chain_updated = true;
                    }
                }
                if engine.audition.epoch() != audition_epoch
                    || engine.audition.is_active() != audition_active
                {
                    return false;
                }
            }
        }
        if engine.signal_state_abi() != signal_before {
            return false;
        }
        let history_count = entries.len() as u32;
        for (index, point) in entries.into_iter().enumerate() {
            unsafe { history.add(index).write(to_c_history_entry(point)) };
        }
        for (index, point) in chain_points.into_iter().enumerate() {
            unsafe { chain.add(index).write(point) };
        }
        unsafe {
            *out = KirinLevelSnapshot {
                version,
                history_count,
                chain_updated: chain_updated as u8,
                reserved: [0; 7],
                frame,
                chain: chain_snapshot,
            };
        }
        true
    }))
    .unwrap_or(false)
}

#[cfg(test)]
#[path = "level_snapshot_ffi_tests.rs"]
mod tests;
