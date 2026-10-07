//! Bounded try-read acquisition. All facts are assembled before the single output commit.
use super::aggregate::fixed_cohort;
use super::*;
use crate::attack_snapshot_authority::AttackSnapshotAuthority;
use crate::attack_snapshot_classify::{classify_band_lanes, BandSideEvidence};
use crate::KirinHyphaEngine;
use kirin_measure::attack_perception::band::AttackBand;
use kirin_measure::attack_runtime::snapshot::{
    AttackObservationSnapshot, AttackSourceEvidence, AttackSourceKey,
};
use kirin_measure::spectrum_exchange::{AttackMappingProof, AttackObservationView};
use kirin_measure::{AttackHistory, PluginDataRole, SpectrumViewStatus};
use std::panic::{catch_unwind, AssertUnwindSafe};

pub(crate) fn source_key(source: AttackSourceKey) -> KirinSnapshotSourceKey {
    KirinSnapshotSourceKey {
        incarnation: source.incarnation,
        generation: source.generation,
        sample_rate: source.sample_rate,
        channels: source.channels,
        reserved: [0; 3],
        odf_hash: source.odf_hash,
    }
}

pub(crate) fn proof_revision(proof: &AttackMappingProof) -> u64 {
    u64::from_le_bytes(proof.token()[..8].try_into().expect("fixed proof hash"))
}

fn observation<'a>(
    snapshot: &'a AttackObservationSnapshot,
    onset: i64,
    requested_end: Option<i64>,
    band: AttackBand,
) -> BandSideEvidence<'a> {
    let Some(source) = snapshot.source.filter(|s| s.valid()) else {
        return BandSideEvidence::Missing(KIRIN_REASON_SOURCE_CHANGED);
    };
    if snapshot.band != Some(band) {
        return BandSideEvidence::Missing(KIRIN_REASON_SEMANTICS);
    }
    let entry = match requested_end {
        Some(end) => snapshot.anchored_at(onset, end),
        None => snapshot.own_at(onset),
    };
    if let Some(entry) = entry {
        if !entry.valid()
            || !source.source.matches(&entry.event)
            || entry.measure.is_some_and(|m| m.band != band)
        {
            return BandSideEvidence::Missing(KIRIN_REASON_SOURCE_CHANGED);
        }
        return BandSideEvidence::Observation(entry);
    }
    // Only the producer's explicit pending observation proves an accepted valid request.
    // A newer separately read history may contain an event not yet in this facts packet.
    if onset < source.pcm_start {
        BandSideEvidence::Missing(KIRIN_REASON_NOT_KEPT)
    } else {
        BandSideEvidence::Missing(KIRIN_REASON_WAITING_PUBLICATION)
    }
}

fn envelope(side: BandSideEvidence<'_>) -> Option<SummaryEnvelope> {
    let BandSideEvidence::Observation(observation) = side else {
        return None;
    };
    let measure = observation.measure?;
    Some(SummaryEnvelope {
        head: measure.envelope.head.map(|v| f64::from(v) / 100.0),
        tail: measure.envelope.tail.map(|v| f64::from(v) / 100.0),
        head_valid: observation.head_valid,
        tail_valid: observation.tail_valid,
    })
}

fn measured_end(side: BandSideEvidence<'_>) -> Option<i64> {
    match side {
        BandSideEvidence::Observation(obs) if obs.measure.is_some() => Some(obs.actual_end),
        _ => None,
    }
}

fn scalar_proof(
    proof: Option<&AttackMappingProof>,
    band: AttackBand,
    onset: i64,
    pre: BandSideEvidence<'_>,
    post: BandSideEvidence<'_>,
    local: AttackSourceKey,
) -> Result<u64, u8> {
    let proof = proof.ok_or(KIRIN_REASON_MAPPING)?;
    let end = measured_end(pre)
        .into_iter()
        .chain(measured_end(post))
        .max()
        .unwrap_or(onset.saturating_add(i64::from(local.sample_rate) * 13 / 100));
    if proof.validates_span(band, onset, end, proof.pre.source, local) {
        Ok(super::attack_proof_revision(proof))
    } else {
        Err(KIRIN_REASON_MAPPING)
    }
}

