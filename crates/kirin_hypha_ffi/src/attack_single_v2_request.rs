//! Validate control authority before accepting one bounded worker request.
use super::*;
use crate::attack_snapshot_authority::AttackSnapshotAuthority;
use crate::KirinHyphaEngine;
use kirin_measure::attack_perception::band::{span_end_for, AttackBand, BandSpanEnd};
use kirin_measure::attack_runtime::single::{AttackSingleRequest, AttackSingleSnapshot};
use kirin_measure::attack_runtime::snapshot::{AttackObservationSnapshot, AttackSourceKey};
use kirin_measure::{AttackDetailedEvent, AttackEvent, AttackHistory, PluginDataRole};
use std::panic::{catch_unwind, AssertUnwindSafe};

pub(super) fn source_key(value: AttackSourceKey) -> KirinSnapshotSourceKey {
    KirinSnapshotSourceKey {
        incarnation: value.incarnation,
        generation: value.generation,
        sample_rate: value.sample_rate,
        channels: value.channels,
        reserved: [0; 3],
        odf_hash: value.odf_hash,
    }
}
fn same_key(source: AttackSourceKey, key: KirinSnapshotSourceKey) -> bool {
    source_key(source) == key
}

pub(super) fn pre_completion(
    request: &AttackSingleRequest,
    proof: Option<kirin_measure::spectrum_exchange::AttackMappingProof>,
    detail: Option<AttackDetailedEvent>,
) -> Option<(AttackSourceKey, AttackDetailedEvent)> {
    let proof = proof?;
    let detail = detail?;
    (proof.binding_token() == request.proof_token
        && Some(proof.pre.source) == request.pre_source
        && proof.pre.source.matches(&detail.event)
        && detail.event.event_sample == request.event.event_sample
        && detail.features.complete)
        .then_some((proof.pre.source, detail))
}

fn own_request(
    history: &AttackHistory,
    local: &AttackObservationSnapshot,
    request: KirinAttackSingleV2Request,
    authority: u64,
) -> Option<AttackSingleRequest> {
    let source = local.source?.source;
    if !same_key(source, request.event.source)
        || request.event.token != request.event.event_sample as u64
    {
        return None;
    }
    let events: Vec<_> = history.events().copied().collect();
    let index = events.iter().position(|event| {
        source.matches(event) && event.event_sample == request.event.event_sample
    })?;
    let event = events[index];
    let next = events.get(index + 1).map(|event| event.event_sample);
    let band = AttackBand::from_index(request.band);
    let (requested_end, span_end) = if band.is_some() {
        span_end_for(source.sample_rate, event.event_sample, next)
    } else {
        (
            all_end(source.sample_rate, event.event_sample, next),
            BandSpanEnd::Window,
        )
    };
    Some(AttackSingleRequest {
        key_source: source,
        key_event_sample: event.event_sample,
        key_token: request.event.token,
        local_source: source,
        pre_source: None,
        event,
        band,
        requested_end,
        measurement_end: requested_end,
        span_end,
        pre: None,
        pre_detail: None,
        pair_kind: 4,
        target: request.target,
        pair_authority_revision: authority,
        proof_token: [0; 32],
    })
}

fn all_end(rate: u32, onset: i64, next: Option<i64>) -> i64 {
    let bin = kirin_measure::attack_perception::attack_bin_frames(rate);
    let head_end = onset
        .div_euclid(bin)
        .saturating_add(kirin_measure::ATTACK_HEAD_BINS);
    let limit = head_end.saturating_add(kirin_measure::ATTACK_BODY_BINS);
    next.map(|sample| sample.div_euclid(bin))
        .unwrap_or(limit)
        .min(limit)
        .max(head_end)
        .saturating_mul(bin)
}

