use std::time::Instant;

use super::{
    PostSession, SpectrumCoordinator, SpectrumViewStatus, PRESENTATION_HOLD, WARMUP_LIMIT,
};
use crate::perceptual::{
    difference_post_minus_pre as perceptual_difference_post_minus_pre, PerceptualDifference,
};
use crate::spectrum::{difference_post_minus_pre, SpectrumDifference};
use crate::spectrum_runtime::{PerceptualHistory, SpectrumHistory};

#[cfg(test)]
#[path = "spectrum_exchange_batch_tests.rs"]
mod batch_tests;

#[cfg(test)]
pub(super) fn newest_exact_difference(
    post: &SpectrumHistory,
    pre: &SpectrumHistory,
) -> Option<SpectrumDifference> {
    post.frames().rev().find_map(|post_frame| {
        pre.matching_presentation_end(post_frame.presentation_end_samples)
            .and_then(|pre_frame| difference_post_minus_pre(post_frame, pre_frame))
    })
}

pub(super) fn exact_spectrum_differences(
    post: &SpectrumHistory,
    pre: &SpectrumHistory,
) -> Vec<SpectrumDifference> {
    let pre = current_run(pre.frames(), |frame| frame.presentation_end_samples);
    current_run(post.frames(), |frame| frame.presentation_end_samples)
        .into_iter()
        .filter_map(|post_frame| {
            pre.iter()
                .find(|frame| frame.presentation_end_samples == post_frame.presentation_end_samples)
                .and_then(|pre_frame| difference_post_minus_pre(post_frame, pre_frame))
        })
        .collect()
}

#[cfg(test)]
pub(super) fn newest_exact_perceptual_difference(
    post: &PerceptualHistory,
    pre: &PerceptualHistory,
) -> Option<PerceptualDifference> {
    post.frames().rev().find_map(|post_frame| {
        pre.matching_presentation_end(post_frame.presentation_end_samples)
            .and_then(|pre_frame| perceptual_difference_post_minus_pre(post_frame, pre_frame))
    })
}

pub(super) fn exact_perceptual_differences(
    post: &PerceptualHistory,
    pre: &PerceptualHistory,
) -> Vec<PerceptualDifference> {
    let pre = current_run(pre.frames(), |frame| frame.presentation_end_samples);
    current_run(post.frames(), |frame| frame.presentation_end_samples)
        .into_iter()
        .filter_map(|post_frame| {
            pre.iter()
                .find(|frame| frame.presentation_end_samples == post_frame.presentation_end_samples)
                .and_then(|pre_frame| perceptual_difference_post_minus_pre(post_frame, pre_frame))
        })
        .collect()
}

fn current_run<'a, T: 'a>(
    frames: impl DoubleEndedIterator<Item = &'a T>,
    endpoint: impl Fn(&T) -> i64,
) -> Vec<&'a T> {
    // Worker retention is acquisition-ordered and can span a backwards seek. Recover only
    // the newest run on EACH side, never reinsert an old high endpoint ahead of new facts.
    let mut result = Vec::new();
    let mut newer = None;
    for frame in frames.rev() {
        let end = endpoint(frame);
        if newer.is_some_and(|value| end > value) {
            break;
        }
        if newer == Some(end) {
            continue;
        }
        newer = Some(end);
        result.push(frame);
    }
    result.reverse();
    result
}

fn joined_status<T>(
    session: &PostSession,
    now: Instant,
    local: Option<&T>,
    remote: Option<&T>,
    local_has_data: impl Fn(&T) -> bool,
) -> SpectrumViewStatus {
    if (local.is_some_and(&local_has_data) && remote.is_some_and(local_has_data))
        || session
            .started_at
            .is_some_and(|started| now.duration_since(started) >= WARMUP_LIMIT)
    {
        SpectrumViewStatus::Unavailable
    } else {
        SpectrumViewStatus::WarmingUp
    }
}

