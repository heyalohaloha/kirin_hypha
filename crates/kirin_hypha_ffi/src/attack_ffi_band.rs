//! DRUM band C ABI (B-1096): the chosen band and, per hit, PRE's and POST's band measures.
//!
//! A band is chosen on POST only. It rides the pair request to PRE, so both sides measure the
//! same band at the same onsets; nothing is measured while no band is chosen.

use std::panic::{catch_unwind, AssertUnwindSafe};

use kirin_measure::attack_perception::band::{
    AttackBand, AttackBandMeasure, ATTACK_BAND_HEAD_POINTS, ATTACK_BAND_HISTORY_CAPACITY,
    ATTACK_BAND_TAIL_POINTS,
};
use kirin_measure::{AttackPairViewSnapshot, PluginDataRole, SpectrumViewStatus};

use super::KirinHyphaEngine;

pub const KIRIN_ATTACK_BAND_BATCH_CAPACITY: usize = ATTACK_BAND_HISTORY_CAPACITY;
pub const KIRIN_ATTACK_BAND_HEAD_POINTS: usize = ATTACK_BAND_HEAD_POINTS;
pub const KIRIN_ATTACK_BAND_TAIL_POINTS: usize = ATTACK_BAND_TAIL_POINTS;

/// One side of one hit. Times are milliseconds from the onset; a value whose `*_available` is
/// 0 was not in the audio and reads as unavailable, never as 0.
#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct KirinAttackBandSide {
    pub available: u8,
    pub arrival_available: u8,
    pub attack_available: u8,
    pub release_available: u8,
    pub reserved: [u8; 4],
    pub span_end_sample: i64,
    pub peak_ms: f32,
    pub arrival_ms: f32,
    pub attack_ms: f32,
    pub release_ms: f32,
    pub level_dbfs: f32,
    pub reserved2: f32,
    /// dBFS envelope over [onset - 20 ms, onset + 40 ms).
    pub head_dbfs: [f32; KIRIN_ATTACK_BAND_HEAD_POINTS],
    /// dBFS envelope over [onset, onset + 300 ms); points past `span_end_sample` sit on the floor.
    pub tail_dbfs: [f32; KIRIN_ATTACK_BAND_TAIL_POINTS],
}

impl Default for KirinAttackBandSide {
    fn default() -> Self {
        Self {
            available: 0,
            arrival_available: 0,
            attack_available: 0,
            release_available: 0,
            reserved: [0; 4],
            span_end_sample: 0,
            peak_ms: 0.0,
            arrival_ms: 0.0,
            attack_ms: 0.0,
            release_ms: 0.0,
            level_dbfs: 0.0,
            reserved2: 0.0,
            head_dbfs: [0.0; KIRIN_ATTACK_BAND_HEAD_POINTS],
            tail_dbfs: [0.0; KIRIN_ATTACK_BAND_TAIL_POINTS],
        }
    }
}

/// kind: 0 = matched (PRE and POST at the PRE onset), 2 = POST only.
#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct KirinAttackBandHit {
    pub generation: u64,
    pub sample_rate: u32,
    pub channels: u8,
    pub band: u8,
    pub kind: u8,
    pub delay_available: u8,
    pub event_sample: i64,
    /// The band's time resolution: one period of its centre frequency.
    pub resolution_micros: u32,
    /// POST minus PRE arrival, ms.
    pub delay_ms: f32,
    pub pre: KirinAttackBandSide,
    pub post: KirinAttackBandSide,
}

impl Default for KirinAttackBandHit {
    fn default() -> Self {
        Self {
            generation: 0,
            sample_rate: 0,
            channels: 0,
            band: 0,
            kind: 0,
            delay_available: 0,
            event_sample: 0,
            resolution_micros: 0,
            delay_ms: 0.0,
            pre: KirinAttackBandSide::default(),
            post: KirinAttackBandSide::default(),
        }
    }
}

/// `status` uses the pair view's vocabulary. `band` is the chosen band (0 = none).
/// `pre_band_available` is 1 once PRE's snapshot declares the same band: a PRE that predates
/// bands never does, and the hits then stay POST only.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct KirinAttackBandBatch {
    pub status: u8,
    pub band: u8,
    pub pre_band_available: u8,
    pub reserved: u8,
    pub count: u32,
    pub capacity: u32,
    pub reserved2: u32,
    pub hits: [KirinAttackBandHit; KIRIN_ATTACK_BAND_BATCH_CAPACITY],
}

impl Default for KirinAttackBandBatch {
    fn default() -> Self {
        Self {
            status: 0,
            band: 0,
            pre_band_available: 0,
            reserved: 0,
            count: 0,
            capacity: KIRIN_ATTACK_BAND_BATCH_CAPACITY as u32,
            reserved2: 0,
            hits: [KirinAttackBandHit::default(); KIRIN_ATTACK_BAND_BATCH_CAPACITY],
        }
    }
}

fn ms(frames: f32, sample_rate: u32) -> f32 {
    frames * 1_000.0 / sample_rate as f32
}

