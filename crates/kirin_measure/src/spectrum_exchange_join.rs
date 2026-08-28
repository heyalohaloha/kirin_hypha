use std::time::Instant;

use super::{PostSession, SpectrumCoordinator, SpectrumViewStatus, WARMUP_LIMIT};
use crate::perceptual::{
    difference_post_minus_pre as perceptual_difference_post_minus_pre, PerceptualDifference,
};
use crate::spectrum::{difference_post_minus_pre, SpectrumDifference};
use crate::spectrum_runtime::{PerceptualHistory, SpectrumHistory};

pub(super) fn newest_exact_difference(
    post: &SpectrumHistory,
    pre: &SpectrumHistory,
) -> Option<SpectrumDifference> {
    post.frames().rev().find_map(|post_frame| {
        pre.matching_presentation_end(post_frame.presentation_end_samples)
            .and_then(|pre_frame| difference_post_minus_pre(post_frame, pre_frame))
    })
}

pub(super) fn newest_exact_perceptual_difference(
    post: &PerceptualHistory,
    pre: &PerceptualHistory,
) -> Option<PerceptualDifference> {
    post.frames().rev().find_map(|post_frame| {
        pre.matching_presentation_end(post_frame.presentation_end_samples)
            .and_then(|pre_frame| perceptual_difference_post_minus_pre(post_frame, pre_frame))
    })
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
    session: &PostSession,
    now: Instant,
    local: Option<&SpectrumHistory>,
    remote: Option<&SpectrumHistory>,
) {
    if let Some(difference) = local
        .zip(remote)
        .and_then(|(post, pre)| newest_exact_difference(post, pre))
    {
        coordinator.store_view(SpectrumViewStatus::Active, Some(difference), None);
        return;
    }
    let status = joined_status(session, now, local, remote, |history| {
        history.newest().is_some()
    });
    coordinator.store_view(status, None, None);
}

pub(super) fn store_joined_perceptual(
    coordinator: &SpectrumCoordinator,
    session: &PostSession,
    now: Instant,
    local: Option<&PerceptualHistory>,
    remote: Option<&PerceptualHistory>,
) {
    if let Some(difference) = local
        .zip(remote)
        .and_then(|(post, pre)| newest_exact_perceptual_difference(post, pre))
    {
        coordinator.store_view(SpectrumViewStatus::Active, None, Some(difference));
        return;
    }
    let status = joined_status(session, now, local, remote, |history| {
        history.newest().is_some()
    });
    coordinator.store_view(status, None, None);
}
