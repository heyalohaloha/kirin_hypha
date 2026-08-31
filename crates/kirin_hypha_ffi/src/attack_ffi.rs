//! Internal-only ATTACK DRUM C ABI.
//!
//! This deliberately does not extend `AnalysisViewMode`, publish a GUI route, or persist state.
//! It exposes the already-isolated SuperFlux worker to validation builds while the shipping
//! default remains OFF.

use std::panic::{catch_unwind, AssertUnwindSafe};

use kirin_measure::{
    AttackHistory, PluginDataRole, ATTACK_SHAPE_POINT_CAPACITY, ATTACK_WAVEFORM_HISTORY_CAPACITY,
};

use super::KirinHyphaEngine;

#[path = "attack_ffi_convert.rs"]
mod convert;
use convert::*;

pub const KIRIN_ATTACK_BATCH_CAPACITY: usize = 64;
// Six seconds at the strict >30 ms event separation can contain at most 200 confirmed events.
// Keep one complete internal presentation window in a single lock-free snapshot.
pub const KIRIN_ATTACK_EVENT_BATCH_CAPACITY: usize = 240;
pub const KIRIN_ATTACK_WAVEFORM_BATCH_CAPACITY: usize = ATTACK_WAVEFORM_HISTORY_CAPACITY;
pub const KIRIN_ATTACK_DETAIL_BATCH_CAPACITY: usize = 240;
pub const KIRIN_ATTACK_SHAPE_CAPACITY: usize = ATTACK_SHAPE_POINT_CAPACITY;

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct KirinAttackOdfFrame {
    pub generation: u64,
    pub sample_rate: u32,
    pub channels: u8,
    pub reserved: [u8; 3],
    pub definition_hash: [u8; 32],
    pub window_samples: u32,
    pub hop_samples: u32,
    pub support_start_samples: i64,
    pub support_end_samples: i64,
    pub event_sample: i64,
    pub value: f32,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct KirinAttackBatch {
    pub count: u32,
    pub capacity: u32,
    pub frames: [KirinAttackOdfFrame; KIRIN_ATTACK_BATCH_CAPACITY],
}

impl Default for KirinAttackBatch {
    fn default() -> Self {
        Self {
            count: 0,
            capacity: KIRIN_ATTACK_BATCH_CAPACITY as u32,
            frames: [KirinAttackOdfFrame::default(); KIRIN_ATTACK_BATCH_CAPACITY],
        }
    }
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct KirinAttackEvent {
    pub generation: u64,
    pub sample_rate: u32,
    pub channels: u8,
    pub reserved: [u8; 3],
    pub definition_hash: [u8; 32],
    pub event_sample: i64,
    pub decision_sample: i64,
    pub value: f32,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct KirinAttackEventBatch {
    pub count: u32,
    pub capacity: u32,
    pub events: [KirinAttackEvent; KIRIN_ATTACK_EVENT_BATCH_CAPACITY],
}

impl Default for KirinAttackEventBatch {
    fn default() -> Self {
        Self {
            count: 0,
            capacity: KIRIN_ATTACK_EVENT_BATCH_CAPACITY as u32,
            events: [KirinAttackEvent::default(); KIRIN_ATTACK_EVENT_BATCH_CAPACITY],
        }
    }
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct KirinAttackWaveformPoint {
    pub generation: u64,
    pub sample_rate: u32,
    pub channels: u8,
    pub reserved: [u8; 3],
    pub start_sample: i64,
    pub end_sample: i64,
    pub peak_linear: f32,
    pub rms_dbfs: f32,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct KirinAttackWaveformBatch {
    pub count: u32,
    pub capacity: u32,
    pub points: [KirinAttackWaveformPoint; KIRIN_ATTACK_WAVEFORM_BATCH_CAPACITY],
}

impl Default for KirinAttackWaveformBatch {
    fn default() -> Self {
        Self {
            count: 0,
            capacity: KIRIN_ATTACK_WAVEFORM_BATCH_CAPACITY as u32,
            points: [KirinAttackWaveformPoint::default(); KIRIN_ATTACK_WAVEFORM_BATCH_CAPACITY],
        }
    }
}

#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct KirinAttackDetail {
    pub generation: u64,
    pub sample_rate: u32,
    pub channels: u8,
    pub temporal_centroid_available: u8,
    pub sharpness_available: u8,
    pub reserved: u8,
    pub definition_hash: [u8; 32],
    pub event_sample: i64,
    pub decision_sample: i64,
    pub shape_start_sample: i64,
    pub shape_end_sample: i64,
    pub value: f32,
    pub contrast_db: f32,
    pub context_rms_dbfs: f32,
    pub attack_rms_dbfs: f32,
    pub sample_peak_dbfs: f32,
    pub crest_db: f32,
    pub sample_edge_ratio_db: f32,
    pub peak_plateau_ms: f32,
    pub temporal_centroid_ms: f32,
    pub sharpness_acum: f32,
    pub shape_count: u32,
    pub reserved2: u32,
    pub shape: [f32; KIRIN_ATTACK_SHAPE_CAPACITY],
}

impl Default for KirinAttackDetail {
    fn default() -> Self {
        Self {
            generation: 0,
            sample_rate: 0,
            channels: 0,
            temporal_centroid_available: 0,
            sharpness_available: 0,
            reserved: 0,
            definition_hash: [0; 32],
            event_sample: 0,
            decision_sample: 0,
            shape_start_sample: 0,
            shape_end_sample: 0,
            value: 0.0,
            contrast_db: 0.0,
            context_rms_dbfs: 0.0,
            attack_rms_dbfs: 0.0,
            sample_peak_dbfs: 0.0,
            crest_db: 0.0,
            sample_edge_ratio_db: 0.0,
            peak_plateau_ms: 0.0,
            temporal_centroid_ms: 0.0,
            sharpness_acum: 0.0,
            shape_count: 0,
            reserved2: 0,
            shape: [0.0; KIRIN_ATTACK_SHAPE_CAPACITY],
        }
    }
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct KirinAttackDetailBatch {
    pub count: u32,
    pub capacity: u32,
    pub details: [KirinAttackDetail; KIRIN_ATTACK_DETAIL_BATCH_CAPACITY],
}

impl Default for KirinAttackDetailBatch {
    fn default() -> Self {
        Self {
            count: 0,
            capacity: KIRIN_ATTACK_DETAIL_BATCH_CAPACITY as u32,
            details: [KirinAttackDetail::default(); KIRIN_ATTACK_DETAIL_BATCH_CAPACITY],
        }
    }
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct KirinAttackStats {
    pub available: u8,
    pub enabled: u8,
    pub worker_running: u8,
    pub channels: u8,
    pub reserved: [u8; 4],
    pub pushed_blocks: u64,
    pub dropped_blocks: u64,
    pub analyzed_frames: u64,
}

impl KirinHyphaEngine {
    /// POST-only validation switch. It is intentionally absent from JUCE navigation/state.
    pub fn set_internal_attack_enabled(&self, enabled: bool) -> bool {
        if self.write_role.lock().ok().and_then(|role| *role) != Some(PluginDataRole::Post) {
            return false;
        }
        self.attack_runtime
            .as_ref()
            .is_some_and(|runtime| runtime.set_enabled(enabled))
    }

    pub fn poll_internal_attack_batch(&self) -> Option<KirinAttackBatch> {
        if self.write_role.lock().ok().and_then(|role| *role) != Some(PluginDataRole::Post) {
            return None;
        }
        self.attack_runtime
            .as_ref()?
            .try_history()
            .as_ref()
            .map(to_c_attack_batch)
    }

    pub fn poll_internal_attack_events(&self) -> Option<KirinAttackEventBatch> {
        if self.write_role.lock().ok().and_then(|role| *role) != Some(PluginDataRole::Post) {
            return None;
        }
        self.attack_runtime
            .as_ref()?
            .try_history()
            .as_ref()
            .map(to_c_attack_event_batch)
    }

    pub fn poll_internal_attack_waveform(&self) -> Option<KirinAttackWaveformBatch> {
        self.post_attack_history().map(to_c_attack_waveform_batch)
    }

    pub fn poll_internal_attack_details(&self) -> Option<KirinAttackDetailBatch> {
        self.post_attack_history().map(to_c_attack_detail_batch)
    }

    pub fn internal_attack_stats(&self) -> KirinAttackStats {
        self.attack_runtime
            .as_ref()
            .map_or_else(KirinAttackStats::default, |runtime| {
                to_c_attack_stats(runtime.stats())
            })
    }

    fn post_attack_history(&self) -> Option<AttackHistory> {
        if self.write_role.lock().ok().and_then(|role| *role) != Some(PluginDataRole::Post) {
            return None;
        }
        self.attack_runtime.as_ref()?.try_history()
    }
}

/// Internal ATTACK DRUM validation switch. Not a public analysis route.
///
/// # Safety
/// `handle` must be null or a live pointer returned by `kirin_hypha_create`.
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_set_internal_attack_enabled(
    handle: *mut KirinHyphaEngine,
    enabled: bool,
) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() {
            return false;
        }
        unsafe { (*handle).set_internal_attack_enabled(enabled) }
    }))
    .unwrap_or(false)
}

/// Copies at most the newest 64 raw ODF frames, oldest first. UI/control thread only.
///
/// # Safety
/// `handle` and `out` must be live writable pointers.
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_poll_internal_attack_batch(
    handle: *mut KirinHyphaEngine,
    out: *mut KirinAttackBatch,
) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() || out.is_null() {
            return false;
        }
        let Some(batch) = (unsafe { &*handle }).poll_internal_attack_batch() else {
            return false;
        };
        unsafe { *out = batch };
        true
    }))
    .unwrap_or(false)
}

