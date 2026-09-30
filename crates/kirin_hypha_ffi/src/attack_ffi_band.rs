//! DRUM band C ABI (B-1096, B-1098): the chosen band, and for every hit the lanes show, PRE's
//! and POST's outcome in it.
//!
//! The hits are exactly the lanes' hits, keyed by the same `event_sample`: the pair events while
//! a pair is active (as `kirin_hypha_poll_attack_pair_events` gives them), POST's own details
//! otherwise (as `kirin_hypha_poll_attack_details`). One function maps a hit to its band sides,
//! and both polls use it, so the band lanes cannot have other columns than the whole-signal
//! lanes, and the envelope of a hit is the one its values came from. Nothing here measures: the
//! ATTACK workers did, once per hit.

use std::panic::{catch_unwind, AssertUnwindSafe};

use kirin_measure::attack_perception::band::{
    AttackBand, ATTACK_BAND_HEAD_POINTS, ATTACK_BAND_TAIL_POINTS,
};
use kirin_measure::attack_runtime::AttackPreBand;
use kirin_measure::{AttackOdfFrame, AttackPairViewSnapshot, PluginDataRole, SpectrumViewStatus};

use super::{KirinHyphaEngine, KIRIN_ATTACK_PAIR_EVENT_BATCH_CAPACITY};

pub const KIRIN_ATTACK_BAND_BATCH_CAPACITY: usize = KIRIN_ATTACK_PAIR_EVENT_BATCH_CAPACITY;
pub const KIRIN_ATTACK_BAND_HEAD_POINTS: usize = ATTACK_BAND_HEAD_POINTS;
pub const KIRIN_ATTACK_BAND_TAIL_POINTS: usize = ATTACK_BAND_TAIL_POINTS;

/// A side's outcome in the band.
pub const KIRIN_ATTACK_BAND_SIDE_PENDING: u8 = 0;
pub const KIRIN_ATTACK_BAND_SIDE_RISES: u8 = 1;
pub const KIRIN_ATTACK_BAND_SIDE_RINGS_ON: u8 = 2;
pub const KIRIN_ATTACK_BAND_SIDE_SILENT: u8 = 3;
pub const KIRIN_ATTACK_BAND_SIDE_NOT_KEPT: u8 = 4;
pub const KIRIN_ATTACK_BAND_SIDE_ABSENT: u8 = 5;
pub const KIRIN_ATTACK_BAND_ARRIVAL_AT: u8 = 0;
pub const KIRIN_ATTACK_BAND_ARRIVAL_RINGING: u8 = 1;
pub const KIRIN_ATTACK_BAND_RELEASE_AT: u8 = 0;
pub const KIRIN_ATTACK_BAND_RELEASE_NEXT_HIT: u8 = 1;
pub const KIRIN_ATTACK_BAND_RELEASE_AT_LEAST: u8 = 2;
/// Whether PRE's side of the chosen band is there.
pub const KIRIN_ATTACK_BAND_PRE_OFF: u8 = 0;
pub const KIRIN_ATTACK_BAND_PRE_SAME: u8 = 1;
pub const KIRIN_ATTACK_BAND_PRE_WAITING: u8 = 2;
pub const KIRIN_ATTACK_BAND_PRE_PREDATES: u8 = 3;
/// A hit's kind: the pair event kinds 0 to 3, and 4 for POST's own hit while no pair is active.
pub const KIRIN_ATTACK_BAND_KIND_POST_ALONE: u8 = 4;