fn masks<const N: usize>(
    proof: Option<&AttackMappingProof>,
    band: AttackBand,
    onset: i64,
    local: AttackSourceKey,
    head: bool,
) -> [u8; N] {
    std::array::from_fn(|i| {
        let rate = i64::from(local.sample_rate);
        // The producer grid rounds microsecond spans to samples, including at 44.1 kHz.
        let frames = |micros: i64| ((rate * micros + 500_000) / 1_000_000).max(1);
        let (lead, span) = if head {
            (frames(20_000), frames(60_000))
        } else {
            (0, frames(300_000))
        };
        let at = onset
            .saturating_sub(lead)
            .saturating_add(span * (2 * i as i64 + 1) / (2 * N as i64));
        u8::from(proof.is_some_and(|proof| {
            proof.validates_span(
                band,
                onset,
                at.saturating_add(1).max(onset.saturating_add(1)),
                proof.pre.source,
                local,
            )
        }))
    })
}

fn paired_events(
    view: &AttackObservationView,
    local: &AttackObservationSnapshot,
    band: AttackBand,
    target: u8,
    source: AttackSourceEvidence,
) -> Vec<SummaryEvent> {
    fixed_cohort(
        &view.events,
        source.cutoff,
        source.source.sample_rate,
        |e| e.event_sample,
    )
    .into_iter()
    .map(|content| {
        let pair = content.pair;
        let pre_onset = pair.pre_event_sample;
        let pre = pre_onset
            .and_then(|at| {
                view.pre
                    .as_ref()
                    .map(|snapshot| observation(snapshot, at, None, band))
            })
            .unwrap_or(BandSideEvidence::Missing(KIRIN_REASON_MAPPING));
        let onset = pre_onset.unwrap_or(content.event_sample);
        let requested_end = match pre {
            BandSideEvidence::Observation(obs) => Some(obs.requested_end),
            _ => None,
        };
        let post = if pre_onset.is_some() {
            requested_end
                .map(|end| observation(local, onset, Some(end), band))
                .unwrap_or(BandSideEvidence::Missing(KIRIN_REASON_MAPPING))
        } else {
            pair.post_event_sample
                .map(|at| observation(local, at, None, band))
                .unwrap_or(BandSideEvidence::Missing(KIRIN_REASON_MAPPING))
        };
        let proof = scalar_proof(view.proof.as_ref(), band, onset, pre, post, source.source);
        SummaryEvent {
            key: KirinSnapshotEventKey {
                source: source_key(content.source),
                event_sample: content.event_sample,
                token: content.token,
            },
            kind: pair.kind as u8,
            lanes: classify_band_lanes(target, band, pre, post, proof),
            pre: envelope(pre),
            post: envelope(post),
            proof_head: masks(view.proof.as_ref(), band, onset, source.source, true),
            proof_tail: masks(view.proof.as_ref(), band, onset, source.source, false),
        }
    })
    .collect()
}

fn local_events(
    history: &AttackHistory,
    local: &AttackObservationSnapshot,
    band: AttackBand,
    target: u8,
    source: AttackSourceEvidence,
) -> Vec<SummaryEvent> {
    let published: Vec<_> = history
        .events()
        .filter(|event| source.source.matches(event))
        .collect();
    fixed_cohort(&published, source.cutoff, source.source.sample_rate, |e| {
        e.event_sample
    })
    .into_iter()
    .map(|event| {
        let post = observation(local, event.event_sample, None, band);
        let pre = BandSideEvidence::Missing(KIRIN_REASON_NO_PAIR);
        SummaryEvent {
            key: KirinSnapshotEventKey {
                source: source_key(source.source),
                event_sample: event.event_sample,
                token: event.event_sample as u64,
            },
            kind: 4,
            lanes: classify_band_lanes(target, band, pre, post, Err(KIRIN_REASON_NO_PAIR)),
            pre: None,
            post: envelope(post),
            proof_head: [0; 96],
            proof_tail: [0; 64],
        }
    })
    .collect()
}