pub(super) fn store_joined_spectrum(
    coordinator: &SpectrumCoordinator,
    session: &mut PostSession,
    now: Instant,
    local: Option<&SpectrumHistory>,
    remote: Option<&SpectrumHistory>,
) {
    let differences = local.zip(remote).map_or_else(Vec::new, |(post, pre)| {
        exact_spectrum_differences(post, pre)
    });
    if let Some(difference) = differences.last() {
        let endpoint = difference.presentation_end_samples;
        let latest_endpoints = local
            .and_then(|history| history.newest())
            .zip(remote.and_then(|history| history.newest()))
            .map(|(post, pre)| (post.presentation_end_samples, pre.presentation_end_samples));
        let exact_endpoint_is_current =
            latest_endpoints.is_some_and(|(post, pre)| post == endpoint && pre == endpoint);
        if session.last_presented_end_samples != Some(endpoint) {
            let moved_backwards = session
                .last_presented_end_samples
                .is_some_and(|previous| endpoint < previous);
            // PRE and POST workers do not have to publish the first frame of a new transport run
            // in the same 30 Hz exchange tick. Confirm the backwards boundary from each side's
            // newest verified endpoint, then restart at their newest exact intersection. Requiring
            // both newest endpoints to be identical can reject every valid lower frame forever
            // when the two workers remain one frame apart. A one-sided late result still cannot
            // cross this gate.
            let both_sides_moved_backwards = session
                .last_presented_end_samples
                .zip(latest_endpoints)
                .is_some_and(|(previous, (post, pre))| post < previous && pre < previous);
            if moved_backwards && both_sides_moved_backwards {
                coordinator.store_spectrum_sequence(&differences, local, true);
            } else if !moved_backwards {
                coordinator.store_spectrum_sequence(&differences, local, false);
            } else {
                // Keep the prior exact fact only for the bounded presentation lease. If the
                // second side never crosses the boundary, normal unavailable handling below
                // remains authoritative instead of freezing the old frame indefinitely.
                if session
                    .last_presented_at
                    .is_some_and(|presented| now.duration_since(presented) < PRESENTATION_HOLD)
                {
                    return;
                }
                let status = joined_status(session, now, local, remote, |history| {
                    history.newest().is_some()
                });
                coordinator.store_spectrum_view(status, None, local);
                return;
            }
            session.last_presented_at = Some(now);
            session.last_presented_end_samples = Some(endpoint);
            return;
        }
        if exact_endpoint_is_current {
            return;
        }
    }
    if session
        .last_presented_at
        .is_some_and(|presented| now.duration_since(presented) < PRESENTATION_HOLD)
    {
        return;
    }
    let status = joined_status(session, now, local, remote, |history| {
        history.newest().is_some()
    });
    coordinator.store_spectrum_view(status, None, local);
}

pub(super) fn store_joined_perceptual(
    coordinator: &SpectrumCoordinator,
    session: &mut PostSession,
    now: Instant,
    local: Option<&PerceptualHistory>,
    remote: Option<&PerceptualHistory>,
) {
    let differences = local.zip(remote).map_or_else(Vec::new, |(post, pre)| {
        exact_perceptual_differences(post, pre)
    });
    if let Some(newest) = differences.last() {
        let endpoint = newest.presentation_end_samples;
        let exact_endpoint_is_current = local
            .and_then(|history| history.newest())
            .zip(remote.and_then(|history| history.newest()))
            .is_some_and(|(post, pre)| {
                post.presentation_end_samples == endpoint
                    && pre.presentation_end_samples == endpoint
            });
        if session.last_presented_end_samples != Some(endpoint) {
            coordinator.store_perceptual_view(SpectrumViewStatus::Active, &differences);
            session.last_presented_at = Some(now);
            session.last_presented_end_samples = Some(endpoint);
            return;
        }
        if exact_endpoint_is_current {
            return;
        }
    }
    if session
        .last_presented_at
        .is_some_and(|presented| now.duration_since(presented) < PRESENTATION_HOLD)
    {
        return;
    }
    let status = joined_status(session, now, local, remote, |history| {
        history.newest().is_some()
    });
    coordinator.store_perceptual_view(status, &[]);
}