/// One side of one hit. Times are ms from `measured_at_sample`. `state` says which values mean
/// anything: RISES all of them (as `arrival_state` and `release_state` qualify), RINGS_ON and
/// SILENT only `level_dbfs` and `peak_ms`, the others none.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct KirinAttackBandSide {
    pub state: u8,
    pub arrival_state: u8,
    pub release_state: u8,
    pub reserved: u8,
    pub peak_ms: f32,
    pub arrival_ms: f32,
    pub attack_ms: f32,
    /// The release, or its lower bound when `release_state` is AT_LEAST.
    pub release_ms: f32,
    pub level_dbfs: f32,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct KirinAttackBandHit {
    /// The lanes' key for this hit: the pair event's, or POST's own onset.
    pub event_sample: i64,
    /// Where both sides were measured: the PRE onset for a matched pair.
    pub measured_at_sample: i64,
    pub kind: u8,
    pub reserved: [u8; 7],
    pub pre: KirinAttackBandSide,
    pub post: KirinAttackBandSide,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct KirinAttackBandBatch {
    /// The pair view's status vocabulary.
    pub status: u8,
    /// The band the hits are for; 0 = none chosen.
    pub band: u8,
    pub pre_band: u8,
    pub reserved: u8,
    pub count: u32,
    pub capacity: u32,
    /// The band's time resolution: one period of its centre.
    pub resolution_micros: u32,
    /// The run the hits belong to, as the lanes' other batches carry it.
    pub generation: u64,
    pub sample_rate: u32,
    pub reserved2: u32,
    pub hits: [KirinAttackBandHit; KIRIN_ATTACK_BAND_BATCH_CAPACITY],
}

impl Default for KirinAttackBandBatch {
    fn default() -> Self {
        Self {
            status: 0,
            band: 0,
            pre_band: KIRIN_ATTACK_BAND_PRE_OFF,
            reserved: 0,
            count: 0,
            capacity: KIRIN_ATTACK_BAND_BATCH_CAPACITY as u32,
            resolution_micros: 0,
            generation: 0,
            sample_rate: 0,
            reserved2: 0,
            hits: [KirinAttackBandHit::default(); KIRIN_ATTACK_BAND_BATCH_CAPACITY],
        }
    }
}

/// dBFS points over [measured_at - 20 ms, + 40 ms) and [measured_at, + 300 ms).
#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct KirinAttackBandEnvelope {
    pub head_dbfs: [f32; KIRIN_ATTACK_BAND_HEAD_POINTS],
    pub tail_dbfs: [f32; KIRIN_ATTACK_BAND_TAIL_POINTS],
}

impl Default for KirinAttackBandEnvelope {
    fn default() -> Self {
        Self {
            head_dbfs: [0.0; KIRIN_ATTACK_BAND_HEAD_POINTS],
            tail_dbfs: [0.0; KIRIN_ATTACK_BAND_TAIL_POINTS],
        }
    }
}

/// One hit with its envelopes: the same record the batch carries for it, and the band both are
/// for. An envelope means something when its side's state is RISES, RINGS_ON or SILENT.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct KirinAttackBandHitEnvelope {
    pub hit: KirinAttackBandHit,
    pub band: u8,
    pub reserved: [u8; 7],
    pub pre: KirinAttackBandEnvelope,
    pub post: KirinAttackBandEnvelope,
}

#[path = "attack_ffi_band_map.rs"]
mod map;
use map::{pre_band_code, sources, status_code, to_c_envelope, Source};

