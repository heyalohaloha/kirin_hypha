use serde::Serialize;

use crate::contract::sha256_bytes;
use crate::drum_excerpt::{
    EXCERPT_CAP_SAMPLES, EXCERPT_SAMPLE_RATE, EXCERPT_START_QUANTUM_SAMPLES,
};
use crate::metadata::Performance;

#[derive(Serialize)]
pub(crate) struct SamplingAudit {
    inference_unit: &'static str,
    claim: &'static str,
    short_full_interval_ids: usize,
    long_hash_window_ids: usize,
    normalized_start_quartile_bins_q1_q2_q3_q4: [usize; 4],
    mean_normalized_start: Option<f64>,
    median_normalized_start: Option<f64>,
    uniform_ks_statistic_diagnostic_only: Option<f64>,
    uniform_ks_five_percent_critical_approx: Option<f64>,
    seed_redraw_permitted: bool,
    zero_raw_note_excerpt_ids: usize,
    zero_compound_event_excerpt_ids: usize,
}

#[derive(Serialize)]
pub(crate) struct SourceMidiDiagnostic {
    source_duration_samples_44100: u64,
    source_duration_secs: f64,
    source_raw_notes: usize,
    source_compound_events: usize,
    source_kick_only_events: usize,
    source_hat_only_events: usize,
}

pub(crate) fn duration_sample_binding(selected: &[Performance]) -> String {
    let mut value = Vec::new();
    for (index, item) in selected.iter().enumerate() {
        push_field(&mut value, &(index + 1).to_string());
        push_field(&mut value, &item.row.id);
        push_field(&mut value, &item.row.duration_decimal);
        value.extend_from_slice(&item.row.duration_samples_44100.to_be_bytes());
    }
    sha256_bytes(&value)
}

fn push_field(output: &mut Vec<u8>, value: &str) {
    output.extend_from_slice(&(value.len() as u64).to_be_bytes());
    output.extend_from_slice(value.as_bytes());
}

pub(crate) fn sampling_audit(selected: &[Performance]) -> SamplingAudit {
    let mut starts = Vec::new();
    for item in selected {
        if item.row.duration_samples_44100 > EXCERPT_CAP_SAMPLES {
            starts.push(normalized_start(
                item.row.duration_samples_44100,
                item.excerpt_start_sample_44100(),
            ));
        }
    }
    starts.sort_by(f64::total_cmp);
    let mut quartiles = [0; 4];
    for value in &starts {
        let bin = if *value < 0.25 {
            0
        } else if *value < 0.5 {
            1
        } else if *value < 0.75 {
            2
        } else {
            3
        };
        quartiles[bin] += 1;
    }
    let count = starts.len();
    SamplingAudit {
        inference_unit: "performance_id",
        claim: "candidate_independent_hash_window_not_uniform_dataset_time",
        short_full_interval_ids: selected.len() - count,
        long_hash_window_ids: count,
        normalized_start_quartile_bins_q1_q2_q3_q4: quartiles,
        mean_normalized_start: (!starts.is_empty())
            .then(|| starts.iter().sum::<f64>() / count as f64),
        median_normalized_start: median(&starts),
        uniform_ks_statistic_diagnostic_only: ks_uniform(&starts),
        uniform_ks_five_percent_critical_approx: (count > 0).then(|| 1.36 / (count as f64).sqrt()),
        seed_redraw_permitted: false,
        zero_raw_note_excerpt_ids: selected
            .iter()
            .filter(|item| item.midi.raw_notes == 0)
            .count(),
        zero_compound_event_excerpt_ids: selected
            .iter()
            .filter(|item| item.midi.compound_events == 0)
            .count(),
    }
}

fn normalized_start(source_samples: u64, start_sample: u64) -> f64 {
    let maximum_start = ((source_samples - EXCERPT_CAP_SAMPLES) / EXCERPT_START_QUANTUM_SAMPLES)
        * EXCERPT_START_QUANTUM_SAMPLES;
    if maximum_start == 0 {
        0.0
    } else {
        start_sample as f64 / maximum_start as f64
    }
}

fn median(values: &[f64]) -> Option<f64> {
    let middle = values.len() / 2;
    match values.len() {
        0 => None,
        length if length % 2 == 0 => Some((values[middle - 1] + values[middle]) / 2.0),
        _ => Some(values[middle]),
    }
}

fn ks_uniform(values: &[f64]) -> Option<f64> {
    let count = values.len();
    (count > 0).then(|| {
        values
            .iter()
            .enumerate()
            .map(|(index, value)| {
                let upper = (index + 1) as f64 / count as f64 - value;
                let lower = value - index as f64 / count as f64;
                upper.max(lower)
            })
            .fold(0.0, f64::max)
    })
}

pub(crate) fn source_diagnostic(selected: &[Performance]) -> SourceMidiDiagnostic {
    let samples = selected
        .iter()
        .map(|item| item.row.duration_samples_44100)
        .sum::<u64>();
    SourceMidiDiagnostic {
        source_duration_samples_44100: samples,
        source_duration_secs: samples as f64 / f64::from(EXCERPT_SAMPLE_RATE),
        source_raw_notes: selected.iter().map(|item| item.midi.source_raw_notes).sum(),
        source_compound_events: selected
            .iter()
            .map(|item| item.midi.source_compound_events)
            .sum(),
        source_kick_only_events: selected
            .iter()
            .map(|item| item.midi.source_kick_only_events)
            .sum(),
        source_hat_only_events: selected
            .iter()
            .map(|item| item.midi.source_hat_only_events)
            .sum(),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn one_position_long_windows_normalize_to_zero_without_nan() {
        for extra in [1, EXCERPT_START_QUANTUM_SAMPLES - 1] {
            assert_eq!(normalized_start(EXCERPT_CAP_SAMPLES + extra, 0), 0.0);
        }
    }
}
