//! UI-only meter snapshots and history export. No audio processing lives here.
use super::*;
#[path = "chain_observation_ffi.rs"]
mod chain_ffi;
pub use chain_ffi::*;

impl KirinHyphaEngine {
    /// Record/Keepから独立した常設メーターセッションの最新完了値を読む。
    /// Live EBU calculation lock is never touched by this UI path.
    pub fn poll_meter_session(&self) -> Option<MeterSessionSnapshot> {
        self.meter_session_publication.as_ref()?.try_snapshot()
    }

    /// TIME履歴を選択したresolutionで新しい順の範囲まで非ブロッキング取得する。
    pub fn poll_meter_history(
        &self,
        resolution: MeterHistoryResolution,
        max_entries: usize,
    ) -> Option<Vec<MeterHistoryEntry>> {
        self.meter_session
            .as_ref()?
            .try_lock()
            .ok()
            .map(|session| session.recent_history(resolution, max_entries))
    }

    pub fn poll_meter_history_decimated(
        &self,
        resolution: MeterHistoryResolution,
        max_entries: usize,
        max_output: usize,
    ) -> Option<Vec<MeterHistoryEntry>> {
        self.meter_session
            .as_ref()?
            .try_lock()
            .ok()
            .map(|session| session.recent_history_decimated(resolution, max_entries, max_output))
    }

    /// Exact-pair POST−PRE TIME history. PRE/unenabled roles fail closed.
    pub fn poll_meter_delta_history(
        &self,
        resolution: MeterHistoryResolution,
        max_entries: usize,
    ) -> Option<Vec<MeterHistoryEntry>> {
        let is_post =
            self.write_role.lock().ok().and_then(|role| *role) == Some(PluginDataRole::Post);
        is_post.then(|| {
            self.meter_delta_history
                .as_ref()
                .map(|exchange| exchange.recent(resolution, max_entries))
        })?
    }

    pub fn poll_meter_delta_history_decimated(
        &self,
        resolution: MeterHistoryResolution,
        max_entries: usize,
        max_output: usize,
    ) -> Option<Vec<MeterHistoryEntry>> {
        let is_post =
            self.write_role.lock().ok().and_then(|role| *role) == Some(PluginDataRole::Post);
        is_post.then(|| {
            self.meter_delta_history
                .as_ref()
                .map(|exchange| exchange.recent_decimated(resolution, max_entries, max_output))
        })?
    }
}

fn to_c_history_range(range: MeterHistoryRange) -> KirinMeterHistoryRange {
    KirinMeterHistoryRange {
        min: opt_f64(range.min),
        max: opt_f64(range.max),
        mean: opt_f64(range.mean),
    }
}

pub(crate) fn to_c_history_entry(entry: MeterHistoryEntry) -> KirinMeterHistoryEntry {
    let resolution = match entry.resolution {
        MeterHistoryResolution::Hz10 => KIRIN_METER_HISTORY_10_HZ,
        MeterHistoryResolution::Hz1 => KIRIN_METER_HISTORY_1_HZ,
        MeterHistoryResolution::Hz0_1 => KIRIN_METER_HISTORY_0_1_HZ,
    };
    KirinMeterHistoryEntry {
        generation: entry.generation,
        measurement_epoch: entry.measurement_epoch,
        run_id: entry.run_id,
        first_observed_frames: entry.first_observed_frames,
        last_observed_frames: entry.last_observed_frames,
        first_timeline_endpoint_samples: entry.first_timeline_endpoint_samples.unwrap_or(i64::MIN),
        last_timeline_endpoint_samples: entry.last_timeline_endpoint_samples.unwrap_or(i64::MIN),
        observation_count: entry.observation_count,
        resolution,
        reserved: 0,
        clip_event_count: channel_abi::widen(&entry.clip_event_count, 0),
        lufs_m: to_c_history_range(entry.lufs_m),
        lufs_s: to_c_history_range(entry.lufs_s),
        true_peak: to_c_history_range(entry.true_peak),
        correlation: to_c_history_range(entry.correlation),
        plr: to_c_history_range(entry.plr),
    }
}