fn same_binding(a: &AttackObservationView, b: &AttackObservationView) -> bool {
    a.authority_revision == b.authority_revision
        && a.target_hash == b.target_hash
        && a.origin == b.origin
        && match (a.proof, b.proof) {
            (Some(a), Some(b)) => {
                a.request_id == b.request_id
                    && a.mapping_epoch == b.mapping_epoch
                    && a.pre.source == b.pre.source
                    && a.post.source == b.post.source
                    && a.band_semantic_hash == b.band_semantic_hash
            }
            (None, None) => true,
            _ => false,
        }
}

impl KirinHyphaEngine {
    pub fn attack_band_summary_v2(
        &self,
        request: KirinAttackBandSummaryV2Request,
    ) -> Result<KirinAttackBandSummaryV2, u8> {
        if !request.is_valid() {
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
        let band = AttackBand::from_index(request.band).ok_or(KIRIN_SNAPSHOT_INVALID_REQUEST)?;
        if runtime.band() != Some(band) {
            return Err(KIRIN_SNAPSHOT_INVALID_REQUEST);
        }
        let local = runtime
            .try_observation_snapshot()
            .ok_or(KIRIN_SNAPSHOT_BUSY)?;
        let source = local
            .source
            .filter(|s| s.valid())
            .ok_or(KIRIN_SNAPSHOT_BUSY)?;
        let history = runtime.try_history().ok_or(KIRIN_SNAPSHOT_BUSY)?;
        let view = self
            .spectrum
            .try_attack_observation_view()
            .ok_or(KIRIN_SNAPSHOT_BUSY)?;
        if view.authority_revision != authority.pair.generation {
            return Err(KIRIN_SNAPSHOT_BUSY);
        }
        let paired = authority.matches_view(&view)
            && view.status == SpectrumViewStatus::Active
            && view.proof.is_some_and(|proof| {
                proof.authority_revision == authority.pair.generation
                    && proof.post.source == source.source
                    && proof.band_semantic_hash == source.band_semantic_hash
                    && view
                        .pre
                        .as_ref()
                        .and_then(|pre| pre.source)
                        .is_some_and(|pre| pre.source == proof.pre.source)
            });
        let events = if paired {
            paired_events(&view, &local, band, request.target, source)
        } else {
            local_events(&history, &local, band, request.target, source)
        };
        let header = KirinSnapshotHeader {
            version: 2,
            struct_size: std::mem::size_of::<KirinAttackBandSummaryV2>() as u32,
            kind: KIRIN_SNAPSHOT_BAND_SUMMARY,
            target: request.target,
            band: request.band,
            signal_state: authority.signal,
            snapshot_revision: local.revision,
            authority_revision: authority.pair.generation,
            cutoff_sample: source.cutoff,
            source: source_key(source.source),
            band_semantic_hash: source.band_semantic_hash,
            flags: 0,
        };
        let mut snapshot =
            assemble_summary(header, &events).ok_or(KIRIN_SNAPSHOT_INVALID_REQUEST)?;
        snapshot.header.snapshot_revision = content_revision(
            &snapshot,
            local.revision,
            view.pre.as_ref().map_or(0, |pre| pre.revision),
        );
        let after = runtime
            .try_observation_snapshot()
            .ok_or(KIRIN_SNAPSHOT_BUSY)?;
        let after_view = self
            .spectrum
            .try_attack_observation_view()
            .ok_or(KIRIN_SNAPSHOT_BUSY)?;
        if after.source.is_none_or(|s| {
            s.source != source.source || s.band_semantic_hash != source.band_semantic_hash
        }) || after.band != Some(band)
            || AttackSnapshotAuthority::read(self).as_ref() != Some(&authority)
            || !same_binding(&view, &after_view)
        {
            return Err(KIRIN_SNAPSHOT_BUSY);
        }
        Ok(snapshot)
    }
}

fn content_revision(
    snapshot: &KirinAttackBandSummaryV2,
    post_revision: u64,
    pre_revision: u64,
) -> u64 {
    use std::hash::{Hash, Hasher};
    let mut hash = std::collections::hash_map::DefaultHasher::new();
    (
        post_revision,
        pre_revision,
        snapshot.header.authority_revision,
        snapshot.header.cutoff_sample,
        snapshot.header.target,
        snapshot.header.band,
        snapshot.header.signal_state,
    )
        .hash(&mut hash);
    for i in 0..snapshot.cohort_count as usize {
        let key = snapshot.events[i];
        (
            key.source.incarnation,
            key.source.generation,
            key.source.sample_rate,
            key.source.channels,
            key.source.odf_hash,
            key.event_sample,
            key.token,
            snapshot.pair_kind[i],
        )
            .hash(&mut hash);
        for value in snapshot.evidence[i] {
            (
                value.measurement_revision,
                value.proof_revision,
                value.class,
                value.reason,
                value.finish,
                value.requested_start,
                value.requested_end,
                value.actual_start,
                value.actual_end,
            )
                .hash(&mut hash);
            (
                value.interval.lower.kind,
                value.interval.lower.closed,
                value.interval.lower.value.to_bits(),
                value.interval.upper.kind,
                value.interval.upper.closed,
                value.interval.upper.value.to_bits(),
            )
                .hash(&mut hash);
        }
    }
    for point in snapshot.head.iter().chain(snapshot.tail.iter()) {
        (
            point.participating_bits,
            point.valid_count,
            point.connect_previous,
            point.has_pre,
            point.pre_mean.to_bits(),
            point.pre_min.to_bits(),
            point.pre_max.to_bits(),
            point.post_mean.to_bits(),
            point.post_min.to_bits(),
            point.post_max.to_bits(),
        )
            .hash(&mut hash);
    }
    hash.finish()
}

/// # Safety
/// Non-null pointers must refer to a live engine and appropriately sized readable/writable storage.
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_poll_attack_band_summary_v2(
    handle: *const KirinHyphaEngine,
    request_size: u32,
    request: *const KirinAttackBandSummaryV2Request,
    out_size: u32,
    out: *mut KirinAttackBandSummaryV2,
) -> u8 {
    catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null()
            || request.is_null()
            || out.is_null()
            || request_size as usize != std::mem::size_of::<KirinAttackBandSummaryV2Request>()
            || (out_size as usize) < std::mem::size_of::<KirinAttackBandSummaryV2>()
            || !(request as usize)
                .is_multiple_of(std::mem::align_of::<KirinAttackBandSummaryV2Request>())
            || !(out as usize).is_multiple_of(std::mem::align_of::<KirinAttackBandSummaryV2>())
        {
            return KIRIN_SNAPSHOT_INVALID_REQUEST;
        }
        // SAFETY: pointer contracts, size and alignment were checked before the read.
        let request = unsafe { request.read() };
        if request.version != KIRIN_ATTACK_BAND_SUMMARY_V2_VERSION {
            return KIRIN_SNAPSHOT_UNSUPPORTED;
        }
        if !request.is_valid() {
            return KIRIN_SNAPSHOT_INVALID_REQUEST;
        }
        // SAFETY: the caller supplies a live engine.
        match unsafe { &*handle }.attack_band_summary_v2(request) {
            Ok(snapshot) => unsafe { commit_sized(out, out_size, snapshot) },
            Err(status) => status,
        }
    }))
    .unwrap_or(KIRIN_SNAPSHOT_BUSY)
}

#[cfg(test)]
#[path = "attack_summary_v2_poll_tests.rs"]
mod tests;
