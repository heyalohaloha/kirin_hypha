//! Producer-owned PRE/POST WAV-start clock.
//!
//! Metric values are deliberately absent from the clock decision. A Broadcast-Wave time
//! reference is the strongest start anchor. When the WAV has no `bext`, the producer uses the
//! exact contiguous DAW/render range observed for this Record generation. Metric shapes and the
//! relative position of a frame inside the broader Keep history never participate in the decision.

use std::collections::BTreeMap;

use crate::plugin_data::{Frame, PluginDataFile};
use crate::record_expected::ExpectedWavMetadata;

const FRAME_INTERVAL_MS: u64 = 100;

pub(crate) struct WavStartClockPlan {
    pub pre_frames: Vec<Frame>,
    pub post_frames: Vec<Frame>,
    /// Producer/DAW positions used only to select the exact source frames.
    pub producer_slots: Vec<i64>,
    /// Public TRACE positions on the dropped WAV axis. The first frame is the end of the first
    /// 100 ms measurement window, so this always starts at `slot_samples`, never at a DAW offset.
    pub wav_slots: Vec<i64>,
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
    if !shared_session {
        return None;
    }
    if pre.sample_rate == 0
        || pre.sample_rate != post.sample_rate
        || pre.sample_rate != expected.expected_sample_rate
    {
        return None;
    }
    // BWF is the exact WAV sample-0 anchor, so host-clock provenance is optional on that path.
    // Without BWF, the shared producer render range still needs two compatible native clocks.
    if expected.wav_time_reference_samples.is_none() && !has_compatible_host_clocks(pre, post) {
        return None;
    }
    // Current producers commit exactly one direct frame for every direct slot. The direct Record
    // epoch may include measured lead/tail, but the exact BWF/render window must be present on both
    // sides. Legacy context fields may deserialize but can never repair a missing direct slot.
    let pre_source = direct_trace_source(pre, expected_len)?;
    let post_source = direct_trace_source(post, expected_len)?;
    let render_range = shared_producer_render_range(pre, post, expected);
    let (selected_positions, start_basis) = select_wav_window(
        &pre_source,
        &post_source,
        expected,
        expected_len,
        slot_samples,
        render_range,
    )?;
    let pre_frames = selected_frames(&pre_source, &selected_positions)?;
    let post_frames = selected_frames(&post_source, &selected_positions)?;
    let wav_slots = (1..=expected_len)
        .map(|index| i64::try_from(index).ok()?.checked_mul(slot_samples))
        .collect::<Option<Vec<_>>>()?;
    Some(WavStartClockPlan {
        pre_frames,
        post_frames,
        producer_slots: selected_positions,
        wav_slots,
        start_basis,
    })
}

pub(crate) fn has_compatible_host_clocks(pre: &PluginDataFile, post: &PluginDataFile) -> bool {
    let (Some(pre_clock), Some(post_clock)) = (pre.trace_clock.as_ref(), post.trace_clock.as_ref())
    else {
        return false;
    };
    let sources_are_known = |sources: &[String]| {
        !sources.is_empty()
            && sources.iter().all(|source| {
                matches!(
                    source.as_str(),
                    "project_timeline" | "audio_render_timeline"
                )
            })
    };
    pre_clock.basis == crate::trace_alignment::TRACE_CLOCK_BASIS
        && post_clock.basis == crate::trace_alignment::TRACE_CLOCK_BASIS
        && pre_clock.sample_rate > 0
        && pre_clock.sample_rate == post_clock.sample_rate
        && sources_are_known(&pre_clock.sources)
        && sources_are_known(&post_clock.sources)
}

