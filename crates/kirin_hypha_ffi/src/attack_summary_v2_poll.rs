//! Bounded try-read acquisition. All facts are assembled before the single output commit.
use super::revision::content_revision;
use super::*;
use crate::attack_snapshot_authority::AttackSnapshotAuthority;
use crate::KirinHyphaEngine;
use kirin_measure::attack_perception::band::AttackBand;
use kirin_measure::attack_runtime::snapshot::AttackSourceKey;
use kirin_measure::spectrum_exchange::AttackMappingProof;
use kirin_measure::{PluginDataRole, SpectrumViewStatus};
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

pub(super) fn source_event(
    source: AttackSourceKey,
    event_sample: i64,
    token: u64,
) -> KirinSnapshotEventKey {
    KirinSnapshotEventKey {
        source: source_key(source),
        event_sample,
        token,
    }
}

pub(crate) fn proof_revision(proof: &AttackMappingProof) -> u64 {
    u64::from_le_bytes(proof.token()[..8].try_into().expect("fixed proof hash"))
}

use crate::attack_navigation_v2::same_binding;

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
        let band = AttackBand::from_index(request.band);
        if band.is_some() && runtime.band() != band {
            return Err(KIRIN_SNAPSHOT_INVALID_REQUEST);
        }
        let local = if band.is_some() {
            runtime.try_observation_snapshot()
        } else {
            runtime.try_navigation_snapshot_result().ok()
        }
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
        let navigation = if band.is_none() {
            Some(crate::attack_navigation_v2::assemble_navigation(
                &authority,
                &local,
                &history,
                &view,
                request.target,
            )?)
        } else {
            None
        };
        let target = navigation
            .as_ref()
            .map_or(request.target, |n| n.header.target);
        let all_paired = navigation.as_ref().is_some_and(|n| n.header.flags & 1 != 0);
        let candidates = match band {
            Some(band) if paired => super::band::paired_events(&view, &local, band, target, source),
            Some(band) => super::band::local_events(&history, &local, band, target, source),
            None => super::all::events(&history, &view, source, all_paired, target),
        };
        let (mut events, newer_unfinished_count) =
            super::cohort::select(&candidates, source.cutoff, source.source.sample_rate);
        if let Some(band) = band {
            super::band::enrich(&mut events, &view, &local, band, source, paired);
        }
        let header = KirinSnapshotHeader {
            version: 2,
            struct_size: std::mem::size_of::<KirinAttackBandSummaryV2>() as u32,
            kind: KIRIN_SNAPSHOT_BAND_SUMMARY,
            target,
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
        snapshot.newer_unfinished_count = newer_unfinished_count;
        if band.is_none() {
            super::all::representative(&mut snapshot, &history, &view, source, all_paired);
        }
        if authority.matches_view(&view) {
            if let Some(proof) = view
                .proof
                .filter(|proof| proof.post.source == source.source)
            {
                snapshot.comparison_binding = proof.binding_token();
            }
        }
        snapshot.header.snapshot_revision = content_revision(
            &snapshot,
            local.revision,
            view.pre.as_ref().map_or(0, |pre| pre.revision),
        );
        let after = if band.is_some() {
            runtime.try_observation_snapshot()
        } else {
            runtime.try_navigation_snapshot_result().ok()
        }
        .ok_or(KIRIN_SNAPSHOT_BUSY)?;
        let after_view = self
            .spectrum
            .try_attack_observation_view()
            .ok_or(KIRIN_SNAPSHOT_BUSY)?;
        if after.source.is_none_or(|s| {
            s.source != source.source || s.band_semantic_hash != source.band_semantic_hash
        }) || (band.is_some() && after.band != band)
            || AttackSnapshotAuthority::read(self).as_ref() != Some(&authority)
            || !same_binding(&view, &after_view)
        {
            return Err(KIRIN_SNAPSHOT_BUSY);
        }
        Ok(snapshot)
    }
}

/// # Safety
/// Pointers name a live engine, a readable aligned version prefix and aligned writable output.
/// A V2 request additionally requires the complete V2 request and declared output storage.
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
            || request_size < std::mem::size_of::<u32>() as u32
            || !(request as usize)
                .is_multiple_of(std::mem::align_of::<KirinAttackBandSummaryV2Request>())
            || !(out as usize).is_multiple_of(std::mem::align_of::<KirinAttackBandSummaryV2>())
        {
            return KIRIN_SNAPSHOT_INVALID_REQUEST;
        }
        // Read only the fixed version prefix until this version's full layout is validated.
        if unsafe { request.cast::<u32>().read() } != KIRIN_ATTACK_BAND_SUMMARY_V2_VERSION {
            return KIRIN_SNAPSHOT_UNSUPPORTED;
        }
        if request_size as usize != std::mem::size_of::<KirinAttackBandSummaryV2Request>()
            || (out_size as usize) < std::mem::size_of::<KirinAttackBandSummaryV2>()
        {
            return KIRIN_SNAPSHOT_INVALID_REQUEST;
        }
        // SAFETY: pointer contracts, size and alignment were checked before the full read.
        let request = unsafe { request.read() };
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

#[cfg(test)]
#[path = "attack_summary_v2_completion_tests.rs"]
mod completion_tests;

#[cfg(test)]
#[path = "attack_drum_integration_tests.rs"]
mod drum_integration_tests;