#[path = "attack_ffi_band_summary.rs"]
mod summary;
pub use summary::{
    KirinAttackBandLaneSummary, KirinAttackBandSummary, KIRIN_ATTACK_BAND_HELD_LONG_TAIL,
    KIRIN_ATTACK_BAND_HELD_NEXT_HIT, KIRIN_ATTACK_BAND_HELD_NONE, KIRIN_ATTACK_BAND_HELD_RINGING,
    KIRIN_ATTACK_BAND_LANE_NONE, KIRIN_ATTACK_BAND_LANE_VALUE, KIRIN_ATTACK_BAND_LANE_WITHIN,
    KIRIN_ATTACK_BAND_LEVEL_WITHIN_DB, KIRIN_ATTACK_BAND_SUMMARY_HITS,
};

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

    /// Runs `read` over the lanes' hits in the chosen band, with the identity of the run they
    /// belong to. `None` while the view or history is being replaced: the editor keeps what it
    /// had.
    fn with_band_sources<R>(
        &self,
        read: impl FnOnce(
            &AttackPairViewSnapshot,
            Option<AttackBand>,
            Option<&AttackOdfFrame>,
            &[Source<'_>],
        ) -> R,
    ) -> Option<R> {
        if self.write_role.lock().ok().and_then(|role| *role) != Some(PluginDataRole::Post) {
            return None;
        }
        let runtime = self.attack_runtime.as_ref()?;
        let band = runtime.band();
        let results = runtime.band_results();
        let active = self
            .spectrum
            .with_attack_view(|view| view.status == SpectrumViewStatus::Active)?;
        let history = if active {
            None
        } else {
            Some(runtime.try_history()?)
        };
        self.spectrum
            .with_attack_view(|view| {
                // The pair came or went between the two reads: keep what the editor has rather
                // than show an empty band for one poll.
                if (view.status == SpectrumViewStatus::Active) != active {
                    return None;
                }
                let identity = match history.as_ref() {
                    Some(history) => history.newest(),
                    None => view.post.as_ref().and_then(|post| post.newest()),
                };
                let hits = band
                    .map(|band| sources(view, history.as_ref(), &results, band))
                    .unwrap_or_default();
                Some(read(view, band, identity, &hits))
            })
            .flatten()
    }

    pub fn poll_attack_band(&self) -> Option<KirinAttackBandBatch> {
        self.with_band_sources(|view, band, identity, hits| {
            let mut batch = KirinAttackBandBatch {
                status: status_code(view.status),
                band: band.map_or(0, AttackBand::index),
                pre_band: if view.status == SpectrumViewStatus::Active {
                    pre_band_code(view.pre_band)
                } else {
                    KIRIN_ATTACK_BAND_PRE_OFF
                },
                resolution_micros: band.map_or(0, AttackBand::resolution_micros),
                generation: identity.map_or(0, |identity| identity.generation),
                sample_rate: identity.map_or(0, |identity| identity.sample_rate),
                ..Default::default()
            };
            for (destination, source) in batch.hits.iter_mut().zip(hits) {
                *destination = source.hit;
                batch.count += 1;
            }
            batch
        })
    }

    /// The recent hits that rise in the chosen band, summed up (`attack_ffi_band_summary.rs`).
    pub fn poll_attack_band_summary(&self) -> Option<KirinAttackBandSummary> {
        self.with_band_sources(|view, band, identity, hits| {
            let active = view.status == SpectrumViewStatus::Active;
            let mut summary = KirinAttackBandSummary {
                status: status_code(view.status),
                band: band.map_or(0, AttackBand::index),
                pre_band: if active {
                    pre_band_code(view.pre_band)
                } else {
                    KIRIN_ATTACK_BAND_PRE_OFF
                },
                resolution_micros: band.map_or(0, AttackBand::resolution_micros),
                generation: identity.map_or(0, |identity| identity.generation),
                sample_rate: identity.map_or(0, |identity| identity.sample_rate),
                ..Default::default()
            };
            if band.is_some() {
                // While PRE switches bands the view keeps POST - PRE and waits for it.
                let delta =
                    active && matches!(view.pre_band, AttackPreBand::Same | AttackPreBand::Waiting);
                let resolution_ms = summary.resolution_micros as f32 / 1_000.0;
                summary::summarise(&mut summary, hits, delta, resolution_ms);
            }
            summary
        })
    }

    /// The hit keyed `event_sample` with both envelopes; `None` when it is not a lanes hit now.
    pub fn poll_attack_band_envelope(
        &self,
        event_sample: i64,
    ) -> Option<KirinAttackBandHitEnvelope> {
        self.with_band_sources(|_, band, _, hits| {
            hits.iter()
                .find(|source| source.hit.event_sample == event_sample)
                .map(|source| KirinAttackBandHitEnvelope {
                    hit: source.hit,
                    band: band.map_or(0, AttackBand::index),
                    reserved: [0; 7],
                    pre: to_c_envelope(source.pre),
                    post: to_c_envelope(source.post),
                })
        })
        .flatten()
    }
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

/// Copies the chosen band's outcome for every hit the lanes show, oldest first. UI thread only.
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

/// Copies the summary of the recent hits that rise in the chosen band. UI thread only.
///
/// # Safety
/// `handle` and `out` must be live writable pointers.
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_poll_attack_band_summary(
    handle: *mut KirinHyphaEngine,
    out: *mut KirinAttackBandSummary,
) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() || out.is_null() {
            return false;
        }
        let Some(summary) = (unsafe { &*handle }).poll_attack_band_summary() else {
            return false;
        };
        unsafe { *out = summary };
        true
    }))
    .unwrap_or(false)
}

/// Copies one hit with its PRE and POST band envelopes, for the HEAD / TAIL panes. UI thread
/// only.
///
/// # Safety
/// `handle` and `out` must be live writable pointers.
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_poll_attack_band_envelope(
    handle: *mut KirinHyphaEngine,
    event_sample: i64,
    out: *mut KirinAttackBandHitEnvelope,
) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() || out.is_null() {
            return false;
        }
        let Some(envelope) = (unsafe { &*handle }).poll_attack_band_envelope(event_sample) else {
            return false;
        };
        unsafe { *out = envelope };
        true
    }))
    .unwrap_or(false)
}

#[cfg(test)]
#[path = "attack_ffi_band_tests.rs"]
mod tests;
