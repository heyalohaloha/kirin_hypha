//! Host-specific TRACE clock resolution from immutable Record observations.
//!
//! Hosts disagree about whether a plug-in node's reported project position already includes its
//! downstream presentation delay. We therefore retain the raw position and latency separately,
//! test a small factual model set only after the WAV start is known, and choose independently for
//! PRE and POST. Metric shapes never participate.

use crate::plugin_data::{Frame, PluginDataFile, TraceClockObservation};
use crate::record_expected::ExpectedWavMetadata;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum ClockModel {
    ProducerPlusOutput,
    Producer,
    RawMinusOutput,
    Raw,
    RawPlusOutput,
}

impl ClockModel {
    const ALL: [Self; 4] = [
        Self::Producer,
        Self::RawMinusOutput,
        Self::Raw,
        Self::RawPlusOutput,
    ];

    fn name(self) -> &'static str {
        match self {
            Self::ProducerPlusOutput => "producer_plus_output_latency",
            Self::Producer => "producer_position",
            Self::RawMinusOutput => "raw_minus_output_latency",
            Self::Raw => "raw_host_position",
            Self::RawPlusOutput => "raw_plus_output_latency",
        }
    }

    fn position(self, observation: &TraceClockObservation) -> Option<i64> {
        match self {
            Self::ProducerPlusOutput => observation
                .producer_position_samples?
                .checked_add(i64::from(observation.output_presentation_latency_samples?)),
            Self::Producer => observation.producer_position_samples,
            Self::RawMinusOutput => observation
                .raw_host_position_samples?
                .checked_sub(i64::from(observation.output_presentation_latency_samples?)),
            Self::Raw => observation.raw_host_position_samples,
            Self::RawPlusOutput => observation
                .raw_host_position_samples?
                .checked_add(i64::from(observation.output_presentation_latency_samples?)),
        }
    }

    fn render_origin(
        self,
        presentation_start: i64,
        raw_start: Option<i64>,
        first: Option<&TraceClockObservation>,
    ) -> Option<i64> {
        match self {
            // Keep the producer-owned render origin fixed while each observation receives its own
            // epoch's output latency. Moving the origin by the first epoch would cancel the very
            // PRE/POST difference this model is required to preserve.
            Self::ProducerPlusOutput => Some(presentation_start),
            // `BounceTake.host_*` is the producer-owned output-presentation range. Producer
            // positions and raw-minus-output positions are already on that axis; applying the
            // first callback's latency here would shift the WAV origin a second time.
            Self::Producer | Self::RawMinusOutput => Some(presentation_start),
            Self::Raw => raw_start,
            Self::RawPlusOutput => {
                raw_start?.checked_add(i64::from(first?.output_presentation_latency_samples?))
            }
        }
    }
}

#[derive(Debug, Clone)]
pub(crate) struct SideClockResolution {
    pub frames: Vec<Frame>,
    pub producer_slots: Vec<i64>,
    pub wav_slots: Vec<i64>,
    pub origin_position_samples: i64,
    pub model: &'static str,
}

pub(crate) fn resolve_exact_side(
    data: &PluginDataFile,
    expected: &ExpectedWavMetadata,
    expected_len: usize,
    slot_samples: i64,
) -> Option<SideClockResolution> {
    if expected_len == 0 || slot_samples <= 0 || data.trace_clock_observations.is_empty() {
        return None;
    }
    // Current producers retain output latency on every 100 ms observation. Apply it exactly once
    // at WAV binding: the metric payload remains untouched and only its factual sample position
    // changes. This path intentionally wins over the legacy grid-count models below.
    let aligned_model = ClockModel::ProducerPlusOutput;
    let aligned_origin = match expected.wav_time_reference_samples {
        Some(start) => i64::try_from(start).ok(),
        None => render_origin(data, expected, aligned_model),
    };
    if let Some(origin) = aligned_origin {
        if let Some((frames, producer_slots, wav_slots)) = select_latency_mapped_frames(
            &data.trace_clock_observations,
            origin,
            expected.expected_duration_samples,
            expected.expected_sample_rate,
            expected_len,
            slot_samples,
        ) {
            return Some(SideClockResolution {
                frames,
                producer_slots,
                wav_slots,
                origin_position_samples: origin,
                model: aligned_model.name(),
            });
        }
    }

    let mut best = None::<SideClockResolution>;
    for model in ClockModel::ALL {
        let origin = match expected.wav_time_reference_samples {
            Some(start) => i64::try_from(start).ok(),
            None => render_origin(data, expected, model),
        };
        let Some(origin) = origin else {
            continue;
        };
        if let Some((frames, producer_slots, wav_slots)) = select_sparse_monotonic_frames(
            &data.trace_clock_observations,
            origin,
            expected_len,
            slot_samples,
            model,
        ) {
            let candidate = SideClockResolution {
                frames,
                producer_slots,
                wav_slots,
                origin_position_samples: origin,
                model: model.name(),
            };
            if best
                .as_ref()
                .is_none_or(|current| candidate.frames.len() > current.frames.len())
            {
                best = Some(candidate);
            }
        }
    }
    best
}

