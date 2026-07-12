//! Producer-owned PRE/POST WAV-start clock.
//!
//! Metric values are deliberately absent from the clock decision. A Broadcast-Wave time
//! reference is the primary start anchor. When a DAW omits `bext`, the first complete dense window
//! shared by the PRE and POST presentation-normalised content clocks is the fallback Record-session anchor. Both lanes
//! are always read at the same absolute sample positions and are then rebased to WAV sample 0..N.

use std::collections::BTreeMap;

use crate::plugin_data::{Frame, PluginDataFile};
use crate::record_expected::ExpectedWavMetadata;

const FRAME_INTERVAL_MS: u64 = 100;

pub(crate) struct WavStartClockPlan {
    pub pre_frames: Vec<Frame>,
    pub post_frames: Vec<Frame>,
    pub canonical_slots: Vec<i64>,
    pub start_basis: &'static str,
}

pub(crate) fn build_wav_start_clock_plan(
    pre: &PluginDataFile,
    post: &PluginDataFile,
    expected: &ExpectedWavMetadata,
    expected_len: usize,
    slot_samples: i64,
) -> Option<WavStartClockPlan> {
    if expected_len == 0 || slot_samples <= 0 {
        return None;
    }
    let shared_session = pre
        .record_session_id
        .as_deref()
        .zip(post.record_session_id.as_deref())
        .is_some_and(|(pre_session, post_session)| {
            !pre_session.trim().is_empty() && pre_session == post_session
        });
    if !shared_session || !has_compatible_host_clocks(pre, post) {
        return None;
    }
    let pre_source = trace_source(pre);
    let post_source = trace_source(post);
    let selected_positions = select_wav_window(
        &pre_source,
        &post_source,
        expected,
        expected_len,
        slot_samples,
    )?;
    let pre_frames = selected_frames(&pre_source, &selected_positions)?;
    let post_frames = selected_frames(&post_source, &selected_positions)?;
    Some(WavStartClockPlan {
        pre_frames,
        post_frames,
        canonical_slots: selected_positions,
        start_basis: if expected.wav_time_reference_samples.is_some() {
            crate::trace_alignment::TRACE_ALIGNMENT_START_BWF
        } else {
            crate::trace_alignment::TRACE_ALIGNMENT_START_SHARED_CLOCK
        },
    })
}

pub(crate) fn has_compatible_host_clocks(pre: &PluginDataFile, post: &PluginDataFile) -> bool {
    let (Some(pre_clock), Some(post_clock)) = (pre.trace_clock.as_ref(), post.trace_clock.as_ref())
    else {
        return false;
    };
    pre_clock.basis == crate::trace_alignment::TRACE_CLOCK_BASIS
        && post_clock.basis == crate::trace_alignment::TRACE_CLOCK_BASIS
        && pre_clock.sample_rate > 0
        && pre_clock.sample_rate == post_clock.sample_rate
        && !pre_clock.sources.is_empty()
        && pre_clock.sources == post_clock.sources
}

fn trace_source(data: &PluginDataFile) -> BTreeMap<i64, Frame> {
    let context_is_usable = !data.trace_context_frames.is_empty()
        && data.trace_context_frames.len() == data.trace_context_slot_positions.len();
    let (positions, frames) = if context_is_usable {
        (
            &data.trace_context_slot_positions,
            &data.trace_context_frames,
        )
    } else {
        (&data.trace_slot_positions, &data.frames)
    };
    positions
        .iter()
        .copied()
        .zip(frames.iter().cloned())
        .collect()
}

fn selected_frames(source: &BTreeMap<i64, Frame>, positions: &[i64]) -> Option<Vec<Frame>> {
    positions
        .iter()
        .enumerate()
        .map(|(index, position)| {
            let t_ms = u64::try_from(index)
                .ok()?
                .checked_add(1)?
                .checked_mul(FRAME_INTERVAL_MS)?;
            let mut frame = source.get(position)?.clone();
            frame.t_ms = t_ms;
            Some(frame)
        })
        .collect()
}

