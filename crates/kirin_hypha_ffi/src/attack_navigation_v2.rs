//! Coherent ALL navigation: opaque producer keys and waveform are one authority-qualified read.
use crate::attack_snapshot_authority::AttackSnapshotAuthority;
use kirin_measure::attack_runtime::snapshot::{
    AttackObservationReadError, AttackObservationSnapshot, AttackSourceKey,
};
use kirin_measure::spectrum_exchange::{AttackMappingProof, AttackObservationView};
use kirin_measure::{AttackHistory, AttackWaveformPoint};
fn source_key(s: AttackSourceKey) -> KirinSnapshotSourceKey {
    KirinSnapshotSourceKey {
        incarnation: s.incarnation,
        generation: s.generation,
        sample_rate: s.sample_rate,
        channels: s.channels,
        odf_hash: s.odf_hash,
        reserved: [0; 3],
    }
}
use crate::snapshot_types::*;
use crate::{KirinAttackWaveformBatch, KirinHyphaEngine};
use kirin_measure::{PluginDataRole, SpectrumViewStatus};
use std::panic::{catch_unwind, AssertUnwindSafe};

pub const KIRIN_ATTACK_NAVIGATION_VERSION: u32 = 2;
pub const KIRIN_ATTACK_NAVIGATION_CAPACITY: usize = 240;
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct KirinAttackNavigationRequestV2 {
    pub version: u32,
    pub struct_size: u32,
    pub target: u8,
    pub reserved: [u8; 7],
}
#[repr(C)]
#[derive(Clone, Copy)]
pub struct KirinAttackNavigationV2 {
    pub header: KirinSnapshotHeader,
    pub count: u32,
    pub capacity: u32,
    pub events: [KirinSnapshotEventKey; KIRIN_ATTACK_NAVIGATION_CAPACITY],
    pub pair_kind: [u8; KIRIN_ATTACK_NAVIGATION_CAPACITY],
    pub post: KirinAttackWaveformBatch,
    pub pre: KirinAttackWaveformBatch,
}
impl Default for KirinAttackNavigationV2 {
    fn default() -> Self {
        Self {
            header: Default::default(),
            count: 0,
            capacity: KIRIN_ATTACK_NAVIGATION_CAPACITY as u32,
            events: [Default::default(); KIRIN_ATTACK_NAVIGATION_CAPACITY],
            pair_kind: [4; KIRIN_ATTACK_NAVIGATION_CAPACITY],
            post: Default::default(),
            pre: Default::default(),
        }
    }
}

fn qualified_waveform<'a>(
    points: impl Iterator<Item = &'a AttackWaveformPoint>,
    source: AttackSourceKey,
    cutoff: i64,
    proof: Option<&AttackMappingProof>,
) -> KirinAttackWaveformBatch {
    let mut out = KirinAttackWaveformBatch::default();
    for point in points
        .filter(|p| {
            p.has_valid_layout()
                && p.generation == source.generation
                && p.sample_rate == source.sample_rate
                && p.channels == source.channels
                && p.end_sample <= cutoff
                && proof.is_none_or(|proof| {
                    proof.validates_raw_span(
                        p.start_sample,
                        p.end_sample,
                        source,
                        proof.post.source,
                    )
                })
        })
        .take(out.points.len())
    {
        out.points[out.count as usize] = crate::KirinAttackWaveformPoint {
            generation: point.generation,
            sample_rate: point.sample_rate,
            channels: point.channels,
            reserved: [0; 3],
            start_sample: point.start_sample,
            end_sample: point.end_sample,
            peak_linear: point.peak_linear,
            rms_dbfs: point.rms_dbfs,
        };
        out.count += 1;
    }
    out
}

// Match Single's G1 ledger lookup exactly. Numeric comparison additionally needs Active
// evidence; temporarily unavailable numbers must not replace the producer's opaque keys.
fn ledger_available(
    authority: &AttackSnapshotAuthority,
    view: &AttackObservationView,
    source: AttackSourceKey,
) -> bool {
    authority.matches_view(view) && view.proof.is_some_and(|p| p.post.source == source)
}

