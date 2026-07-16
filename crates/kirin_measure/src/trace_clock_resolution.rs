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
            Self::Producer => "producer_position",
            Self::RawMinusOutput => "raw_minus_output_latency",
            Self::Raw => "raw_host_position",
            Self::RawPlusOutput => "raw_plus_output_latency",
        }
    }

    fn position(self, observation: &TraceClockObservation) -> Option<i64> {
        match self {
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

    fn origin(self, raw_start: i64, first: &TraceClockObservation) -> Option<i64> {
        match self {
            Self::Producer => {
                let raw = first.raw_host_position_samples?;
                let producer = first.producer_position_samples?;
                raw_start.checked_add(producer.checked_sub(raw)?)
            }
            Self::RawMinusOutput => {
                raw_start.checked_sub(i64::from(first.output_presentation_latency_samples?))
            }
            Self::Raw => Some(raw_start),
            Self::RawPlusOutput => {
                raw_start.checked_add(i64::from(first.output_presentation_latency_samples?))
            }
        }
    }
}

#[derive(Debug, Clone)]
pub(crate) struct SideClockResolution {
    pub frames: Vec<Frame>,
    pub producer_slots: Vec<i64>,
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
    for model in ClockModel::ALL {
        let origin = match expected.wav_time_reference_samples {
            Some(start) => i64::try_from(start).ok(),
            None => render_origin(data, expected, model),
        };
        let Some(origin) = origin else {
            continue;
        };
        let Some(targets) = target_slots(origin, expected_len, slot_samples) else {
            continue;
        };
        if let Some(frames) =
            select_monotonic_frames(&data.trace_clock_observations, &targets, model)
        {
            return Some(SideClockResolution {
                frames,
                producer_slots: targets,
                model: model.name(),
            });
        }
    }
    None
}

fn render_origin(
    data: &PluginDataFile,
    expected: &ExpectedWavMetadata,
    model: ClockModel,
) -> Option<i64> {
    let take = data.bounce_take.as_ref()?;
    let raw_start = take.host_start_position_samples?;
    let raw_end = take.host_end_position_samples?;
    let expected_duration = i64::try_from(expected.expected_duration_samples).ok()?;
    if take.sample_rate != expected.expected_sample_rate
        || take.duration_samples != expected.expected_duration_samples
        || raw_end.checked_sub(raw_start) != Some(expected_duration)
    {
        return None;
    }
    let first = data
        .trace_clock_observations
        .iter()
        .filter(|observation| observation.raw_host_position_samples.is_some())
        .min_by_key(|observation| observation.frame.t_ms)?;
    model.origin(raw_start, first)
}

fn target_slots(origin: i64, expected_len: usize, slot_samples: i64) -> Option<Vec<i64>> {
    (1..=expected_len)
        .map(|index| {
            i64::try_from(index)
                .ok()?
                .checked_mul(slot_samples)?
                .checked_add(origin)
        })
        .collect()
}

fn select_monotonic_frames(
    observations: &[TraceClockObservation],
    targets: &[i64],
    model: ClockModel,
) -> Option<Vec<Frame>> {
    let mut candidates = observations
        .iter()
        .filter_map(|observation| {
            Some((
                observation.frame.t_ms,
                observation.capture_epoch.unwrap_or(0),
                model.position(observation)?,
                &observation.frame,
            ))
        })
        .collect::<Vec<_>>();
    candidates.sort_by_key(|(t_ms, epoch, position, _)| (*t_ms, *epoch, *position));
    let mut selected = Vec::with_capacity(targets.len());
    let mut last_time = None;
    for (index, target) in targets.iter().enumerate() {
        let candidate = candidates.iter().find(|(t_ms, _, position, _)| {
            *position == *target && last_time.is_none_or(|last| *t_ms > last)
        })?;
        last_time = Some(candidate.0);
        let mut frame = candidate.3.clone();
        frame.t_ms = (index as u64 + 1).checked_mul(100)?;
        selected.push(frame);
    }
    Some(selected)
}

/// Preserve reachability without inventing metrics when no factual clock model covers the WAV.
/// Current baked frames win; the observation journal repairs the specific latency-epoch case in
/// which the legacy single-epoch bake dropped later measured segments.
pub(crate) fn chronological_fallback_frames(
    data: &PluginDataFile,
    expected_len: usize,
) -> Vec<Frame> {
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
    let mut frames = if observed_frames.len() > data.frames.len() {
        observed_frames
    } else {
        data.frames.clone()
    };
    frames.truncate(expected_len);
    for (index, frame) in frames.iter_mut().enumerate() {
        frame.t_ms = (index as u64 + 1).saturating_mul(100);
    }
    frames
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
            host_start_position_samples: Some(100_256),
            host_end_position_samples: Some(109_856),
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
            observation(100, 105_056, 256, 10),
            observation(200, 110_112, 512, 11),
        ]);
        let resolved = resolve_exact_side(&data, &expected(Some(100_000)), 2, 4_800)
            .expect("piecewise output latency");
        assert_eq!(resolved.model, "producer_position");
        assert_eq!(resolved.producer_slots, vec![104_800, 109_600]);
        assert_eq!(resolved.frames.len(), 2);
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
    fn overflow_never_creates_a_false_clock_match() {
        let data = data(vec![observation(100, i64::MAX, 0, 1)]);
        assert!(resolve_exact_side(&data, &expected(Some(u64::MAX)), 1, 4_800).is_none());
    }
}