fn direct_trace_source(data: &PluginDataFile, expected_len: usize) -> Option<BTreeMap<i64, Frame>> {
    if data.frames.len() < expected_len
        || data.frames.len() != data.trace_slot_positions.len()
        || !data
            .trace_slot_positions
            .windows(2)
            .all(|pair| pair[0] < pair[1])
    {
        return None;
    }
    Some(
        data.trace_slot_positions
            .iter()
            .copied()
            .zip(data.frames.iter().cloned())
            .collect(),
    )
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
    render_range: Option<(i64, i64)>,
) -> Option<(Vec<i64>, &'static str)> {
    let window_is_complete = |positions: &[i64]| {
        positions.len() == expected_len
            && positions
                .windows(2)
                .all(|pair| pair[1].saturating_sub(pair[0]) == slot_samples)
            && positions
                .iter()
                .all(|position| pre.contains_key(position) && post.contains_key(position))
    };

    let (wav_start, start_basis) = if let Some(wav_start) = expected.wav_time_reference_samples {
        (
            i64::try_from(wav_start).ok()?,
            crate::trace_alignment::TRACE_ALIGNMENT_START_BWF,
        )
    } else {
        let (start, end) = render_range?;
        let duration = i64::try_from(expected.expected_duration_samples).ok()?;
        if end.checked_sub(start) != Some(duration) {
            return None;
        }
        (
            start,
            crate::trace_alignment::TRACE_ALIGNMENT_START_RENDER_RANGE,
        )
    };
    let first = wav_start.checked_add(slot_samples)?;
    let anchored: Vec<i64> = (0..expected_len)
        .map(|index| {
            i64::try_from(index)
                .ok()?
                .checked_mul(slot_samples)?
                .checked_add(first)
        })
        .collect::<Option<Vec<_>>>()?;
    if window_is_complete(&anchored) {
        return Some((anchored, start_basis));
    }
    None
}

