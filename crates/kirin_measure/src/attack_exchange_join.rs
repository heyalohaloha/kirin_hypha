//! Exact content-time ATTACK join and presentation snapshot.

use std::sync::{Arc, TryLockError};
use std::time::{Duration, Instant};

use super::{
    AttackPairViewSnapshot, PostSession, SpectrumCoordinator, SpectrumViewStatus,
    PRESENTATION_HOLD, WARMUP_LIMIT,
};
use crate::attack_perception::band::AttackBand;
use crate::attack_runtime::{AttackAnchor, AttackBandResults, AttackPreBand, BandAnchor};
use crate::{
    AttackDetailedEvent, AttackEvent, AttackHistory, AttackPairEvent, AttackPairEventKind,
    AttackPairJoiner,
};

/// A PRE whose snapshots still carry no band this long after the band was sent predates bands.
const PRE_BAND_PREDATES_AFTER: Duration = Duration::from_millis(2_500);

pub(super) fn store_joined_attack(
    coordinator: &SpectrumCoordinator,
    session: &mut PostSession,
    now: Instant,
    post: Option<AttackHistory>,
    pre: Option<(AttackHistory, Option<AttackBandResults>)>,
) {
    let band = coordinator
        .attack_runtime
        .as_ref()
        .and_then(|runtime| runtime.band());
    let (pre, pre_band_results) = match pre {
        Some((history, results)) => (Some(history), results.map(Arc::new)),
        None => (None, None),
    };
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
        let pre_band = pre_band_state(
            band,
            pre_band_results.as_deref(),
            coordinator.attack_band_sent_for(now),
        );
        let anchors = match (pre_band, pre_band_results.as_deref(), post.as_ref()) {
            (AttackPreBand::Same, Some(results), Some(post)) => {
                band_anchors(results, post, &pair_events)
            }
            _ => Vec::new(),
        };
        request_anchors(coordinator, band, anchors);
        coordinator.store_attack_view(AttackPairViewSnapshot {
            status: SpectrumViewStatus::Active,
            pre,
            post,
            pair_events,
            post_anchored,
            band,
            pre_band,
            pre_band_results,
        });
        session.last_presented_at = Some(now);
        session.last_presented_end_samples = Some(endpoint);
        return;
    }
    request_anchors(coordinator, band, Vec::new());
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
        band,
        ..Default::default()
    });
}

/// Whether PRE's side of the chosen band is there. A PRE that declares another band will follow
/// the request; one that declares none (version 3) long after the band was sent predates bands.
pub(super) fn pre_band_state(
    band: Option<AttackBand>,
    pre_results: Option<&AttackBandResults>,
    sent_for: Option<Duration>,
) -> AttackPreBand {
    match (band, pre_results) {
        (None, _) => AttackPreBand::Off,
        (Some(band), Some(results)) if results.band == Some(band) => AttackPreBand::Same,
        (Some(_), Some(_)) => AttackPreBand::Waiting,
        (Some(_), None) => {
            if sent_for.is_some_and(|sent_for| sent_for >= PRE_BAND_PREDATES_AFTER) {
                AttackPreBand::Predates
            } else {
                AttackPreBand::Waiting
            }
        }
    }
}

fn request_anchors(
    coordinator: &SpectrumCoordinator,
    band: Option<AttackBand>,
    anchors: Vec<BandAnchor>,
) {
    if let Some(runtime) = coordinator.attack_runtime.as_ref() {
        runtime.request_band_anchors(band, anchors);
    }
}

/// The PRE onsets POST measures the band at: every matched pair whose PRE hit was kept, over the
/// tail PRE measured. The POST worker measures each once; asking every tick costs nothing.
fn band_anchors(
    pre_results: &AttackBandResults,
    post: &AttackHistory,
    pairs: &[AttackPairEvent],
) -> Vec<BandAnchor> {
    let Some(identity) = post.newest() else {
        return Vec::new();
    };
    pairs
        .iter()
        .filter(|pair| pair.kind == AttackPairEventKind::Matched)
        .filter_map(|pair| {
            let onset = pair.pre_event_sample?;
            let measure = pre_results.own_at(onset)?.measure?;
            Some(BandAnchor {
                event: AttackEvent {
                    generation: identity.generation,
                    sample_rate: identity.sample_rate,
                    channels: identity.channels,
                    definition_hash: identity.definition_hash,
                    event_sample: onset,
                    decision_sample: pair.decision_sample.max(onset),
                    value: pair.post_value.unwrap_or(0.0),
                },
                span_end_sample: measure.span_end_sample,
                span_end: measure.span_end,
            })
        })
        .collect()
}

/// POST measured at each matched PRE onset over the PRE detail's head, body and Sharpness
/// windows, read from the POST runtime's retained bins; only the head while either side's body is
/// not final. A pair whose PRE detail has not arrived, or whose windows are outside the bins, has
/// no anchored POST detail yet.
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
                body_end_sample: pre_detail
                    .features
                    .complete
                    .then_some(pre_detail.features.body_end_sample),
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

    /// Reads the view in place, without copying its histories: the band polls use this.
    pub fn with_attack_view<R>(
        &self,
        read: impl FnOnce(&AttackPairViewSnapshot) -> R,
    ) -> Option<R> {
        match self.attack_view.try_lock() {
            Ok(view) => Some(read(&view)),
            Err(TryLockError::WouldBlock) => None,
            Err(TryLockError::Poisoned(poisoned)) => Some(read(&poisoned.into_inner())),
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