/// Copies at most the newest 240 fixed-rule ATTACK events, oldest first.
///
/// # Safety
/// `handle` and `out` must be live writable pointers.
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_poll_internal_attack_events(
    handle: *mut KirinHyphaEngine,
    out: *mut KirinAttackEventBatch,
) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() || out.is_null() {
            return false;
        }
        let Some(batch) = (unsafe { &*handle }).poll_internal_attack_events() else {
            return false;
        };
        unsafe { *out = batch };
        true
    }))
    .unwrap_or(false)
}

/// Copies the exact 10 ms absolute POST waveform envelope for the last six seconds.
///
/// # Safety
/// `handle` and `out` must be live writable pointers.
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_poll_internal_attack_waveform(
    handle: *mut KirinHyphaEngine,
    out: *mut KirinAttackWaveformBatch,
) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() || out.is_null() {
            return false;
        }
        let Some(batch) = (unsafe { &*handle }).poll_internal_attack_waveform() else {
            return false;
        };
        unsafe { *out = batch };
        true
    }))
    .unwrap_or(false)
}

/// Copies event-local waveform shapes and factual perceptual descriptors.
///
/// # Safety
/// `handle` and `out` must be live writable pointers.
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_poll_internal_attack_details(
    handle: *mut KirinHyphaEngine,
    out: *mut KirinAttackDetailBatch,
) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() || out.is_null() {
            return false;
        }
        let Some(batch) = (unsafe { &*handle }).poll_internal_attack_details() else {
            return false;
        };
        unsafe { *out = batch };
        true
    }))
    .unwrap_or(false)
}

/// Reads default-OFF worker counters without enabling the worker.
///
/// # Safety
/// `handle` and `out` must be live writable pointers.
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_internal_attack_stats(
    handle: *mut KirinHyphaEngine,
    out: *mut KirinAttackStats,
) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() || out.is_null() {
            return false;
        }
        unsafe { *out = (*handle).internal_attack_stats() };
        true
    }))
    .unwrap_or(false)
}

#[cfg(test)]
#[path = "attack_ffi_tests.rs"]
mod tests;
