//! Source-complete authority for new DRUM readers. Legacy views remain a separate contract.
use super::{PostSession, SpectrumCoordinator, SpectrumViewStatus};
use crate::attack_perception::band::{analysis_range, AttackBand};
use crate::attack_runtime::semantics::band_semantic_hash;
use crate::attack_runtime::snapshot::AttackObservationAnchor;
use crate::attack_runtime::snapshot::{
    AttackObservationSnapshot, AttackSourceEvidence, AttackSourceKey,
};
use crate::{AttackEvent, AttackHistory, AttackPairEvent, AttackPairEventKind};
use sha2::{Digest, Sha256};
use std::sync::atomic::Ordering;
use std::sync::Arc;

/// The immutable IO binding that originally armed one request, never a relabeled current view.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct AttackPairAuthority {
    pub generation: u64,
    pub project_hash: String,
    pub pre_instance_id: String,
    pub post_instance_id: String,
    pub owner_id: String,
    pub claimed_at_bits: u64,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct AttackMappingProof {
    pub authority_revision: u64,
    pub request_id: [u8; 16],
    pub target_hash: [u8; 32],
    pub pre: AttackSourceEvidence,
    pub post: AttackSourceEvidence,
    pub mapping_epoch: u64,
    pub support_start: i64,
    pub support_end: i64,
    pub band_semantic_hash: [u8; 32],
}