fn to_c_band_side(measure: &AttackBandMeasure) -> KirinAttackBandSide {
    let rate = measure.sample_rate;
    KirinAttackBandSide {
        available: 1,
        arrival_available: measure.arrival_frames.is_some() as u8,
        attack_available: measure.attack_frames.is_some() as u8,
        release_available: measure.release_frames.is_some() as u8,
        reserved: [0; 4],
        span_end_sample: measure.span_end_sample,
        peak_ms: ms(measure.peak_frames, rate),
        arrival_ms: measure
            .arrival_frames
            .map_or(0.0, |frames| ms(frames, rate)),
        attack_ms: measure.attack_frames.map_or(0.0, |frames| ms(frames, rate)),
        release_ms: measure
            .release_frames
            .map_or(0.0, |frames| ms(frames, rate)),
        level_dbfs: measure.level_dbfs,
        reserved2: 0.0,
        head_dbfs: measure.head_dbfs,
        tail_dbfs: measure.tail_dbfs,
    }
}

impl KirinHyphaEngine {
    /// POST only. 0 chooses no band; 1 to 8 choose 63 Hz to 8 kHz. It is not persisted.
    pub fn set_attack_band(&self, band: u8) -> bool {
        if self.write_role.lock().ok().and_then(|role| *role) != Some(PluginDataRole::Post) {
            return false;
        }
        let chosen = match band {
            0 => None,
            index => Some(match AttackBand::from_index(index) {
                Some(band) => band,
                None => return false,
            }),
        };
        let Some(runtime) = self.attack_runtime.as_ref() else {
            return false;
        };
        runtime.set_band(chosen);
        self.spectrum.set_post_attack_band(chosen);
        true
    }

    pub fn attack_band(&self) -> u8 {
        self.attack_runtime
            .as_ref()
            .and_then(|runtime| runtime.band())
            .map_or(0, AttackBand::index)
    }

    /// The chosen band's hits: matched pairs from the pair view, and POST's own hits for every
    /// other onset. `None` while the pair view is being replaced.
    pub fn poll_attack_band(&self) -> Option<KirinAttackBandBatch> {
        if self.write_role.lock().ok().and_then(|role| *role) != Some(PluginDataRole::Post) {
            return None;
        }
        let runtime = self.attack_runtime.as_ref()?;
        let view = self.attack_pair_view()?;
        let mut batch = KirinAttackBandBatch {
            status: view.status as u8,
            ..Default::default()
        };
        let Some(band) = runtime.band() else {
            return Some(batch);
        };
        batch.band = band.index();
        batch.pre_band_available = (view.pre_band == Some(band)) as u8;
        let history = runtime.try_history()?;
        let Some(identity) = history.newest() else {
            return Some(batch);
        };
        let matched = band_pair_hits(&view, band, identity.generation);
        let mut hits = matched.clone();
        hits.extend(
            history
                .band_details()
                .filter(|detail| detail.measure.band == band)
                .filter(|detail| {
                    matched
                        .iter()
                        .all(|hit| hit.event_sample != detail.event.event_sample)
                })
                .map(|detail| KirinAttackBandHit {
                    generation: detail.event.generation,
                    sample_rate: detail.event.sample_rate,
                    channels: detail.event.channels,
                    band: band.index(),
                    kind: 2,
                    delay_available: 0,
                    event_sample: detail.event.event_sample,
                    resolution_micros: band.resolution_micros(),
                    delay_ms: 0.0,
                    pre: KirinAttackBandSide::default(),
                    post: to_c_band_side(&detail.measure),
                }),
        );
        hits.sort_by_key(|hit| hit.event_sample);
        let skip = hits.len().saturating_sub(KIRIN_ATTACK_BAND_BATCH_CAPACITY);
        for (destination, source) in batch.hits.iter_mut().zip(hits.iter().skip(skip)) {
            *destination = *source;
            batch.count += 1;
        }
        Some(batch)
    }
}

fn band_pair_hits(
    view: &AttackPairViewSnapshot,
    band: AttackBand,
    generation: u64,
) -> Vec<KirinAttackBandHit> {
    if view.status != SpectrumViewStatus::Active || view.band != Some(band) {
        return Vec::new();
    }
    view.band_pairs
        .iter()
        .map(|pair| KirinAttackBandHit {
            generation,
            sample_rate: pair.pre.sample_rate,
            channels: pair.pre.channels,
            band: band.index(),
            kind: 0,
            delay_available: pair.delay_frames.is_some() as u8,
            event_sample: pair.event_sample,
            resolution_micros: band.resolution_micros(),
            delay_ms: pair
                .delay_frames
                .map_or(0.0, |frames| ms(frames, pair.pre.sample_rate)),
            pre: to_c_band_side(&pair.pre),
            post: pair.post.as_ref().map(to_c_band_side).unwrap_or_default(),
        })
        .collect()
}

/// Chooses the DRUM band. POST only; 0 is no band. UI/control thread only.
///
/// # Safety
/// `handle` must be null or a live pointer returned by `kirin_hypha_create`.
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_set_attack_band(
    handle: *mut KirinHyphaEngine,
    band: u8,
) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() {
            return false;
        }
        unsafe { (*handle).set_attack_band(band) }
    }))
    .unwrap_or(false)
}

/// Copies the chosen band's newest 64 hits, oldest first. UI/control thread only.
///
/// # Safety
/// `handle` and `out` must be live writable pointers.
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_poll_attack_band(
    handle: *mut KirinHyphaEngine,
    out: *mut KirinAttackBandBatch,
) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() || out.is_null() {
            return false;
        }
        let Some(batch) = (unsafe { &*handle }).poll_attack_band() else {
            return false;
        };
        unsafe { *out = batch };
        true
    }))
    .unwrap_or(false)
}

#[cfg(test)]
#[path = "attack_ffi_band_tests.rs"]
mod tests;