fn meter_history_resolution_from_abi(value: u8) -> Option<MeterHistoryResolution> {
    match value {
        KIRIN_METER_HISTORY_10_HZ => Some(MeterHistoryResolution::Hz10),
        KIRIN_METER_HISTORY_1_HZ => Some(MeterHistoryResolution::Hz1),
        KIRIN_METER_HISTORY_0_1_HZ => Some(MeterHistoryResolution::Hz0_1),
        _ => None,
    }
}

/// Record/Keepから独立した常設メーターセッションを1スナップショットで取得する。
/// Empty状態も成立した事実なのでtrueを返し、未成立値はNaNになる。
///
/// # Safety
/// `handle`/`out` は有効。UI Threadから呼ぶこと。
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_poll_meter_session(
    handle: *mut KirinHyphaEngine,
    out: *mut KirinMeterSession,
) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() || out.is_null() {
            return false;
        }
        let Some(snapshot) = (unsafe { &*handle }).poll_meter_session() else {
            return false;
        };
        unsafe { *out = to_c_meter_session(&snapshot) };
        true
    }))
    .unwrap_or(false)
}

/// Observatoryの測定事実を1つのversion付きフレームとして取得する。
/// 組立前後でsignal stateが変わった場合はfalseとし、異なる時点を混ぜない。
///
/// # Safety
/// `handle`/`out` は有効。UI Threadから呼ぶこと。
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_poll_observatory_frame(
    handle: *mut KirinHyphaEngine,
    out: *mut KirinObservatoryFrame,
) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() || out.is_null() {
            return false;
        }
        let engine = unsafe { &*handle };
        let signal_before = engine.signal_state_abi();
        let Some(snapshot) = engine.poll_meter_session() else {
            return false;
        };
        let delta_result = engine.poll_delta().unwrap_or_default();
        let delta = to_c_delta(&delta_result);
        let signal_after = engine.signal_state_abi();
        if signal_before != signal_after {
            return false;
        }
        let (lra_state, lra_elapsed_seconds) = lra_readiness(&snapshot);
        let comparison = comparison_projection(
            &delta_result,
            snapshot.measurement_epoch,
            snapshot.generation,
        );
        let frame = KirinObservatoryFrame {
            version: abi_contract::KIRIN_OBSERVATORY_FRAME_VERSION,
            signal_state: signal_after,
            lra_state,
            delta_available: delta_has_finite_fact(&delta) as u8,
            comparison_state: comparison.state,
            lra_elapsed_seconds,
            meter: to_c_meter_session(&snapshot),
            delta,
            comparison_reason: comparison.reason,
            comparison_reserved: [0; 7],
            comparison_generation: comparison.generation,
            comparison_identity: comparison.identity,
        };
        unsafe { *out = frame };
        true
    }))
    .unwrap_or(false)
}

/// 常設Meter SessionのTIME履歴を古い順で最大`out_capacity`件取得する。
/// 10 Hzはexact、1 Hz/0.1 Hzはmin/max/mean集約であり、同じ線として偽装しない。
///
/// # Safety
/// `out_count`は書き込み可能、`out_capacity > 0`なら`out`は同数要素を書き込み可能であること。
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_poll_meter_history(
    handle: *mut KirinHyphaEngine,
    resolution: u8,
    out: *mut KirinMeterHistoryEntry,
    out_capacity: u32,
    out_count: *mut u32,
) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null()
            || out_count.is_null()
            || (out_capacity > 0 && out.is_null())
            || out_capacity as usize > KIRIN_METER_HISTORY_MAX_ENTRIES
        {
            return false;
        }
        let Some(resolution) = meter_history_resolution_from_abi(resolution) else {
            return false;
        };
        let Some(entries) =
            (unsafe { &*handle }).poll_meter_history(resolution, out_capacity as usize)
        else {
            return false;
        };
        let count = u32::try_from(entries.len()).unwrap_or(u32::MAX);
        for (index, entry) in entries.into_iter().enumerate() {
            unsafe { out.add(index).write(to_c_history_entry(entry)) };
        }
        unsafe { *out_count = out_capacity.min(count) };
        true
    }))
    .unwrap_or(false)
}