fn select_latency_mapped_frames(
    observations: &[TraceClockObservation],
    origin: i64,
    duration_samples: u64,
    sample_rate: u32,
    expected_len: usize,
    slot_samples: i64,
) -> Option<(Vec<Frame>, Vec<i64>, Vec<i64>)> {
    if expected_len == 0 || slot_samples <= 0 || sample_rate == 0 {
        return None;
    }
    let duration = i64::try_from(duration_samples).ok()?;
    let end = origin.checked_add(duration)?;
    let first_full_window = origin.checked_add(slot_samples)?;
    let mut candidates = observations
        .iter()
        .filter_map(|observation| {
            let latency = observation.output_presentation_latency_samples?;
            let aligned = observation
                .producer_position_samples?
                .checked_add(i64::from(latency))?;
            (aligned >= first_full_window && aligned <= end).then_some((
                aligned,
                observation.frame.t_ms,
                observation.capture_epoch.unwrap_or(0),
                &observation.frame,
            ))
        })
        .collect::<Vec<_>>();
    candidates.sort_by_key(|(position, t_ms, epoch, _)| (*t_ms, *epoch, *position));

    let mut frames = Vec::with_capacity(candidates.len().min(expected_len));
    let mut producer_slots = Vec::with_capacity(frames.capacity());
    let mut wav_slots = Vec::with_capacity(frames.capacity());
    let mut previous_position = None;
    let mut previous_time = None;
    for (position, t_ms, _, source_frame) in candidates {
        if previous_position.is_some_and(|previous| position <= previous)
            || previous_time.is_some_and(|previous| t_ms <= previous)
        {
            return None;
        }
        let relative = position.checked_sub(origin)?;
        let mut frame = source_frame.clone();
        frame.t_ms = u64::try_from(relative).ok()?.saturating_mul(1_000) / u64::from(sample_rate);
        frames.push(frame);
        producer_slots.push(position);
        wav_slots.push(relative);
        previous_position = Some(position);
        previous_time = Some(t_ms);
    }
    (!frames.is_empty() && frames.len() <= expected_len).then_some((
        frames,
        producer_slots,
        wav_slots,
    ))
}

fn render_origin(
    data: &PluginDataFile,
    expected: &ExpectedWavMetadata,
    model: ClockModel,
) -> Option<i64> {
    let take = data.bounce_take.as_ref()?;
    let presentation_start = take.host_start_position_samples?;
    let presentation_end = take.host_end_position_samples?;
    let expected_duration = i64::try_from(expected.expected_duration_samples).ok()?;
    if take.sample_rate != expected.expected_sample_rate
        || take.duration_samples != expected.expected_duration_samples
        || presentation_end.checked_sub(presentation_start) != Some(expected_duration)
    {
        return None;
    }
    let raw_start = data
        .raw_host_clock_range
        .as_ref()
        .map(|range| range.start_position_samples);
    let first = data
        .trace_clock_observations
        .iter()
        .filter(|observation| observation.raw_host_position_samples.is_some())
        .min_by_key(|observation| observation.frame.t_ms);
    model.render_origin(presentation_start, raw_start, first)
}

