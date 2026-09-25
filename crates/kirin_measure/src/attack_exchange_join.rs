//! Exact content-time ATTACK join and presentation snapshot.

use std::sync::TryLockError;
use std::time::Instant;

use super::{
    AttackPairViewSnapshot, PostSession, SpectrumCoordinator, SpectrumViewStatus,
    PRESENTATION_HOLD, WARMUP_LIMIT,
};
use crate::attack_runtime::AttackAnchor;
use crate::{
    AttackDetailedEvent, AttackEvent, AttackHistory, AttackPairEvent, AttackPairEventKind,
    AttackPairJoiner,
};

pub(super) fn store_joined_attack(
    coordinator: &SpectrumCoordinator,
    session: &mut PostSession,
    now: Instant,
    post: Option<AttackHistory>,
    pre: Option<AttackHistory>,
) {
    let joined = post
        .as_ref()
        .zip(pre.as_ref())
        .and_then(|(post, pre)| exact_pair_events(pre, post));
    if let Some((endpoint, pair_events)) = joined {
        let post_anchored = post
            .as_ref()
            .zip(pre.as_ref())
            .map(|(post, pre)| anchored_post_details(coordinator, pre, post, &pair_events))
            .unwrap_or_default();
        coordinator.store_attack_view(AttackPairViewSnapshot {
            status: SpectrumViewStatus::Active,
            pre,
            post,
            pair_events,
            post_anchored,
        });
        session.last_presented_at = Some(now);
        session.last_presented_end_samples = Some(endpoint);
        return;
    }
    if session
        .last_presented_at
        .is_some_and(|presented| now.duration_since(presented) < PRESENTATION_HOLD)
    {
        return;
    }
    let has_both =
        post.as_ref().is_some_and(has_attack_data) && pre.as_ref().is_some_and(has_attack_data);
    let status = if has_both
        || session
            .started_at
            .is_some_and(|started| now.duration_since(started) >= WARMUP_LIMIT)
    {
        SpectrumViewStatus::Unavailable
    } else {
        SpectrumViewStatus::WarmingUp
    };
    coordinator.store_attack_view(AttackPairViewSnapshot {
        status,
        pre,
        post,
        ..Default::default()
    });
}

/// POST measured at each matched PRE onset over the PRE detail's head, body and Sharpness
/// windows, read from the POST runtime's retained bins. A pair whose PRE detail has not arrived,
/// or whose windows are outside the bins, has no anchored POST detail yet.
fn anchored_post_details(
    coordinator: &SpectrumCoordinator,
    pre: &AttackHistory,
    post: &AttackHistory,
    pairs: &[AttackPairEvent],
) -> Vec<AttackDetailedEvent> {
    let (Some(runtime), Some(identity)) = (coordinator.attack_runtime.as_ref(), post.newest())
    else {
        return Vec::new();
    };
    // History details are strictly increasing in event_sample.
    let pre_details = pre.details().collect::<Vec<_>>();
    let anchors = pairs
        .iter()
        .filter(|pair| pair.kind == AttackPairEventKind::Matched)
        .filter_map(|pair| {
            let onset = pair.pre_event_sample?;
            let found =
                pre_details.binary_search_by_key(&onset, |detail| detail.event.event_sample);
            let pre_detail = pre_details[found.ok()?];
            Some(AttackAnchor {
                event: AttackEvent {
                    generation: identity.generation,
                    sample_rate: identity.sample_rate,
                    channels: identity.channels,
                    definition_hash: identity.definition_hash,
                    event_sample: onset,
                    decision_sample: pair.decision_sample.max(onset),
                    value: pair.post_value.unwrap_or(0.0),
                },
                body_end_sample: pre_detail.features.body_end_sample,
            })
        })
        .collect::<Vec<_>>();
    runtime.details_at(&anchors)
}

fn exact_pair_events(
    pre: &AttackHistory,
    post: &AttackHistory,
) -> Option<(i64, Vec<crate::AttackPairEvent>)> {
    let mut pre_frames = pre.frames().peekable();
    let mut post_frames = post.frames().peekable();
    let mut joiner = AttackPairJoiner::new();
    let mut events = Vec::new();
    let mut newest_endpoint = None;
    while let (Some(pre_frame), Some(post_frame)) = (pre_frames.peek(), post_frames.peek()) {
        match pre_frame.event_sample.cmp(&post_frame.event_sample) {
            std::cmp::Ordering::Less => {
                pre_frames.next();
            }
            std::cmp::Ordering::Greater => {
                post_frames.next();
            }
            std::cmp::Ordering::Equal => {
                let pre_frame = **pre_frame;
                let post_frame = **post_frame;
                pre_frames.next();
                post_frames.next();
                let emitted = joiner.push(pre_frame, post_frame).ok()?;
                newest_endpoint = Some(pre_frame.support_end_samples);
                if let Some(event) = emitted {
                    events.push(event);
                }
            }
        }
    }
    newest_endpoint.map(|endpoint| (endpoint, events))
}

fn has_attack_data(history: &AttackHistory) -> bool {
    history.newest().is_some() && history.waveform().next_back().is_some()
}

impl SpectrumCoordinator {
    pub fn try_attack_view(&self) -> Option<AttackPairViewSnapshot> {
        match self.attack_view.try_lock() {
            Ok(view) => Some(view.clone()),
            Err(TryLockError::WouldBlock) => None,
            Err(TryLockError::Poisoned(poisoned)) => Some(poisoned.into_inner().clone()),
        }
    }

    pub(super) fn store_attack_view(&self, view: AttackPairViewSnapshot) {
        let mut current = match self.attack_view.lock() {
            Ok(current) => current,
            Err(poisoned) => poisoned.into_inner(),
        };
        *current = view;
    }
}

#[cfg(test)]
#[path = "attack_exchange_join_tests.rs"]
mod tests;