/// 指定時間範囲を最大`out_capacity`点へ集約して取得する。
///
/// # Safety
/// `handle`は有効なエンジンを指すこと。`out_count`は書き込み可能で、`out_capacity > 0`
/// のとき`out`は同数以上の要素を書き込める領域を指すこと。UI Threadから呼ぶこと。
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_poll_meter_history_decimated(
    handle: *mut KirinHyphaEngine,
    resolution: u8,
    max_entries: u32,
    out: *mut KirinMeterHistoryEntry,
    out_capacity: u32,
    out_count: *mut u32,
) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null()
            || out_count.is_null()
            || (out_capacity > 0 && out.is_null())
            || max_entries as usize > KIRIN_METER_HISTORY_MAX_ENTRIES
            || out_capacity as usize > KIRIN_METER_HISTORY_MAX_ENTRIES
        {
            return false;
        }
        let Some(resolution) = meter_history_resolution_from_abi(resolution) else {
            return false;
        };
        let Some(entries) = (unsafe { &*handle }).poll_meter_history_decimated(
            resolution,
            max_entries as usize,
            out_capacity as usize,
        ) else {
            return false;
        };
        for (index, entry) in entries.iter().copied().enumerate() {
            unsafe { out.add(index).write(to_c_history_entry(entry)) };
        }
        unsafe { *out_count = entries.len() as u32 };
        true
    }))
    .unwrap_or(false)
}

/// 同じDAW presentation sample終端で結合できたPOST−PRE TIME履歴だけを返す。
/// 欠測・重複時刻・PRE roleは値を生成しない。
///
/// # Safety
/// `out_count`は書き込み可能、`out_capacity > 0`なら`out`は同数要素を書き込み可能であること。
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_poll_meter_delta_history(
    handle: *mut KirinHyphaEngine,
    resolution: u8,
    out: *mut KirinMeterHistoryEntry,
    out_capacity: u32,
    out_count: *mut u32,
) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null()
            || out_count.is_null()
            || (out_capacity > 0 && out.is_null())
            || out_capacity as usize > KIRIN_METER_HISTORY_MAX_ENTRIES
        {
            return false;
        }
        let Some(resolution) = meter_history_resolution_from_abi(resolution) else {
            return false;
        };
        let Some(entries) =
            (unsafe { &*handle }).poll_meter_delta_history(resolution, out_capacity as usize)
        else {
            return false;
        };
        let count = u32::try_from(entries.len()).unwrap_or(u32::MAX);
        for (index, entry) in entries.into_iter().enumerate() {
            unsafe { out.add(index).write(to_c_history_entry(entry)) };
        }
        unsafe { *out_count = out_capacity.min(count) };
        true
    }))
    .unwrap_or(false)
}

/// exact join済みPOST−PRE履歴を最大`out_capacity`点へ集約して取得する。
///
/// # Safety
/// `handle`は有効なエンジンを指すこと。`out_count`は書き込み可能で、`out_capacity > 0`
/// のとき`out`は同数以上の要素を書き込める領域を指すこと。UI Threadから呼ぶこと。
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_poll_meter_delta_history_decimated(
    handle: *mut KirinHyphaEngine,
    resolution: u8,
    max_entries: u32,
    out: *mut KirinMeterHistoryEntry,
    out_capacity: u32,
    out_count: *mut u32,
) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null()
            || out_count.is_null()
            || (out_capacity > 0 && out.is_null())
            || max_entries as usize > KIRIN_METER_HISTORY_MAX_ENTRIES
            || out_capacity as usize > KIRIN_METER_HISTORY_MAX_ENTRIES
        {
            return false;
        }
        let Some(resolution) = meter_history_resolution_from_abi(resolution) else {
            return false;
        };
        let Some(entries) = (unsafe { &*handle }).poll_meter_delta_history_decimated(
            resolution,
            max_entries as usize,
            out_capacity as usize,
        ) else {
            return false;
        };
        for (index, entry) in entries.iter().copied().enumerate() {
            unsafe { out.add(index).write(to_c_history_entry(entry)) };
        }
        unsafe { *out_count = entries.len() as u32 };
        true
    }))
    .unwrap_or(false)
}