fn select_sparse_monotonic_frames(
    observations: &[TraceClockObservation],
    origin: i64,
    expected_len: usize,
    slot_samples: i64,
    model: ClockModel,
) -> Option<(Vec<Frame>, Vec<i64>, Vec<i64>)> {
    if expected_len == 0 || slot_samples <= 0 {
        return None;
    }
    let mut candidates = observations
        .iter()
        .filter_map(|observation| {
            let position = model.position(observation)?;
            let relative = position.checked_sub(origin)?;
            if relative <= 0 || relative % slot_samples != 0 {
                return None;
            }
            let slot_index = usize::try_from(relative / slot_samples).ok()?;
            if slot_index == 0 || slot_index > expected_len {
                return None;
            }
            Some((
                slot_index,
                observation.frame.t_ms,
                observation.capture_epoch.unwrap_or(0),
                position,
                &observation.frame,
            ))
        })
        .collect::<Vec<_>>();
    candidates.sort_by_key(|(slot, t_ms, epoch, position, _)| (*slot, *t_ms, *epoch, *position));
    let mut selected_frames = Vec::with_capacity(candidates.len().min(expected_len));
    let mut producer_slots = Vec::with_capacity(selected_frames.capacity());
    let mut wav_slots = Vec::with_capacity(selected_frames.capacity());
    let mut last_time = None;
    let mut last_slot = 0;
    for (slot_index, t_ms, _, position, source_frame) in candidates {
        if slot_index == last_slot || last_time.is_some_and(|last| t_ms <= last) {
            continue;
        }
        let relative = i64::try_from(slot_index).ok()?.checked_mul(slot_samples)?;
        let mut frame = source_frame.clone();
        frame.t_ms = u64::try_from(slot_index).ok()?.checked_mul(100)?;
        selected_frames.push(frame);
        producer_slots.push(position);
        wav_slots.push(relative);
        last_slot = slot_index;
        last_time = Some(t_ms);
    }
    (!selected_frames.is_empty()).then_some((selected_frames, producer_slots, wav_slots))
}