impl AttackMappingProof {
    pub fn validates_span(
        &self,
        band: AttackBand,
        onset: i64,
        actual_end: i64,
        pre: AttackSourceKey,
        post: AttackSourceKey,
    ) -> bool {
        let (from, to) = analysis_range(band, post.sample_rate, onset, actual_end);
        self.authority_revision > 0
            && self.request_id != [0; 16]
            && self.pre.source == pre
            && self.post.source == post
            && pre.odf_hash == post.odf_hash
            && pre.sample_rate == post.sample_rate
            && pre.channels == post.channels
            && self.pre.valid()
            && self.post.valid()
            && (1..=4).contains(&(self.pre.clock_policy & 255))
            && self.pre.clock_policy & 255 == self.post.clock_policy & 255
            && self.band_semantic_hash == band_semantic_hash()
            && self.pre.band_semantic_hash == self.band_semantic_hash
            && self.post.band_semantic_hash == self.band_semantic_hash
            && self.support_start <= from
            && to <= self.support_end
            && self.pre.pcm_start <= from
            && to <= self.pre.pcm_end
            && self.post.pcm_start <= from
            && to <= self.post.pcm_end
    }
    pub fn validates_raw_span(
        &self,
        from: i64,
        to: i64,
        pre: AttackSourceKey,
        post: AttackSourceKey,
    ) -> bool {
        from < to
            && self.authority_revision > 0
            && self.request_id != [0; 16]
            && self.pre.source == pre
            && self.post.source == post
            && pre.odf_hash == post.odf_hash
            && pre.sample_rate == post.sample_rate
            && pre.channels == post.channels
            && self.pre.valid()
            && self.post.valid()
            && (1..=4).contains(&(self.pre.clock_policy & 255))
            && self.pre.clock_policy & 255 == self.post.clock_policy & 255
            && self.support_start <= from
            && to <= self.support_end
            && self.pre.pcm_start <= from
            && to <= self.pre.pcm_end
            && self.post.pcm_start <= from
            && to <= self.post.pcm_end
    }
    pub fn token(&self) -> [u8; 32] {
        self.hash_token(true)
    }
    pub fn binding_token(&self) -> [u8; 32] {
        self.hash_token(false)
    }
    fn hash_token(&self, include_support: bool) -> [u8; 32] {
        let mut hash = Sha256::new();
        hash.update(self.request_id);
        hash.update(self.authority_revision.to_le_bytes());
        hash.update(self.target_hash);
        for source in [self.pre, self.post] {
            hash.update(source.source.incarnation);
            hash.update(source.source.generation.to_le_bytes());
            hash.update(source.source.sample_rate.to_le_bytes());
            hash.update([source.source.channels]);
            hash.update(source.source.odf_hash);
            hash.update(source.clock_policy.to_le_bytes());
        }
        hash.update(self.mapping_epoch.to_le_bytes());
        if include_support {
            hash.update(self.support_start.to_le_bytes());
            hash.update(self.support_end.to_le_bytes());
        }
        hash.update(self.band_semantic_hash);
        hash.finalize().into()
    }
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct AttackContentEvent {
    pub source: AttackSourceKey,
    pub event_sample: i64,
    pub token: u64,
    pub pair: AttackPairEvent,
}

#[derive(Clone, Debug, Default)]
pub struct AttackObservationView {
    pub status: SpectrumViewStatus,
    pub authority_revision: u64,
    pub target_hash: [u8; 32],
    pub proof: Option<AttackMappingProof>,
    pub origin: Option<AttackPairAuthority>,
    pub pre: Option<Arc<AttackObservationSnapshot>>,
    pub post: Option<Arc<AttackObservationSnapshot>>,
    pub post_history: Option<Arc<AttackHistory>>,
    pub pre_history: Option<Arc<AttackHistory>>,
    pub mapping_request_id: [u8; 16],
    /// Retain the ledger's original source qualification even while numeric proof is absent.
    pub mapping_source_pair: Option<(AttackSourceKey, AttackSourceKey)>,
    pub events: Vec<AttackContentEvent>,
    /// Retired public event tokens redirect only within this bounded source/run view.
    pub aliases: Vec<(u64, u64)>,
}

impl AttackObservationView {
    fn qualify_content_ledger(&mut self, proof: &AttackMappingProof) {
        let sources = (proof.pre.source, proof.post.source);
        if self.mapping_request_id != proof.request_id || self.mapping_source_pair != Some(sources)
        {
            *self = Default::default();
        }
        self.mapping_request_id = proof.request_id;
        self.mapping_source_pair = Some(sources);
    }
}

impl SpectrumCoordinator {
    pub fn set_pair_authority_revision(&self, revision: u64) {
        let old = self
            .pair_authority_revision
            .fetch_max(revision, Ordering::AcqRel);
        if revision > old {
            if let Ok(mut view) = self.attack_observation_view.lock() {
                *view = Default::default();
            }
            if let Some(runtime) = &self.attack_runtime {
                runtime.request_observation_anchors(runtime.band(), Vec::new());
            }
        }
    }
    /// IO only. An older cycle cannot roll back a newer manual selection. New requests capture
    /// the whole origin under a separate lock; stale sessions retain their original binding.
    pub fn set_attack_pair_authority(&self, revision: u64, origin: Option<AttackPairAuthority>) {
        self.set_pair_authority_revision(revision);
        let Ok(mut current) = self.attack_pair_authority.lock() else {
            return;
        };
        if self.pair_authority_revision.load(Ordering::Acquire) != revision {
            return;
        }
        if origin
            .as_ref()
            .is_some_and(|origin| origin.generation != revision)
        {
            return;
        }
        *current = origin;
    }
    pub fn try_attack_observation_view(&self) -> Option<AttackObservationView> {
        let view = self.attack_observation_view.try_lock().ok()?.clone();
        let revision = self.pair_authority_revision.load(Ordering::Acquire);
        if view.authority_revision == revision {
            Some(view)
        } else {
            Some(AttackObservationView {
                authority_revision: revision,
                ..Default::default()
            })
        }
    }
    pub(super) fn clear_attack_observations(&self) {
        if let Ok(mut view) = self.attack_observation_view.lock() {
            view.authority_revision = self.pair_authority_revision.load(Ordering::Acquire);
            view.status = SpectrumViewStatus::Unavailable;
            view.proof = None;
            view.pre = None;
            view.post = None;
            view.pre_history = None;
            view.post_history = None;
        }
    }
    pub(super) fn store_attack_observations(
        &self,
        session: &PostSession,
        pre_history: &AttackHistory,
        post_history: &AttackHistory,
        pre: Arc<AttackObservationSnapshot>,
        pairs: &[AttackPairEvent],
    ) -> bool {
        let revision = session.authority_revision;
        if self.pair_authority_revision.load(Ordering::Acquire) != revision {
            return false;
        }
        let Some(origin) = session
            .attack_origin
            .clone()
            .filter(|origin| origin.generation == revision)
        else {
            return false;
        };
        if self
            .attack_pair_authority
            .try_lock()
            .ok()
            .is_none_or(|current| current.as_ref() != Some(&origin))
        {
            return false;
        }
        let Some(post) = self
            .attack_runtime
            .as_ref()
            .and_then(|runtime| runtime.try_observation_snapshot())
        else {
            return false;
        };
        let (Some(pre_source), Some(post_source)) = (pre.source, post.source) else {
            return false;
        };
        let Some((support_start, support_end)) = mapped_support(
            pre_history,
            post_history,
            pre_source.source,
            post_source.source,
        ) else {
            return false;
        };
        let Some(target) = session.target.as_ref() else {
            return false;
        };
        if target.pre_instance_id != origin.pre_instance_id {
            return false;
        }
        let target_hash: [u8; 32] = Sha256::digest(target.pre_instance_id.as_bytes()).into();
        let proof = AttackMappingProof {
            authority_revision: revision,
            request_id: *session.request_id.as_bytes(),
            target_hash,
            pre: pre_source,
            post: post_source,
            mapping_epoch: u64::from_le_bytes(
                session.request_id.as_bytes()[..8].try_into().unwrap(),
            )
            .max(1),
            support_start,
            support_end,
            band_semantic_hash: band_semantic_hash(),
        };
        let mut anchors = Vec::new();
        for pair in pairs {
            if !matches!(
                pair.kind,
                AttackPairEventKind::Matched | AttackPairEventKind::PreOnly
            ) {
                continue;
            }
            let Some(onset) = pair.pre_event_sample else {
                continue;
            };
            let Some(pre_observation) = pre.own_at(onset) else {
                continue;
            };
            let Some(measure) = pre_observation.measure else {
                continue;
            };
            if !proof.validates_span(
                measure.band,
                onset,
                measure.span_end_sample,
                pre_source.source,
                post_source.source,
            ) {
                continue;
            }
            anchors.push(AttackObservationAnchor {
                event: AttackEvent {
                    generation: post_source.source.generation,
                    sample_rate: post_source.source.sample_rate,
                    channels: post_source.source.channels,
                    definition_hash: post_source.source.odf_hash,
                    event_sample: onset,
                    decision_sample: pair.decision_sample.max(onset),
                    value: pair.post_value.unwrap_or(0.0),
                },
                requested_end: pre_observation.requested_end,
                measurement_end: measure.span_end_sample,
                span_end: measure.span_end,
            });
        }
        let Ok(mut view) = self.attack_observation_view.lock() else {
            return false;
        };
        if self.pair_authority_revision.load(Ordering::Acquire) != revision {
            return false;
        }
        view.qualify_content_ledger(&proof);
        let old = view.events.clone();
        let mut serial = old.iter().map(|event| event.token).max().unwrap_or(0);
        let mut events = Vec::new();
        for &pair in pairs
            .iter()
            .rev()
            .take(crate::ATTACK_EVENT_HISTORY_CAPACITY)
            .collect::<Vec<_>>()
            .iter()
            .rev()
        {
            let found = old
                .iter()
                .filter(|event| same_content(event.pair, *pair))
                .min_by_key(|event| event.token);
            let (source, event_sample, token) = match found {
                Some(event) => (event.source, event.event_sample, event.token),
                None => {
                    serial += 1;
                    (
                        if pair.pre_event_sample.is_some() {
                            pre_source.source
                        } else {
                            post_source.source
                        },
                        pair.pre_event_sample
                            .or(pair.post_event_sample)
                            .unwrap_or(pair.event_sample),
                        serial,
                    )
                }
            };
            if let Some(found) = found {
                for duplicate in old
                    .iter()
                    .filter(|event| same_content(event.pair, *pair) && event.token != found.token)
                {
                    if !view.aliases.contains(&(duplicate.token, token)) {
                        view.aliases.push((duplicate.token, token));
                    }
                }
            }
            events.push(AttackContentEvent {
                source,
                event_sample,
                token,
                pair: *pair,
            });
        }
        view.aliases
            .retain(|(_, target)| events.iter().any(|event| event.token == *target));
        if view.aliases.len() > crate::ATTACK_EVENT_HISTORY_CAPACITY {
            let extra = view.aliases.len() - crate::ATTACK_EVENT_HISTORY_CAPACITY;
            view.aliases.drain(..extra);
        }
        view.status = SpectrumViewStatus::Active;
        view.authority_revision = revision;
        view.target_hash = target_hash;
        view.proof = Some(proof);
        view.origin = Some(origin);
        view.pre = Some(pre);
        view.post = Some(post);
        view.post_history = Some(Arc::new(post_history.clone()));
        view.pre_history = Some(Arc::new(pre_history.clone()));
        view.mapping_request_id = proof.request_id;
        view.events = events;
        drop(view);
        if let Some(runtime) = &self.attack_runtime {
            runtime.request_observation_anchors(runtime.band(), anchors);
        }
        true
    }
}

fn same_content(a: AttackPairEvent, b: AttackPairEvent) -> bool {
    if (a.kind == AttackPairEventKind::Ambiguous) != (b.kind == AttackPairEventKind::Ambiguous) {
        return false;
    }
    a.pre_event_sample
        .zip(b.pre_event_sample)
        .is_some_and(|(a, b)| a == b)
        || a.post_event_sample
            .zip(b.post_event_sample)
            .is_some_and(|(a, b)| a == b)
        || (a.kind == AttackPairEventKind::Ambiguous
            && b.kind == a.kind
            && a.event_sample == b.event_sample)
}

fn mapped_support(
    pre: &AttackHistory,
    post: &AttackHistory,
    pre_key: AttackSourceKey,
    post_key: AttackSourceKey,
) -> Option<(i64, i64)> {
    let mut a = pre.frames().peekable();
    let mut b = post.frames().peekable();
    let (mut start, mut end, mut previous) = (None, None, None);
    while let (Some(left), Some(right)) = (a.peek(), b.peek()) {
        match left.event_sample.cmp(&right.event_sample) {
            std::cmp::Ordering::Less => {
                a.next();
            }
            std::cmp::Ordering::Greater => {
                b.next();
            }
            std::cmp::Ordering::Equal => {
                let (left, right) = (*a.next()?, *b.next()?);
                if left.generation != pre_key.generation
                    || right.generation != post_key.generation
                    || left.definition_hash != pre_key.odf_hash
                    || right.definition_hash != post_key.odf_hash
                    || left.definition_hash != right.definition_hash
                    || left.sample_rate != right.sample_rate
                    || left.channels != right.channels
                    || left.hop_samples != right.hop_samples
                    || left.support_start_samples != right.support_start_samples
                    || left.support_end_samples != right.support_end_samples
                {
                    return None;
                }
                if previous.is_none_or(|sample: i64| {
                    sample.checked_add(i64::from(left.hop_samples)) != Some(left.event_sample)
                }) {
                    start = Some(left.support_start_samples);
                }
                previous = Some(left.event_sample);
                end = Some(left.support_end_samples);
            }
        }
    }
    Some((start?, end?))
}

#[cfg(test)]
#[path = "attack_snapshot_exchange_tests.rs"]
mod tests;