fn read_status(error: AttackObservationReadError) -> u8 {
    match error {
        AttackObservationReadError::Busy => KIRIN_SNAPSHOT_BUSY,
        AttackObservationReadError::SourceUnavailable => KIRIN_SNAPSHOT_RETIRED,
    }
}

fn same_binding(a: &AttackObservationView, b: &AttackObservationView) -> bool {
    a.status == b.status
        && a.authority_revision == b.authority_revision
        && a.target_hash == b.target_hash
        && a.origin == b.origin
        && a.mapping_request_id == b.mapping_request_id
        && a.mapping_source_pair == b.mapping_source_pair
        && a.pre.as_ref().and_then(|p| p.source).map(|s| s.source)
            == b.pre.as_ref().and_then(|p| p.source).map(|s| s.source)
        && a.post.as_ref().and_then(|p| p.source).map(|s| s.source)
            == b.post.as_ref().and_then(|p| p.source).map(|s| s.source)
        && match (a.proof, b.proof) {
            (Some(a), Some(b)) => a.binding_token() == b.binding_token(),
            (None, None) => true,
            _ => false,
        }
}

fn assemble_navigation(
    authority: &AttackSnapshotAuthority,
    local: &AttackObservationSnapshot,
    history: &AttackHistory,
    view: &AttackObservationView,
    target: u8,
) -> Result<KirinAttackNavigationV2, u8> {
    let source = local
        .source
        .filter(|s| s.valid())
        .ok_or(KIRIN_SNAPSHOT_RETIRED)?;
    let ledger = ledger_available(authority, view, source.source);
    let paired = ledger
        && view.status == SpectrumViewStatus::Active
        && view.proof.is_some_and(|p| {
            p.authority_revision == authority.pair.generation
                && p.pre.valid()
                && p.post.valid()
                && p.band_semantic_hash == source.band_semantic_hash
                && view
                    .pre
                    .as_ref()
                    .and_then(|pre| pre.source)
                    .is_some_and(|pre| pre.source == p.pre.source)
        });
    let mut out = KirinAttackNavigationV2::default();
    out.header = KirinSnapshotHeader {
        version: 2,
        struct_size: std::mem::size_of_val(&out) as u32,
        kind: 4,
        target: if paired { target } else { KIRIN_TARGET_POST },
        band: 0,
        signal_state: authority.signal,
        snapshot_revision: local.revision,
        authority_revision: authority.pair.generation,
        cutoff_sample: source.cutoff,
        source: source_key(source.source),
        flags: u32::from(paired),
        band_semantic_hash: source.band_semantic_hash,
    };
    let start = source
        .cutoff
        .saturating_sub(i64::from(source.source.sample_rate) * 6);
    let mut keys: Vec<_> = if ledger {
        let proof = view.proof.unwrap();
        if view
            .events
            .iter()
            .any(|e| e.source != proof.pre.source && e.source != proof.post.source)
        {
            return Err(KIRIN_SNAPSHOT_BUSY);
        }
        view.events
            .iter()
            .filter(|e| (start..=source.cutoff).contains(&e.event_sample))
            .map(|e| {
                (
                    KirinSnapshotEventKey {
                        source: source_key(e.source),
                        event_sample: e.event_sample,
                        token: e.token,
                    },
                    e.pair.kind as u8,
                )
            })
            .collect()
    } else {
        history
            .events()
            .filter(|e| {
                source.source.matches(e) && (start..=source.cutoff).contains(&e.event_sample)
            })
            .map(|e| {
                (
                    KirinSnapshotEventKey {
                        source: source_key(source.source),
                        event_sample: e.event_sample,
                        token: e.event_sample as u64,
                    },
                    4,
                )
            })
            .collect()
    };
    keys.sort_by_key(|(e, _)| (e.event_sample, e.token));
    if keys.len() > KIRIN_ATTACK_NAVIGATION_CAPACITY {
        return Err(KIRIN_SNAPSHOT_UNSUPPORTED);
    }
    for (index, (key, kind)) in keys.into_iter().enumerate() {
        out.events[index] = key;
        out.pair_kind[index] = kind;
        out.count += 1;
    }
    out.post = qualified_waveform(history.waveform(), source.source, source.cutoff, None);
    if paired {
        if let Some(pre) = &view.pre_history {
            let proof = view.proof.as_ref().unwrap();
            out.pre =
                qualified_waveform(pre.waveform(), proof.pre.source, source.cutoff, Some(proof));
        }
    }
    Ok(out)
}