/// Preserve reachability without inventing metrics when no factual clock model covers the WAV.
/// Current baked frames win; the observation journal repairs the specific latency-epoch case in
/// which the legacy single-epoch bake dropped later measured segments.
pub(crate) fn chronological_fallback_frames(
    data: &PluginDataFile,
    expected_len: usize,
) -> Vec<Frame> {
    let max_ms = (expected_len as u64).saturating_mul(100);
    let normalize = |frames: Vec<Frame>| {
        let mut by_slot = std::collections::BTreeMap::new();
        for frame in frames {
            if frame.t_ms == 0 || frame.t_ms > max_ms || frame.t_ms % 100 != 0 {
                continue;
            }
            by_slot.entry(frame.t_ms).or_insert(frame);
        }
        by_slot.into_values().collect::<Vec<_>>()
    };
    let baked_frames = normalize(data.frames.clone());
    let mut observations = data.trace_clock_observations.iter().collect::<Vec<_>>();
    observations.sort_by_key(|observation| {
        (
            observation.frame.t_ms,
            observation.capture_epoch.unwrap_or(0),
        )
    });
    let observed_frames = observations
        .into_iter()
        .map(|observation| observation.frame.clone())
        .collect::<Vec<_>>();
    let observed_frames = normalize(observed_frames);
    // Compare usable coverage after duration/grid normalization. Raw observation count also
    // includes callbacks before/after an offline render and therefore must not displace an already
    // complete producer-selected take merely because the journal is longer.
    let frames = if observed_frames.len() > baked_frames.len() {
        observed_frames
    } else {
        baked_frames
    };
    if !frames.is_empty() {
        return frames;
    }
    // Pre-v1.3 artifacts sometimes carried no usable per-frame t_ms. Keep their historical
    // record-order compatibility path, but only when there is no factual slot timestamp at all.
    data.frames
        .iter()
        .take(expected_len)
        .cloned()
        .enumerate()
        .map(|(index, mut frame)| {
            frame.t_ms = (index as u64 + 1).saturating_mul(100);
            frame
        })
        .collect()
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::plugin_data::{BounceTake, Role};

    fn frame(t_ms: u64, value: f64) -> Frame {
        Frame {
            t_ms,
            n_prime: None,
            sharpness: None,
            lufs_m: value,
            true_peak: value,
            crest: value.abs(),
            psr: None,
        }
    }

    fn observation(t_ms: u64, raw: i64, output: u32, epoch: u64) -> TraceClockObservation {
        TraceClockObservation {
            frame: frame(t_ms, t_ms as f64),
            producer_position_samples: raw.checked_sub(i64::from(output)),
            raw_host_position_samples: Some(raw),
            capture_epoch: Some(epoch),
            clock_source: Some("project_timeline".to_string()),
            presentation_latency_source: Some("vst3".to_string()),
            input_presentation_latency_samples: None,
            output_presentation_latency_samples: Some(output),
        }
    }

    fn data(observations: Vec<TraceClockObservation>) -> PluginDataFile {
        let mut data = PluginDataFile::new(
            "i".into(),
            "p".into(),
            "x".into(),
            Role::Pre,
            None,
            48_000,
            None,
            None,
            None,
            None,
        );
        data.trace_clock_observations = observations;
        data.raw_host_clock_range = Some(crate::plugin_data::HostClockRange {
            start_position_samples: 100_256,
            end_position_samples: 110_112,
        });
        data.bounce_take = Some(BounceTake {
            source: "render_clock_native".into(),
            time_axis: "native_samples".into(),
            alignment_status: "sample_count_ready".into(),
            sample_rate: 48_000,
            wav_start_sample: 0,
            wav_end_sample: 9_600,
            duration_samples: 9_600,
            duration_frames_48k: 9_600,
            start_t_ms: 0,
            end_t_ms: 200,
            trace_sample_count: 2,
            frame_count: 2,
            // Current producers store the output-presentation range here. The raw host range is
            // retained independently above and may have a different span when latency changes.
            host_start_position_samples: Some(100_000),
            host_end_position_samples: Some(109_600),
        });
        data
    }

    fn expected(start: Option<u64>) -> ExpectedWavMetadata {
        ExpectedWavMetadata {
            expected_duration_samples: 9_600,
            expected_sample_rate: 48_000,
            wav_time_reference_samples: start,
            wav_path: "/tmp/x.wav".into(),
            bounce_id: "b".into(),
            created_at_ms: 1,
            wav_file_size: Some(1),
            wav_mtime_ms: 1,
            wav_hash: Some("h".into()),
            consumed_at_ms: None,
            consumed_by_session_id: None,
        }
    }

    #[test]
    fn output_latency_changes_are_resolved_per_observation_across_epochs() {
        let data = data(vec![
            observation(100, 104_800, 256, 10),
            observation(200, 109_600, 512, 11),
        ]);
        let resolved = resolve_exact_side(&data, &expected(Some(100_000)), 2, 4_800)
            .expect("piecewise output latency");
        assert_eq!(resolved.model, "producer_plus_output_latency");
        assert_eq!(resolved.producer_slots, vec![104_800, 109_600]);
        assert_eq!(resolved.wav_slots, vec![4_800, 9_600]);
        assert_eq!(resolved.origin_position_samples, 100_000);
        assert_eq!(resolved.frames.len(), 2);
    }

    #[test]
    fn missing_bext_uses_presentation_origin_without_applying_latency_twice() {
        let data = data(vec![
            observation(100, 104_800, 256, 10),
            observation(200, 109_600, 512, 11),
        ]);
        let mut data = data;
        data.raw_host_clock_range = Some(crate::plugin_data::HostClockRange {
            start_position_samples: 100_000,
            end_position_samples: 109_600,
        });
        let resolved = resolve_exact_side(&data, &expected(None), 2, 4_800)
            .expect("producer presentation range resolves without BWF");
        assert_eq!(resolved.model, "producer_plus_output_latency");
        assert_eq!(resolved.producer_slots, vec![104_800, 109_600]);
    }

    #[test]
    fn missing_bext_raw_minus_output_uses_the_same_presentation_origin() {
        let mut first = observation(100, 105_056, 256, 10);
        let mut second = observation(200, 110_112, 512, 11);
        first.producer_position_samples = None;
        second.producer_position_samples = None;
        let resolved = resolve_exact_side(&data(vec![first, second]), &expected(None), 2, 4_800)
            .expect("raw-minus-output presentation range resolves without BWF");
        assert_eq!(resolved.model, "raw_minus_output_latency");
        assert_eq!(resolved.producer_slots, vec![104_800, 109_600]);
    }

    #[test]
    fn missing_bext_raw_host_model_uses_the_independent_raw_origin() {
        let mut first = observation(100, 104_800, 256, 10);
        let mut second = observation(200, 109_600, 512, 11);
        first.producer_position_samples = None;
        second.producer_position_samples = None;
        let mut data = data(vec![first, second]);
        data.raw_host_clock_range = Some(crate::plugin_data::HostClockRange {
            start_position_samples: 100_000,
            end_position_samples: 109_600,
        });
        let resolved = resolve_exact_side(&data, &expected(None), 2, 4_800)
            .expect("raw host range resolves without BWF");
        assert_eq!(resolved.model, "raw_host_position");
        assert_eq!(resolved.producer_slots, vec![104_800, 109_600]);
    }

    #[test]
    fn missing_bext_raw_plus_output_uses_raw_origin_and_first_latency() {
        let mut first = observation(100, 104_544, 256, 10);
        let mut second = observation(200, 109_088, 512, 11);
        first.producer_position_samples = None;
        second.producer_position_samples = None;
        let mut data = data(vec![first, second]);
        data.raw_host_clock_range = Some(crate::plugin_data::HostClockRange {
            start_position_samples: 99_744,
            end_position_samples: 109_088,
        });
        let resolved = resolve_exact_side(&data, &expected(None), 2, 4_800)
            .expect("raw-plus-output range resolves without BWF");
        assert_eq!(resolved.model, "raw_plus_output_latency");
        assert_eq!(resolved.producer_slots, vec![104_800, 109_600]);
    }

    #[test]
    fn raw_plus_output_supports_a_host_with_opposite_project_clock_semantics() {
        let mut first = observation(100, 104_544, 256, 1);
        let mut second = observation(200, 109_344, 256, 1);
        first.producer_position_samples = None;
        second.producer_position_samples = None;
        let resolved = resolve_exact_side(
            &data(vec![first, second]),
            &expected(Some(100_000)),
            2,
            4_800,
        )
        .expect("opposite host semantics");
        assert_eq!(resolved.model, "raw_plus_output_latency");
    }

    #[test]
    fn raw_minus_output_supports_a_host_reporting_pre_presentation_positions() {
        let mut first = observation(100, 105_056, 256, 1);
        let mut second = observation(200, 109_856, 256, 1);
        first.producer_position_samples = None;
        second.producer_position_samples = None;
        let resolved = resolve_exact_side(
            &data(vec![first, second]),
            &expected(Some(100_000)),
            2,
            4_800,
        )
        .expect("pre-presentation host semantics");
        assert_eq!(resolved.model, "raw_minus_output_latency");
    }

    #[test]
    fn chronological_fallback_keeps_later_latency_epochs_visible() {
        let data = data(vec![observation(100, 1, 0, 1), observation(200, 2, 64, 2)]);
        let frames = chronological_fallback_frames(&data, 2);
        assert_eq!(frames.len(), 2);
        assert_eq!(frames[0].t_ms, 100);
        assert_eq!(frames[1].t_ms, 200);
    }

    #[test]
    fn chronological_fallback_keeps_complete_baked_take_when_journal_has_extra_epochs() {
        let mut data = data(vec![
            observation(100, 104_800, 512, 1),
            observation(100, 104_800, 512, 2),
            observation(200, 109_600, 512, 2),
        ]);
        data.frames = vec![frame(100, -31.0), frame(200, -29.0)];

        let frames = chronological_fallback_frames(&data, 2);

        assert_eq!(frames.len(), 2);
        assert_eq!(frames[0].lufs_m, -31.0);
        assert_eq!(frames[1].lufs_m, -29.0);
    }

    #[test]
    fn anchored_resolution_keeps_a_missing_middle_slot_absent() {
        let mut expected = expected(Some(100_000));
        expected.expected_duration_samples = 14_400;
        let resolved = resolve_exact_side(
            &data(vec![
                observation(100, 104_800, 256, 10),
                observation(300, 114_400, 256, 10),
            ]),
            &expected,
            3,
            4_800,
        )
        .expect("two factual anchored slots");

        assert_eq!(resolved.wav_slots, vec![4_800, 14_400]);
        assert_eq!(
            resolved
                .frames
                .iter()
                .map(|frame| frame.t_ms)
                .collect::<Vec<_>>(),
            vec![100, 300]
        );
    }

    #[test]
    fn overflow_never_creates_a_false_clock_match() {
        let data = data(vec![observation(100, i64::MAX, 0, 1)]);
        assert!(resolve_exact_side(&data, &expected(Some(u64::MAX)), 1, 4_800).is_none());
    }

    #[test]
    fn long_generation_selection_advances_through_candidates_once() {
        const FRAME_COUNT: usize = 6_000;
        let observations = (1..=FRAME_COUNT)
            .map(|index| {
                let position = i64::try_from(index).unwrap() * 4_800;
                observation(index as u64 * 100, position, 0, 1)
            })
            .collect::<Vec<_>>();
        let (frames, producer_slots, wav_slots) = select_sparse_monotonic_frames(
            &observations,
            0,
            FRAME_COUNT,
            4_800,
            ClockModel::Producer,
        )
        .expect("ten-minute generation");
        assert_eq!(frames.len(), FRAME_COUNT);
        assert_eq!(frames.last().map(|frame| frame.t_ms), Some(600_000));
        assert_eq!(producer_slots.last(), Some(&28_800_000));
        assert_eq!(wav_slots.last(), Some(&28_800_000));
    }
}
