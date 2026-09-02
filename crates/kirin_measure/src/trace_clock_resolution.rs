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
    /// Producer content positions relative to this side's factual WAV origin. Output
    /// presentation latency is deliberately absent: this is the independent subtraction clock.
    /// A position may be negative when downstream PDC presents pre-origin producer content inside
    /// the WAV. It remains a private join key and never replaces the public WAV slot above.
    pub comparison_slots: Vec<i64>,
    /// Untouched host positions retained one-to-one with `frames` when every selected
    /// observation carried that diagnostic.
    pub raw_host_slots: Vec<i64>,
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
        if let Some(selection) = select_latency_mapped_frames(
            &data.trace_clock_observations,
            origin,
            expected.expected_duration_samples,
            expected.expected_sample_rate,
            expected_len,
            slot_samples,
        ) {
            return Some(SideClockResolution {
                frames: selection.frames,
                producer_slots: selection.producer_slots,
                wav_slots: selection.wav_slots,
                comparison_slots: selection.comparison_slots,
                raw_host_slots: selection.raw_host_slots,
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
                comparison_slots: Vec::new(),
                raw_host_slots: Vec::new(),
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

struct LatencyMappedSelection {
    frames: Vec<Frame>,
    producer_slots: Vec<i64>,
    wav_slots: Vec<i64>,
    comparison_slots: Vec<i64>,
    raw_host_slots: Vec<i64>,
}

fn select_latency_mapped_frames(
    observations: &[TraceClockObservation],
    origin: i64,
    duration_samples: u64,
    sample_rate: u32,
    expected_len: usize,
    slot_samples: i64,
) -> Option<LatencyMappedSelection> {
    if expected_len == 0 || slot_samples <= 0 || sample_rate == 0 {
        return None;
    }
    let duration = i64::try_from(duration_samples).ok()?;
    let end = origin.checked_add(duration)?;
    let first_full_window = origin.checked_add(slot_samples)?;
    let mut candidates = observations
        .iter()
        .filter_map(|observation| {
            if !matches!(
                observation.presentation_latency_source.as_deref(),
                Some("vst3" | "audio_unit_v2")
            ) {
                return None;
            }
            let latency = observation.output_presentation_latency_samples?;
            let producer = observation.producer_position_samples?;
            let aligned = producer.checked_add(i64::from(latency))?;
            (aligned >= first_full_window && aligned <= end).then_some((
                aligned,
                producer,
                observation.raw_host_position_samples,
                observation.frame.t_ms,
                observation.capture_epoch.unwrap_or(0),
                &observation.frame,
            ))
        })
        .collect::<Vec<_>>();
    candidates.sort_by_key(|(position, _, _, t_ms, epoch, _)| (*t_ms, *epoch, *position));

    let mut frames = Vec::with_capacity(candidates.len().min(expected_len));
    let mut producer_slots = Vec::with_capacity(frames.capacity());
    let mut wav_slots = Vec::with_capacity(frames.capacity());
    let mut comparison_slots = Vec::with_capacity(frames.capacity());
    let mut raw_host_slots = Vec::with_capacity(frames.capacity());
    let mut raw_host_slots_complete = true;
    let mut previous_position = None;
    let mut previous_comparison_position = None;
    let mut previous_time = None;
    for (position, producer_position, raw_host_position, t_ms, _, source_frame) in candidates {
        let relative = position.checked_sub(origin)?;
        let comparison_relative = producer_position.checked_sub(origin)?;
        // A large positive output latency can make pre-origin producer content presentation-valid
        // inside the WAV. Preserve that measured frame and its negative private comparison key;
        // PRE/POST subtraction later uses only factual positions shared by both sides.
        if comparison_relative > duration {
            return None;
        }
        if previous_position.is_some_and(|previous| position <= previous)
            || previous_comparison_position.is_some_and(|previous| producer_position <= previous)
            || previous_time.is_some_and(|previous| t_ms <= previous)
        {
            return None;
        }
        let mut frame = source_frame.clone();
        frame.t_ms = u64::try_from(relative).ok()?.saturating_mul(1_000) / u64::from(sample_rate);
        frames.push(frame);
        producer_slots.push(position);
        wav_slots.push(relative);
        comparison_slots.push(comparison_relative);
        if let Some(raw_host_position) = raw_host_position {
            raw_host_slots.push(raw_host_position);
        } else {
            raw_host_slots_complete = false;
        }
        previous_position = Some(position);
        previous_comparison_position = Some(producer_position);
        previous_time = Some(t_ms);
    }
    if !raw_host_slots_complete {
        raw_host_slots.clear();
    }
    (!frames.is_empty() && frames.len() <= expected_len).then_some(LatencyMappedSelection {
        frames,
        producer_slots,
        wav_slots,
        comparison_slots,
        raw_host_slots,
    })
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
#[path = "trace_clock_resolution_tests.rs"]
mod tests;