fn shared_producer_render_range(
    pre: &PluginDataFile,
    post: &PluginDataFile,
    expected: &ExpectedWavMetadata,
) -> Option<(i64, i64)> {
    let pre_take = pre.bounce_take.as_ref()?;
    let post_take = post.bounce_take.as_ref()?;
    let pre_range = (
        pre_take.host_start_position_samples?,
        pre_take.host_end_position_samples?,
    );
    let post_range = (
        post_take.host_start_position_samples?,
        post_take.host_end_position_samples?,
    );
    let duration = i64::try_from(expected.expected_duration_samples).ok()?;
    (pre_range == post_range
        && pre_take.sample_rate == expected.expected_sample_rate
        && post_take.sample_rate == expected.expected_sample_rate
        && pre_take.duration_samples == expected.expected_duration_samples
        && post_take.duration_samples == expected.expected_duration_samples
        && pre_range.1.checked_sub(pre_range.0) == Some(duration))
    .then_some(pre_range)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::plugin_data::{BounceTake, Role, TraceClock};

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

    fn expected_96k_15s(wav_start: u64) -> ExpectedWavMetadata {
        ExpectedWavMetadata {
            expected_duration_samples: 1_440_000,
            expected_sample_rate: 96_000,
            wav_time_reference_samples: Some(wav_start),
            wav_path: "/tmp/wav-start-clock-96k.wav".to_string(),
            bounce_id: "bounce-wav-start-clock-96k".to_string(),
            created_at_ms: 1,
            wav_file_size: Some(1),
            wav_mtime_ms: 1,
            wav_hash: Some("hash-wav-start-clock-96k".to_string()),
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
        data.bounce_take = Some(BounceTake {
            source: "render_clock_native".to_string(),
            time_axis: "native_samples".to_string(),
            alignment_status: "sample_count_ready".to_string(),
            sample_rate: 48_000,
            wav_start_sample: 0,
            wav_end_sample: 14_400,
            duration_samples: 14_400,
            duration_frames_48k: 14_400,
            start_t_ms: 0,
            end_t_ms: 300,
            trace_sample_count: 3,
            frame_count: 3,
            host_start_position_samples: Some(96_000),
            host_end_position_samples: Some(110_400),
        });
        data
    }

    #[test]
    fn bwf_start_selects_identical_samples_even_when_shapes_disagree() {
        let positions = [96_000, 100_800, 105_600, 110_400, 115_200];
        let pre = source(&positions, &[-30.0, -8.0, -25.0, -12.0, -20.0]);
        let post = source(&positions, &[4.0, -40.0, 2.0, -35.0, 1.0]);

        let selected = select_wav_window(&pre, &post, &expected(Some(96_000)), 3, 4_800, None);

        assert_eq!(
            selected,
            Some((
                vec![100_800, 105_600, 110_400],
                crate::trace_alignment::TRACE_ALIGNMENT_START_BWF,
            ))
        );
    }

    #[test]
    fn bwf_start_never_falls_forward_to_a_different_complete_window() {
        let pre_positions = [100_800, 105_600, 110_400, 115_200];
        let post_positions = [105_600, 110_400, 115_200, 120_000];
        let pre = source(&pre_positions, &[-30.0, -8.0, -25.0, -12.0]);
        let post = source(&post_positions, &[-7.0, -6.0, -5.0, -4.0]);

        let selected = select_wav_window(&pre, &post, &expected(Some(96_000)), 3, 4_800, None);

        assert_eq!(selected, None);
    }

    #[test]
    fn out_of_range_bwf_start_cannot_saturate_into_a_false_window() {
        let positions = [i64::MAX - 9_600, i64::MAX - 4_800, i64::MAX];
        let pre = source(&positions, &[-3.0, -2.0, -1.0]);
        let post = source(&positions, &[-30.0, -20.0, -10.0]);

        let selected = select_wav_window(&pre, &post, &expected(Some(u64::MAX)), 3, 4_800, None);

        assert_eq!(selected, None);
    }

    #[test]
    fn missing_bext_uses_the_exact_render_start_not_a_dense_tail() {
        let pre_positions = [91_200, 96_000, 100_800, 105_600, 110_400];
        let post_positions = [96_000, 100_800, 105_600, 110_400, 115_200];
        let pre = source(&pre_positions, &[-1.0, -2.0, -3.0, -4.0, -5.0]);
        let post = source(&post_positions, &[-50.0, 10.0, -40.0, 20.0, -30.0]);

        let selected = select_wav_window(
            &pre,
            &post,
            &expected(None),
            3,
            4_800,
            Some((91_200, 105_600)),
        );

        assert_eq!(
            selected,
            Some((
                vec![96_000, 100_800, 105_600],
                crate::trace_alignment::TRACE_ALIGNMENT_START_RENDER_RANGE,
            ))
        );
    }

    #[test]
    fn missing_bext_never_promotes_dense_frames_without_a_producer_range() {
        let positions = [96_000, 100_800, 105_600, 110_400];
        let pre = source(&positions, &[-1.0, -2.0, -3.0, -4.0]);
        let post = source(&positions, &[-10.0, -20.0, -30.0, -40.0]);

        assert_eq!(
            select_wav_window(&pre, &post, &expected(None), 3, 4_800, None),
            None
        );
    }

    #[test]
    fn bwf_plan_requires_one_record_session_and_exact_direct_slots_not_a_host_clock() {
        let mut pre = plugin_data(Role::Pre, "session", "project_timeline");
        let mut post = plugin_data(Role::Post, "session", "project_timeline");

        assert!(
            build_wav_start_clock_plan(&pre, &post, &expected(Some(96_000)), 3, 4_800).is_some()
        );

        post.record_session_id = Some("other-session".to_string());
        assert!(
            build_wav_start_clock_plan(&pre, &post, &expected(Some(96_000)), 3, 4_800).is_none()
        );

        post.record_session_id = Some("session".to_string());
        pre.trace_clock = None;
        post.trace_clock = None;
        assert!(
            build_wav_start_clock_plan(&pre, &post, &expected(Some(96_000)), 3, 4_800).is_some()
        );

        post.trace_slot_positions.pop();
        assert!(
            build_wav_start_clock_plan(&pre, &post, &expected(Some(96_000)), 3, 4_800).is_none()
        );
    }

    #[test]
    fn bwf_plan_selects_exact_slots_from_same_record_epoch_lead_and_tail() {
        let mut pre = plugin_data(Role::Pre, "session", "project_timeline");
        let mut post = plugin_data(Role::Post, "session", "project_timeline");
        let positions = vec![96_000, 100_800, 105_600, 110_400, 115_200];
        pre.trace_slot_positions = positions.clone();
        post.trace_slot_positions = positions;
        pre.frames = vec![
            frame(-90.0),
            frame(-30.0),
            frame(-20.0),
            frame(-10.0),
            frame(-80.0),
        ];
        post.frames = vec![
            frame(90.0),
            frame(10.0),
            frame(20.0),
            frame(30.0),
            frame(80.0),
        ];

        let plan = build_wav_start_clock_plan(&pre, &post, &expected(Some(96_000)), 3, 4_800)
            .expect("direct Record lead/tail may surround the exact WAV window");

        assert_eq!(plan.producer_slots, vec![100_800, 105_600, 110_400]);
        assert_eq!(
            plan.pre_frames
                .iter()
                .map(|frame| frame.lufs_m)
                .collect::<Vec<_>>(),
            vec![-30.0, -20.0, -10.0]
        );
        assert_eq!(
            plan.post_frames
                .iter()
                .map(|frame| frame.lufs_m)
                .collect::<Vec<_>>(),
            vec![10.0, 20.0, 30.0]
        );
    }

    #[test]
    fn render_range_plan_still_requires_compatible_known_host_clocks() {
        let pre = plugin_data(Role::Pre, "session", "project_timeline");
        let mut post = plugin_data(Role::Post, "session", "project_timeline");
        assert!(build_wav_start_clock_plan(&pre, &post, &expected(None), 3, 4_800).is_some());

        post.trace_clock = None;
        assert!(build_wav_start_clock_plan(&pre, &post, &expected(None), 3, 4_800).is_none());

        post.trace_clock = plugin_data(Role::Post, "session", "unknown_clock").trace_clock;
        assert!(build_wav_start_clock_plan(&pre, &post, &expected(None), 3, 4_800).is_none());
    }

    #[test]
    fn mixed_au_vst3_clock_sources_are_known_and_share_the_presentation_axis() {
        let pre = plugin_data(Role::Pre, "session", "audio_render_timeline");
        let post = plugin_data(Role::Post, "session", "project_timeline");

        assert!(has_compatible_host_clocks(&pre, &post));
        assert!(
            build_wav_start_clock_plan(&pre, &post, &expected(Some(96_000)), 3, 4_800).is_some()
        );
    }

    #[test]
    fn missing_bext_plan_uses_producer_render_range_with_different_metric_shapes() {
        let pre = plugin_data(Role::Pre, "session", "project_timeline");
        let mut post = plugin_data(Role::Post, "session", "project_timeline");
        post.frames = vec![frame(40.0), frame(-60.0), frame(30.0)];

        let plan = build_wav_start_clock_plan(&pre, &post, &expected(None), 3, 4_800)
            .expect("producer render-range plan");
        assert_eq!(
            plan.start_basis,
            crate::trace_alignment::TRACE_ALIGNMENT_START_RENDER_RANGE
        );
        assert_ne!(plan.pre_frames[0].lufs_m, plan.post_frames[0].lufs_m);
    }

    #[test]
    fn context_cannot_repair_missing_direct_bwf_slots() {
        let mut pre = plugin_data(Role::Pre, "session", "project_timeline");
        let mut post = plugin_data(Role::Post, "session", "project_timeline");
        let positions = [95_940, 96_000, 100_800, 105_600, 110_400];
        pre.trace_context_slot_positions = positions.to_vec();
        pre.trace_context_frames = vec![
            frame(-31.0),
            frame(-30.0),
            frame(-20.0),
            frame(-10.0),
            frame(-5.0),
        ];
        post.trace_context_slot_positions = positions.to_vec();
        post.trace_context_frames = vec![
            frame(20.0),
            frame(10.0),
            frame(4.0),
            frame(-40.0),
            frame(2.0),
        ];
        post.frames.clear();
        post.trace_slot_positions.clear();
        pre.trace_clock = None;
        post.trace_clock = None;

        assert!(
            build_wav_start_clock_plan(&pre, &post, &expected(Some(96_000)), 3, 4_800).is_none(),
            "Watch context must never manufacture a missing producer-direct POST"
        );
    }

    #[test]
    fn context_cannot_replace_a_missing_anchored_direct_slot() {
        let mut pre = plugin_data(Role::Pre, "session", "project_timeline");
        let mut post = plugin_data(Role::Post, "session", "project_timeline");
        let exact_context = vec![100_800, 105_600, 110_400];
        pre.trace_context_slot_positions = exact_context.clone();
        pre.trace_context_frames = vec![frame(-20.0), frame(-10.0), frame(-5.0)];
        post.trace_context_slot_positions = exact_context;
        post.trace_context_frames = vec![frame(4.0), frame(-40.0), frame(2.0)];
        post.trace_slot_positions = vec![100_800, 110_400, 115_200];
        pre.trace_clock = None;
        post.trace_clock = None;

        assert!(
            build_wav_start_clock_plan(&pre, &post, &expected(Some(96_000)), 3, 4_800).is_none()
        );
    }

    #[test]
    fn studio_one_96k_bwf_uses_exact_150_direct_slots_and_ignores_context() {
        const WAV_START: i64 = 6_480_000;
        const SLOT_SAMPLES: i64 = 9_600;
        let direct_positions = (6_470_400_i64..=7_920_000)
            .step_by(SLOT_SAMPLES as usize)
            .collect::<Vec<_>>();
        assert_eq!(
            direct_positions.len(),
            152,
            "failed generation's full producer grid"
        );
        let mut context_positions = vec![6_475_940, WAV_START];
        context_positions.extend((1_i64..=151).map(|index| WAV_START + index * SLOT_SAMPLES));
        let context_frames: Vec<Frame> = context_positions
            .iter()
            .enumerate()
            .map(|(index, _)| frame(1_000.0 + index as f64))
            .collect();
        let mut pre = plugin_data(Role::Pre, "session-96k", "project_timeline");
        let mut post = plugin_data(Role::Post, "session-96k", "project_timeline");
        for (side, base_value) in [(&mut pre, -30.0), (&mut post, 20.0)] {
            side.sample_rate = 96_000;
            side.source_format = 96_000;
            side.trace_slot_positions = direct_positions.clone();
            side.frames = (0..direct_positions.len())
                .map(|index| frame(base_value + index as f64 * 0.01))
                .collect();
            side.trace_context_slot_positions = context_positions.clone();
            side.trace_context_frames = context_frames.clone();
            side.trace_clock = None;
        }

        let plan = build_wav_start_clock_plan(
            &pre,
            &post,
            &expected_96k_15s(WAV_START as u64),
            150,
            SLOT_SAMPLES,
        )
        .expect("exact 96 kHz BWF window");

        assert_eq!(plan.producer_slots.len(), 150);
        assert_eq!(plan.producer_slots.first(), Some(&6_489_600));
        assert_eq!(plan.producer_slots.last(), Some(&7_920_000));
        assert_eq!(plan.wav_slots.first(), Some(&9_600));
        assert_eq!(plan.wav_slots.last(), Some(&1_440_000));
        assert_eq!(plan.pre_frames.len(), 150);
        assert_eq!(plan.post_frames.len(), 150);
        assert_eq!(plan.pre_frames[0].lufs_m, -29.98);
        assert_eq!(plan.post_frames[0].lufs_m, 20.02);
    }
}
