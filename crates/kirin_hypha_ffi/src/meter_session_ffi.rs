use kirin_measure::{BalanceState, MeterSessionSnapshot, MeterSessionState};
use std::panic::{catch_unwind, AssertUnwindSafe};

use super::{
    opt_f64, KirinHyphaEngine, KirinMeterSession, KIRIN_BALANCE_LEFT_ONLY, KIRIN_BALANCE_NUMERIC,
    KIRIN_BALANCE_RIGHT_ONLY, KIRIN_BALANCE_UNAVAILABLE, KIRIN_METER_SESSION_ACTIVE,
    KIRIN_METER_SESSION_EMPTY, KIRIN_METER_SESSION_PAUSED, KIRIN_STEREO_FIELD_SIZE,
};

impl KirinHyphaEngine {
    /// Clears only the user-resettable Hybrid VU TP maximum and Clip latch.
    pub fn clear_meter_peak_clip_holds(&self) -> bool {
        let Some(session) = self.meter_session.as_ref() else {
            return false;
        };
        let Ok(mut session) = session.lock() else {
            return false;
        };
        session.clear_peak_clip_holds();
        if let Some(publication) = self.meter_session_publication.as_ref() {
            publication.publish(session.snapshot());
        }
        true
    }

    /// Only an explicit user action may discard the always-on meter session.
    /// Serializing with the Measure worker avoids losing an in-flight observation.
    pub fn reset_meter_session(&self) -> bool {
        let Some(session) = self.meter_session.as_ref() else {
            return false;
        };
        let Ok(mut session) = session.lock() else {
            return false;
        };
        let Ok(mut watch_max) = self.watch_max.lock() else {
            return false;
        };
        session.reset();
        watch_max.reset();
        if let Some(publication) = self.meter_session_publication.as_ref() {
            publication.publish(session.snapshot());
        }
        if let Some(exchange) = self.meter_delta_history.as_ref() {
            exchange.reset();
        }
        true
    }
}

/// Clears only the Hybrid VU TP maximum and Clip latch after an explicit user action.
///
/// # Safety
/// `handle` must either be null or point to a live engine. Call from the UI/control thread.
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_clear_meter_peak_clip_holds(
    handle: *mut KirinHyphaEngine,
) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        !handle.is_null() && unsafe { (&*handle).clear_meter_peak_clip_holds() }
    }))
    .unwrap_or(false)
}

/// Discards the always-on meter session after an explicit user action.
///
/// # Safety
/// `handle` must either be null or point to a live engine. Call from the UI/control thread.
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_reset_meter_session(handle: *mut KirinHyphaEngine) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        !handle.is_null() && unsafe { (&*handle).reset_meter_session() }
    }))
    .unwrap_or(false)
}

pub(super) fn to_c_meter_session(snapshot: &MeterSessionSnapshot) -> KirinMeterSession {
    let state = match snapshot.state {
        MeterSessionState::Empty => KIRIN_METER_SESSION_EMPTY,
        MeterSessionState::Active => KIRIN_METER_SESSION_ACTIVE,
        MeterSessionState::Paused => KIRIN_METER_SESSION_PAUSED,
    };
    let balance_state = match snapshot.stereo.balance_state {
        BalanceState::Unavailable => KIRIN_BALANCE_UNAVAILABLE,
        BalanceState::Numeric => KIRIN_BALANCE_NUMERIC,
        BalanceState::LeftOnly => KIRIN_BALANCE_LEFT_ONLY,
        BalanceState::RightOnly => KIRIN_BALANCE_RIGHT_ONLY,
    };
    KirinMeterSession {
        generation: snapshot.generation,
        active_frames: snapshot.active_frames,
        observed_frames: snapshot.observed_frames,
        sample_rate: snapshot.sample_rate,
        state,
        reserved: [0; 3],
        lufs_m: opt_f64(snapshot.current.lufs_m),
        lufs_s: opt_f64(snapshot.current.lufs_s),
        lufs_i: opt_f64(snapshot.summary.lufs_i),
        lra: opt_f64(snapshot.summary.lra),
        true_peak: opt_f64(snapshot.current.true_peak),
        max_true_peak: opt_f64(snapshot.summary.max_true_peak),
        plr: opt_f64(snapshot.plr),
        channels: snapshot.stereo.channels,
        balance_state,
        channel_clip_latched: snapshot.stereo.clip_latched.map(u8::from),
        stereo_reserved: [0; 4],
        sample_peak_dbfs: snapshot.stereo.sample_peak_dbfs.map(opt_f64),
        sample_peak_hold_dbfs: snapshot.stereo.sample_peak_hold_dbfs.map(opt_f64),
        channel_true_peak_dbtp: snapshot.stereo.true_peak_dbtp.map(opt_f64),
        channel_max_true_peak_dbtp: snapshot.stereo.max_true_peak_dbtp.map(opt_f64),
        clip_events: snapshot.stereo.clip_events,
        balance_db: opt_f64(snapshot.stereo.balance_db),
        correlation: opt_f64(snapshot.stereo.correlation),
        field_size: if snapshot.stereo.channels == 2 {
            KIRIN_STEREO_FIELD_SIZE
        } else {
            0
        },
        field_observation_count: snapshot.stereo.field_observation_count,
        field_reserved: [0; 6],
        field_density: snapshot.stereo.field_density,
        max_lufs_m: opt_f64(snapshot.max_lufs_m),
        channel_vu_dbfs: snapshot.stereo.vu_dbfs.map(opt_f64),
        channel_instant_true_peak_dbtp: snapshot.stereo.instant_true_peak_dbtp.map(opt_f64),
    }
}