fn select_wav_window(
    pre: &BTreeMap<i64, Frame>,
    post: &BTreeMap<i64, Frame>,
    expected: &ExpectedWavMetadata,
    expected_len: usize,
    slot_samples: i64,
) -> Option<Vec<i64>> {
    let window_is_complete = |positions: &[i64]| {
        positions.len() == expected_len
            && positions
                .windows(2)
                .all(|pair| pair[1].saturating_sub(pair[0]) == slot_samples)
            && positions
                .iter()
                .all(|position| pre.contains_key(position) && post.contains_key(position))
    };

    if let Some(wav_start) = expected.wav_time_reference_samples {
        let first = i64::try_from(wav_start).ok()?.checked_add(slot_samples)?;
        let anchored: Vec<i64> = (0..expected_len)
            .map(|index| {
                i64::try_from(index)
                    .ok()?
                    .checked_mul(slot_samples)?
                    .checked_add(first)
            })
            .collect::<Option<Vec<_>>>()?;
        if window_is_complete(&anchored) {
            return Some(anchored);
        }
        return None;
    }

    let shared_positions: Vec<i64> = pre
        .keys()
        .copied()
        .filter(|position| post.contains_key(position))
        .collect();
    shared_positions
        .windows(expected_len)
        .find(|window| window_is_complete(window))
        .map(<[i64]>::to_vec)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::plugin_data::{Role, TraceClock};

    fn frame(value: f64) -> Frame {
        Frame {
            t_ms: 0,
            n_prime: None,
            sharpness: None,
            lufs_m: value,
            true_peak: value * -3.0,
            crest: value.abs(),
            psr: None,
        }
    }

    fn source(positions: &[i64], values: &[f64]) -> BTreeMap<i64, Frame> {
        positions
            .iter()
            .copied()
            .zip(values.iter().copied().map(frame))
            .collect()
    }

    fn expected(wav_start: Option<u64>) -> ExpectedWavMetadata {
        ExpectedWavMetadata {
            expected_duration_samples: 14_400,
            expected_sample_rate: 48_000,
            wav_time_reference_samples: wav_start,
            wav_path: "/tmp/wav-start-clock.wav".to_string(),
            bounce_id: "bounce-wav-start-clock".to_string(),
            created_at_ms: 1,
            wav_file_size: Some(1),
            wav_mtime_ms: 1,
            wav_hash: Some("hash-wav-start-clock".to_string()),
            consumed_at_ms: None,
            consumed_by_session_id: None,
        }
    }

    fn plugin_data(role: Role, session: &str, source_name: &str) -> PluginDataFile {
        let mut data = PluginDataFile::new(
            "installation".to_string(),
            "project".to_string(),
            format!("{role:?}"),
            role,
            None,
            48_000,
            None,
            None,
            None,
            None,
        );
        data.record_session_id = Some(session.to_string());
        data.trace_clock = Some(TraceClock {
            basis: crate::trace_alignment::TRACE_CLOCK_BASIS.to_string(),
            origin_position_samples: 96_000,
            end_position_samples: 115_200,
            sample_rate: 48_000,
            sources: vec![source_name.to_string()],
        });
        data.trace_slot_positions = vec![100_800, 105_600, 110_400];
        data.frames = vec![frame(-30.0), frame(-20.0), frame(-10.0)];
        data
    }

    #[test]
    fn bwf_start_selects_identical_samples_even_when_shapes_disagree() {
        let positions = [96_000, 100_800, 105_600, 110_400, 115_200];
        let pre = source(&positions, &[-30.0, -8.0, -25.0, -12.0, -20.0]);
        let post = source(&positions, &[4.0, -40.0, 2.0, -35.0, 1.0]);

        let selected = select_wav_window(&pre, &post, &expected(Some(96_000)), 3, 4_800);

        assert_eq!(selected, Some(vec![100_800, 105_600, 110_400]));
    }

    #[test]
    fn bwf_start_never_falls_forward_to_a_different_complete_window() {
        let pre_positions = [100_800, 105_600, 110_400, 115_200];
        let post_positions = [105_600, 110_400, 115_200, 120_000];
        let pre = source(&pre_positions, &[-30.0, -8.0, -25.0, -12.0]);
        let post = source(&post_positions, &[-7.0, -6.0, -5.0, -4.0]);

        let selected = select_wav_window(&pre, &post, &expected(Some(96_000)), 3, 4_800);

        assert_eq!(selected, None);
    }

    #[test]
    fn out_of_range_bwf_start_cannot_saturate_into_a_false_window() {
        let positions = [i64::MAX - 9_600, i64::MAX - 4_800, i64::MAX];
        let pre = source(&positions, &[-3.0, -2.0, -1.0]);
        let post = source(&positions, &[-30.0, -20.0, -10.0]);

        let selected = select_wav_window(&pre, &post, &expected(Some(u64::MAX)), 3, 4_800);

        assert_eq!(selected, None);
    }

    #[test]
    fn missing_bext_uses_first_dense_shared_record_clock_without_shape_matching() {
        let pre_positions = [91_200, 96_000, 100_800, 105_600, 110_400];
        let post_positions = [96_000, 100_800, 105_600, 110_400, 115_200];
        let pre = source(&pre_positions, &[-1.0, -2.0, -3.0, -4.0, -5.0]);
        let post = source(&post_positions, &[-50.0, 10.0, -40.0, 20.0, -30.0]);

        let selected = select_wav_window(&pre, &post, &expected(None), 3, 4_800);

        assert_eq!(selected, Some(vec![96_000, 100_800, 105_600]));
    }

    #[test]
    fn plan_requires_one_record_session_and_one_host_clock_provenance() {
        let pre = plugin_data(Role::Pre, "session", "project_timeline");
        let mut post = plugin_data(Role::Post, "session", "project_timeline");

        assert!(
            build_wav_start_clock_plan(&pre, &post, &expected(Some(96_000)), 3, 4_800).is_some()
        );

        post.record_session_id = Some("other-session".to_string());
        assert!(
            build_wav_start_clock_plan(&pre, &post, &expected(Some(96_000)), 3, 4_800).is_none()
        );

        post.record_session_id = Some("session".to_string());
        post.trace_clock.as_mut().unwrap().sources = vec!["different_clock".to_string()];
        assert!(
            build_wav_start_clock_plan(&pre, &post, &expected(Some(96_000)), 3, 4_800).is_none()
        );
    }

    #[test]
    fn missing_bext_plan_declares_shared_record_clock_without_metric_inference() {
        let pre = plugin_data(Role::Pre, "session", "project_timeline");
        let mut post = plugin_data(Role::Post, "session", "project_timeline");
        post.frames = vec![frame(40.0), frame(-60.0), frame(30.0)];

        let plan = build_wav_start_clock_plan(&pre, &post, &expected(None), 3, 4_800)
            .expect("shared Record clock plan");

        assert_eq!(
            plan.start_basis,
            crate::trace_alignment::TRACE_ALIGNMENT_START_SHARED_CLOCK
        );
        assert_eq!(plan.canonical_slots, vec![100_800, 105_600, 110_400]);
        assert_eq!(plan.pre_frames[0].lufs_m, -30.0);
        assert_eq!(plan.post_frames[0].lufs_m, 40.0);
    }
}