impl KirinHyphaEngine {
    pub fn attack_navigation_v2(
        &self,
        request: KirinAttackNavigationRequestV2,
    ) -> Result<KirinAttackNavigationV2, u8> {
        if request.version != 2
            || request.struct_size as usize != std::mem::size_of_val(&request)
            || request.target > KIRIN_TARGET_DELTA
            || request.reserved != [0; 7]
        {
            return Err(KIRIN_SNAPSHOT_INVALID_REQUEST);
        }
        let authority = AttackSnapshotAuthority::read(self).ok_or(KIRIN_SNAPSHOT_BUSY)?;
        if authority.role != Some(PluginDataRole::Post) {
            return Err(KIRIN_SNAPSHOT_UNSUPPORTED);
        }
        let runtime = self
            .attack_runtime
            .as_ref()
            .ok_or(KIRIN_SNAPSHOT_UNSUPPORTED)?;
        let local = runtime
            .try_navigation_snapshot_result()
            .map_err(read_status)?;
        let source = local
            .source
            .filter(|s| s.valid())
            .ok_or(KIRIN_SNAPSHOT_RETIRED)?;
        let history = runtime.try_history().ok_or(KIRIN_SNAPSHOT_BUSY)?;
        let view = self
            .spectrum
            .try_attack_observation_view()
            .ok_or(KIRIN_SNAPSHOT_BUSY)?;
        let out = assemble_navigation(&authority, &local, &history, &view, request.target)?;
        let after = runtime
            .try_navigation_snapshot_result()
            .map_err(read_status)?;
        let after_view = self
            .spectrum
            .try_attack_observation_view()
            .ok_or(KIRIN_SNAPSHOT_BUSY)?;
        if after.source.is_none_or(|s| {
            s.source != source.source || s.band_semantic_hash != source.band_semantic_hash
        }) {
            return Err(KIRIN_SNAPSHOT_RETIRED);
        }
        if AttackSnapshotAuthority::read(self).as_ref() != Some(&authority)
            || !same_binding(&view, &after_view)
        {
            return Err(KIRIN_SNAPSHOT_BUSY);
        }
        Ok(out)
    }
}
/// # Safety
/// Live handle; request prefix and declared output storage must be readable/writable and aligned.
/// Unknown versions and every failure preserve all output bytes.
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_poll_attack_navigation_v2(
    handle: *const KirinHyphaEngine,
    request_size: u32,
    request: *const KirinAttackNavigationRequestV2,
    out_size: u32,
    out: *mut KirinAttackNavigationV2,
) -> u8 {
    catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null()
            || request.is_null()
            || out.is_null()
            || request_size < 4
            || !(request as usize)
                .is_multiple_of(std::mem::align_of::<KirinAttackNavigationRequestV2>())
            || !(out as usize).is_multiple_of(std::mem::align_of::<KirinAttackNavigationV2>())
        {
            return KIRIN_SNAPSHOT_INVALID_REQUEST;
        }
        if unsafe { request.cast::<u32>().read() } != 2 {
            return KIRIN_SNAPSHOT_UNSUPPORTED;
        }
        if request_size as usize != std::mem::size_of::<KirinAttackNavigationRequestV2>()
            || (out_size as usize) < std::mem::size_of::<KirinAttackNavigationV2>()
        {
            return KIRIN_SNAPSHOT_INVALID_REQUEST;
        }
        match unsafe { &*handle }.attack_navigation_v2(unsafe { request.read() }) {
            Ok(packet) => unsafe { commit_sized(out, out_size, packet) },
            Err(status) => status,
        }
    }))
    .unwrap_or(KIRIN_SNAPSHOT_BUSY)
}

#[cfg(test)]
#[path = "attack_navigation_v2_tests.rs"]
mod tests;