impl KirinHyphaEngine {
    pub fn request_attack_single_v2(&self, request: KirinAttackSingleV2Request) -> Result<u64, u8> {
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
        if runtime.band().map_or(0, AttackBand::index) != request.band {
            return Err(KIRIN_SNAPSHOT_INVALID_REQUEST);
        }
        let local = runtime
            .try_observation_snapshot()
            .ok_or(KIRIN_SNAPSHOT_BUSY)?;
        let source = local
            .source
            .filter(|source| source.valid())
            .ok_or(KIRIN_SNAPSHOT_BUSY)?;
        let history = runtime.try_history().ok_or(KIRIN_SNAPSHOT_BUSY)?;
        let view = self
            .spectrum
            .try_attack_observation_view()
            .ok_or(KIRIN_SNAPSHOT_BUSY)?;
        let paired = authority.matches_view(&view)
            && view
                .proof
                .is_some_and(|proof| proof.post.source == source.source);
        let mut domain = if paired {
            let token = view
                .aliases
                .iter()
                .find(|(alias, _)| *alias == request.event.token)
                .map_or(request.event.token, |(_, canonical)| *canonical);
            let content = view
                .events
                .iter()
                .find(|content| {
                    content.token == token
                        && same_key(content.source, request.event.source)
                        && content.event_sample == request.event.event_sample
                })
                .ok_or(KIRIN_SNAPSHOT_RETIRED)?;
            let pair = content.pair;
            if pair.kind == kirin_measure::AttackPairEventKind::Ambiguous {
                return Err(KIRIN_SNAPSHOT_INVALID_REQUEST);
            }
            let pre_onset = pair.pre_event_sample;
            let onset = pre_onset
                .or(pair.post_event_sample)
                .ok_or(KIRIN_SNAPSHOT_INVALID_REQUEST)?;
            let band = AttackBand::from_index(request.band);
            let pre = pre_onset.and_then(|onset| view.pre.as_ref()?.own_at(onset).copied());
            let pre_detail = pre_onset.and_then(|onset| {
                view.pre_history
                    .as_ref()?
                    .details()
                    .find(|detail| detail.event.event_sample == onset)
                    .copied()
            });
            let (requested_end, measurement_end, span_end) =
                if band.is_some() && pre_onset.is_some() {
                    let observation = pre.ok_or(KIRIN_SNAPSHOT_BUSY)?;
                    let measure = observation.measure.ok_or(KIRIN_SNAPSHOT_BUSY)?;
                    (
                        observation.requested_end,
                        observation.actual_end,
                        measure.span_end,
                    )
                } else if band.is_some() {
                    let observation = local.own_at(onset).ok_or(KIRIN_SNAPSHOT_BUSY)?;
                    (
                        observation.requested_end,
                        observation.requested_end,
                        BandSpanEnd::Window,
                    )
                } else {
                    let logical = pre_detail
                        .filter(|detail| detail.features.complete)
                        .map(|detail| detail.features.body_end_sample)
                        .unwrap_or_else(|| all_end(source.source.sample_rate, onset, None));
                    (logical, logical, BandSpanEnd::Window)
                };
            // The public content owner may be PRE, while the measured event is explicitly
            // remapped into this POST run. Detector generations never stand in for each other.
            let event = AttackEvent {
                generation: source.source.generation,
                sample_rate: source.source.sample_rate,
                channels: source.source.channels,
                definition_hash: source.source.odf_hash,
                event_sample: onset,
                decision_sample: pair.decision_sample.max(onset),
                value: pair.post_value.unwrap_or(0.0),
            };
            AttackSingleRequest {
                key_source: content.source,
                key_event_sample: content.event_sample,
                key_token: content.token,
                local_source: source.source,
                pre_source: pre_onset.map(|_| view.proof.unwrap().pre.source),
                event,
                band,
                requested_end,
                measurement_end,
                span_end,
                pre,
                pre_detail,
                pair_kind: pair.kind as u8,
                target: request.target,
                pair_authority_revision: authority.pair.generation,
                proof_token: view.proof.unwrap().binding_token(),
            }
        } else {
            own_request(&history, &local, request, authority.pair.generation)
                .ok_or(KIRIN_SNAPSHOT_RETIRED)?
        };
        // Post-only has no measured PRE; preserve that fact rather than manufacturing silence.
        if domain.pair_kind == 2 {
            domain.pre = None;
            domain.pre_detail = None;
        }
        if AttackSnapshotAuthority::read(self).as_ref() != Some(&authority)
            || runtime
                .try_observation_snapshot()
                .and_then(|value| value.source)
                .is_none_or(|after| after.source != source.source)
        {
            return Err(KIRIN_SNAPSHOT_BUSY);
        }
        let token = runtime.request_single(domain).ok_or(KIRIN_SNAPSHOT_BUSY)?;
        if AttackSnapshotAuthority::read(self).as_ref() != Some(&authority) {
            runtime.cancel_single(token);
            return Err(KIRIN_SNAPSHOT_BUSY);
        }
        Ok(token)
    }

