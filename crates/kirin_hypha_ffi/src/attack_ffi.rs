//! Internal-only ATTACK DRUM C ABI.
//!
//! This deliberately does not extend `AnalysisViewMode`, publish a GUI route, or persist state.
//! It exposes the already-isolated SuperFlux worker to validation builds while the shipping
//! default remains OFF.

use std::panic::{catch_unwind, AssertUnwindSafe};

use kirin_measure::{
    AttackEvent, AttackHistory, AttackOdfFrame, AttackRuntimeStats, PluginDataRole,
};

use super::KirinHyphaEngine;

pub const KIRIN_ATTACK_BATCH_CAPACITY: usize = 64;
pub const KIRIN_ATTACK_EVENT_BATCH_CAPACITY: usize = 64;

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

    pub fn internal_attack_stats(&self) -> KirinAttackStats {
        self.attack_runtime
            .as_ref()
            .map_or_else(KirinAttackStats::default, |runtime| {
                to_c_attack_stats(runtime.stats())
            })
    }
}

fn to_c_attack_frame(frame: &AttackOdfFrame) -> KirinAttackOdfFrame {
    KirinAttackOdfFrame {
        generation: frame.generation,
        sample_rate: frame.sample_rate,
        channels: frame.channels,
        reserved: [0; 3],
        definition_hash: frame.definition_hash,
        window_samples: frame.window_samples,
        hop_samples: frame.hop_samples,
        support_start_samples: frame.support_start_samples,
        support_end_samples: frame.support_end_samples,
        event_sample: frame.event_sample,
        value: frame.value,
    }
}

fn to_c_attack_batch(history: &AttackHistory) -> KirinAttackBatch {
    let mut batch = KirinAttackBatch::default();
    let skip = history
        .frames()
        .len()
        .saturating_sub(KIRIN_ATTACK_BATCH_CAPACITY);
    for (destination, source) in batch.frames.iter_mut().zip(history.frames().skip(skip)) {
        *destination = to_c_attack_frame(source);
        batch.count += 1;
    }
    batch
}

fn to_c_attack_stats(stats: AttackRuntimeStats) -> KirinAttackStats {
    KirinAttackStats {
        available: 1,
        enabled: stats.enabled as u8,
        worker_running: stats.worker_running as u8,
        channels: stats.channels,
        reserved: [0; 4],
        pushed_blocks: stats.pushed_blocks,
        dropped_blocks: stats.dropped_blocks,
        analyzed_frames: stats.analyzed_frames,
    }
}

fn to_c_attack_event(event: &AttackEvent) -> KirinAttackEvent {
    KirinAttackEvent {
        generation: event.generation,
        sample_rate: event.sample_rate,
        channels: event.channels,
        reserved: [0; 3],
        definition_hash: event.definition_hash,
        event_sample: event.event_sample,
        decision_sample: event.decision_sample,
        value: event.value,
    }
}