    pub fn attack_single_v2(&self, token: u64) -> Result<KirinAttackSingleSnapshotV2, u8> {
        let authority = AttackSnapshotAuthority::read(self).ok_or(KIRIN_SNAPSHOT_BUSY)?;
        if authority.role != Some(PluginDataRole::Post) {
            return Err(KIRIN_SNAPSHOT_UNSUPPORTED);
        }
        let runtime = self
            .attack_runtime
            .as_ref()
            .ok_or(KIRIN_SNAPSHOT_UNSUPPORTED)?;
        let mut state: AttackSingleSnapshot =
            runtime.poll_single(token).ok_or(KIRIN_SNAPSHOT_RETIRED)?;
        let local = runtime.try_observation_snapshot();
        let view = self
            .spectrum
            .try_attack_observation_view()
            .ok_or(KIRIN_SNAPSHOT_BUSY)?;
        let source = local.as_ref().and_then(|snapshot| snapshot.source);
        let paired = state.request.proof_token != [0; 32];
        if (authority.pair.generation != state.request.pair_authority_revision
            || source.is_none_or(|source| source.source != state.request.local_source)
            || (paired
                && (!authority.matches_view(&view)
                    || view
                        .proof
                        .is_none_or(|proof| proof.binding_token() != state.request.proof_token))))
            && state.finish != kirin_measure::attack_runtime::snapshot::AttackFinish::Retired
        {
            state = runtime
                .retire_single(
                    token,
                    kirin_measure::attack_runtime::single::AttackSingleReason::SourceChanged,
                )
                .ok_or(KIRIN_SNAPSHOT_BUSY)?;
        }
        if state.finish == kirin_measure::attack_runtime::snapshot::AttackFinish::Acquiring
            && state.request.band.is_none()
            && matches!(state.request.pair_kind, 0 | 1)
        {
            let detail = view.pre_history.as_ref().and_then(|history| {
                history
                    .details()
                    .find(|detail| detail.event.event_sample == state.request.event.event_sample)
                    .copied()
            });
            let completion = pre_completion(&state.request, view.proof, detail);
            if let Some((pre_source, detail)) = completion {
                runtime
                    .complete_single_pre(token, pre_source, state.request.proof_token, detail)
                    .ok_or(KIRIN_SNAPSHOT_BUSY)?;
                state = runtime.poll_single(token).ok_or(KIRIN_SNAPSHOT_RETIRED)?;
            }
        }
        let snapshot = super::assemble::assemble(&state, source, &view, authority.signal);
        if AttackSnapshotAuthority::read(self).as_ref() != Some(&authority)
            || runtime
                .try_observation_snapshot()
                .and_then(|value| value.source)
                .map(|source| source.source)
                != source.map(|source| source.source)
        {
            return Err(KIRIN_SNAPSHOT_BUSY);
        }
        Ok(snapshot)
    }
}

/// # Safety
/// Pointers must name a live engine, one readable request and a writable aligned u64.
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_request_attack_single_v2(
    handle: *const KirinHyphaEngine,
    request_size: u32,
    request: *const KirinAttackSingleV2Request,
    out_token: *mut u64,
) -> u8 {
    catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null()
            || request.is_null()
            || out_token.is_null()
            || request_size as usize != std::mem::size_of::<KirinAttackSingleV2Request>()
            || !(request as usize)
                .is_multiple_of(std::mem::align_of::<KirinAttackSingleV2Request>())
            || !(out_token as usize).is_multiple_of(std::mem::align_of::<u64>())
        {
            return KIRIN_SNAPSHOT_INVALID_REQUEST;
        }
        let request = unsafe { request.read() };
        if request.version != 2 {
            return KIRIN_SNAPSHOT_UNSUPPORTED;
        }
        match unsafe { &*handle }.request_attack_single_v2(request) {
            Ok(token) => {
                unsafe { out_token.write(token) };
                KIRIN_SNAPSHOT_SUCCESS
            }
            Err(status) => status,
        }
    }))
    .unwrap_or(KIRIN_SNAPSHOT_BUSY)
}

/// # Safety
/// Pointers must name a live engine and writable aligned storage of at least out_size bytes.
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_poll_attack_single_v2(
    handle: *const KirinHyphaEngine,
    token: u64,
    out_size: u32,
    out: *mut KirinAttackSingleSnapshotV2,
) -> u8 {
    catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null()
            || out.is_null()
            || token == 0
            || (out_size as usize) < std::mem::size_of::<KirinAttackSingleSnapshotV2>()
            || !(out as usize).is_multiple_of(std::mem::align_of::<KirinAttackSingleSnapshotV2>())
        {
            return KIRIN_SNAPSHOT_INVALID_REQUEST;
        }
        match unsafe { &*handle }.attack_single_v2(token) {
            Ok(snapshot) => unsafe { commit_sized(out, out_size, snapshot) },
            Err(status) => status,
        }
    }))
    .unwrap_or(KIRIN_SNAPSHOT_BUSY)
}

/// # Safety
/// A non-null handle must name a live engine.
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_cancel_attack_single_v2(
    handle: *const KirinHyphaEngine,
    token: u64,
) -> u8 {
    catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() || token == 0 {
            return KIRIN_SNAPSHOT_INVALID_REQUEST;
        }
        let Some(runtime) = (unsafe { &*handle }).attack_runtime.as_ref() else {
            return KIRIN_SNAPSHOT_UNSUPPORTED;
        };
        match runtime.try_cancel_single(token) {
            Some(true) => KIRIN_SNAPSHOT_SUCCESS,
            Some(false) => KIRIN_SNAPSHOT_RETIRED,
            None => KIRIN_SNAPSHOT_BUSY,
        }
    }))
    .unwrap_or(KIRIN_SNAPSHOT_BUSY)
}