fn to_c_attack_event_batch(history: &AttackHistory) -> KirinAttackEventBatch {
    let mut batch = KirinAttackEventBatch::default();
    let skip = history
        .events()
        .len()
        .saturating_sub(KIRIN_ATTACK_EVENT_BATCH_CAPACITY);
    for (destination, source) in batch.events.iter_mut().zip(history.events().skip(skip)) {
        *destination = to_c_attack_event(source);
        batch.count += 1;
    }
    batch
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

/// Copies at most the newest 64 fixed-rule ATTACK events, oldest first.
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
mod tests {
    use std::mem::{offset_of, size_of};
    use std::thread;
    use std::time::{Duration, Instant};

    use kirin_measure::{
        CaptureClockSource, PluginDataRole, PresentationLatencySamples, PresentationLatencySource,
    };

    use super::*;

    #[test]
    fn attack_c_layout_is_fixed_without_changing_existing_abi() {
        assert_eq!(size_of::<KirinAttackOdfFrame>(), 88);
        assert_eq!(offset_of!(KirinAttackOdfFrame, definition_hash), 16);
        assert_eq!(offset_of!(KirinAttackOdfFrame, support_start_samples), 56);
        assert_eq!(offset_of!(KirinAttackOdfFrame, value), 80);
        assert_eq!(size_of::<KirinAttackBatch>(), 5_640);
        assert_eq!(offset_of!(KirinAttackBatch, frames), 8);
        assert_eq!(size_of::<KirinAttackEvent>(), 72);
        assert_eq!(offset_of!(KirinAttackEvent, event_sample), 48);
        assert_eq!(offset_of!(KirinAttackEvent, value), 64);
        assert_eq!(size_of::<KirinAttackEventBatch>(), 4_616);
        assert_eq!(size_of::<KirinAttackStats>(), 32);
    }

    #[test]
    fn default_is_off_and_only_post_can_enable_internal_attack() {
        let engine = KirinHyphaEngine::new(48_000, 2);
        assert_eq!(
            engine.internal_attack_stats(),
            KirinAttackStats {
                available: 1,
                channels: 2,
                ..KirinAttackStats::default()
            }
        );
        assert!(!engine.set_internal_attack_enabled(true));
        *engine.write_role.lock().unwrap() = Some(PluginDataRole::Pre);
        assert!(!engine.set_internal_attack_enabled(true));
        *engine.write_role.lock().unwrap() = Some(PluginDataRole::Post);
        assert!(engine.set_internal_attack_enabled(true));
        assert_eq!(engine.internal_attack_stats().enabled, 1);
        assert!(engine.set_internal_attack_enabled(false));
        assert_eq!(engine.internal_attack_stats().enabled, 0);
    }

    #[test]
    fn unsupported_host_rate_stays_unavailable_without_failing_engine() {
        let engine = KirinHyphaEngine::new(12_345, 2);
        *engine.write_role.lock().unwrap() = Some(PluginDataRole::Post);
        assert_eq!(engine.internal_attack_stats().available, 0);
        assert!(!engine.set_internal_attack_enabled(true));
        assert!(engine.poll_internal_attack_batch().is_none());
    }

    #[test]
    fn shipping_vst_clock_and_audio_transaction_reaches_attack_worker() {
        let engine = KirinHyphaEngine::new(48_000, 2);
        *engine.write_role.lock().unwrap() = Some(PluginDataRole::Post);
        assert!(engine.set_internal_attack_enabled(true));

        let mut position = 0_i64;
        for block_index in 0..24 {
            let mut block = vec![0.0_f32; 256 * 2];
            if block_index == 8 {
                block[0] = 1.0;
                block[1] = 1.0;
            }
            engine.note_capture_window_with_presentation(
                true,
                position,
                256,
                CaptureClockSource::ProjectTimeline,
                PresentationLatencySamples {
                    source: PresentationLatencySource::Vst3,
                    input: Some(0),
                    output: Some(0),
                },
                false,
            );
            assert!(engine.push_samples_transaction(&block, 2));
            position += 256;
        }

        let deadline = Instant::now() + Duration::from_secs(2);
        while engine
            .poll_internal_attack_events()
            .is_none_or(|batch| batch.count == 0)
            && Instant::now() < deadline
        {
            thread::sleep(Duration::from_millis(5));
        }
        let batch = engine.poll_internal_attack_batch().unwrap();
        assert!(batch.count > 0);
        assert!(batch.count as usize <= KIRIN_ATTACK_BATCH_CAPACITY);
        let frames = &batch.frames[..batch.count as usize];
        assert!(frames.iter().all(|frame| frame.sample_rate == 48_000));
        assert!(frames.iter().all(|frame| frame.channels == 2));
        assert!(frames.iter().all(|frame| frame.window_samples == 2_048));
        assert!(frames.iter().all(|frame| frame.hop_samples == 256));
        assert!(frames.iter().any(|frame| frame.value > 0.0));
        let events = engine.poll_internal_attack_events().unwrap();
        assert!(events.count > 0);
        assert!(events.events[..events.count as usize]
            .iter()
            .all(|event| event.decision_sample > event.event_sample));
    }

    #[test]
    fn c_functions_are_null_safe() {
        let mut stats = KirinAttackStats::default();
        let mut batch = KirinAttackBatch::default();
        let mut events = KirinAttackEventBatch::default();
        unsafe {
            assert!(!kirin_hypha_set_internal_attack_enabled(
                std::ptr::null_mut(),
                true
            ));
            assert!(!kirin_hypha_internal_attack_stats(
                std::ptr::null_mut(),
                &mut stats
            ));
            assert!(!kirin_hypha_poll_internal_attack_batch(
                std::ptr::null_mut(),
                &mut batch
            ));
            assert!(!kirin_hypha_poll_internal_attack_events(
                std::ptr::null_mut(),
                &mut events
            ));
        }
    }
}
