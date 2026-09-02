use std::cmp::Ordering;

use super::input::LabelEvent;

pub(crate) const MATCH_TOLERANCE_MICROS: u64 = 25_000;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub(crate) struct MatchRecord {
    pub(crate) prediction_index: usize,
    pub(crate) label_index: usize,
    pub(crate) signed_error_units: i128,
}

impl MatchRecord {
    pub(crate) fn signed_error_seconds(self, sample_rate: u32) -> f64 {
        self.signed_error_units as f64 / (f64::from(sample_rate) * 1_000_000.0)
    }
}

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
struct Score {
    match_count: usize,
    total_abs_error_units: u128,
}

#[derive(Clone, Copy, Debug, Default)]
struct CandidateEdge {
    prediction_index: usize,
    label_index: usize,
    abs_error_units: u128,
    tail_score: Score,
}

/// Matches sorted prediction samples to sorted integer-microsecond labels.
///
/// The objective is lexicographic: maximize match cardinality, minimize the
/// exact total absolute timing error, then prefer earlier label and prediction
/// indices. Memory is O(labels + admissible edges), never O(predictions*labels).
pub(crate) fn match_events(
    prediction_samples: &[i64],
    sample_rate: u32,
    labels: &[LabelEvent],
) -> Result<Vec<MatchRecord>, String> {
    let mut label_micros = try_vec_with_capacity(labels.len(), "label times")?;
    label_micros.extend(labels.iter().map(|label| label.time_micros));
    match_times(prediction_samples, sample_rate, &label_micros)
}

pub(crate) fn is_within_tolerance(
    prediction_sample: i64,
    sample_rate: u32,
    label_micros: u64,
) -> bool {
    sample_rate != 0
        && signed_error_units(prediction_sample, sample_rate, label_micros).unsigned_abs()
            <= tolerance_units(sample_rate) as u128
}

fn match_times(
    prediction_samples: &[i64],
    sample_rate: u32,
    label_micros: &[u64],
) -> Result<Vec<MatchRecord>, String> {
    validate_inputs(prediction_samples, sample_rate, label_micros)?;
    if prediction_samples.is_empty() || label_micros.is_empty() {
        return Ok(Vec::new());
    }

    let mut candidates = candidate_edges(prediction_samples, sample_rate, label_micros)?;
    if candidates.is_empty() {
        return Ok(Vec::new());
    }
    let edge_indices = indices_by_prediction(&candidates)?;
    let label_slots = label_micros
        .len()
        .checked_add(1)
        .ok_or("matching label row length overflow")?;
    let mut next = try_default_scores(label_slots, "next matching row")?;
    let mut current = try_default_scores(label_slots, "current matching row")?;
    let global_score = populate_tail_scores(
        prediction_samples.len(),
        label_micros.len(),
        &edge_indices,
        &mut candidates,
        &mut next,
        &mut current,
    )?;
    reconstruct_matches(
        prediction_samples,
        sample_rate,
        label_micros,
        &candidates,
        global_score,
    )
}

fn populate_tail_scores(
    prediction_count: usize,
    label_count: usize,
    edge_indices: &[usize],
    candidates: &mut [CandidateEdge],
    next: &mut Vec<Score>,
    current: &mut Vec<Score>,
) -> Result<Score, String> {
    let mut group_end = edge_indices.len();
    for prediction_index in (0..prediction_count).rev() {
        let mut group_start = group_end;
        while group_start > 0
            && candidates[edge_indices[group_start - 1]].prediction_index == prediction_index
        {
            group_start -= 1;
        }
        if group_start == group_end {
            // With no admissible edge, this prediction can only be skipped,
            // so Score(i, j) is exactly the already-retained Score(i + 1, j).
            continue;
        }

        current[label_count] = Score::default();
        let mut edge_position = group_end;
        for label_index in (0..label_count).rev() {
            let edge_index = if edge_position > group_start
                && candidates[edge_indices[edge_position - 1]].label_index == label_index
            {
                edge_position -= 1;
                Some(edge_indices[edge_position])
            } else {
                None
            };
            let mut best = better_score(next[label_index], current[label_index + 1]);
            if let Some(edge_index) = edge_index {
                let tail = next[label_index + 1];
                candidates[edge_index].tail_score = tail;
                best = better_score(
                    best,
                    extend_score(tail, candidates[edge_index].abs_error_units)?,
                );
            }
            current[label_index] = best;
        }
        if edge_position != group_start {
            return Err("matching edge index order invariant failed".to_string());
        }
        std::mem::swap(next, current);
        group_end = group_start;
    }
    if group_end != 0 {
        return Err("matching prediction edge coverage invariant failed".to_string());
    }
    Ok(next[0])
}

fn reconstruct_matches(
    prediction_samples: &[i64],
    sample_rate: u32,
    label_micros: &[u64],
    candidates: &[CandidateEdge],
    global_score: Score,
) -> Result<Vec<MatchRecord>, String> {
    let mut matches = try_vec_with_capacity(global_score.match_count, "matching result")?;
    let mut remaining = global_score;
    let mut prediction_start = 0;
    let mut label_start = 0;
    let mut candidate_start = 0;

    while matches.len() < global_score.match_count {
        let selected = candidates[candidate_start..]
            .iter()
            .position(|candidate| {
                candidate.prediction_index >= prediction_start
                    && candidate.label_index >= label_start
                    && extend_score(candidate.tail_score, candidate.abs_error_units)
                        .is_ok_and(|score| score == remaining)
            })
            .map(|offset| candidate_start + offset)
            .ok_or("optimal matching edge reconstruction failed")?;
        let candidate = candidates[selected];
        matches.push(MatchRecord {
            prediction_index: candidate.prediction_index,
            label_index: candidate.label_index,
            signed_error_units: signed_error_units(
                prediction_samples[candidate.prediction_index],
                sample_rate,
                label_micros[candidate.label_index],
            ),
        });
        remaining = candidate.tail_score;
        prediction_start = candidate
            .prediction_index
            .checked_add(1)
            .ok_or("matching prediction index overflow")?;
        label_start = candidate
            .label_index
            .checked_add(1)
            .ok_or("matching label index overflow")?;
        candidate_start = selected
            .checked_add(1)
            .ok_or("matching candidate index overflow")?;
        while candidate_start < candidates.len()
            && candidates[candidate_start].label_index < label_start
        {
            candidate_start += 1;
        }
    }
    Ok(matches)
}

fn candidate_edges(
    prediction_samples: &[i64],
    sample_rate: u32,
    label_micros: &[u64],
) -> Result<Vec<CandidateEdge>, String> {
    let mut candidates = Vec::new();
    let mut first_prediction = 0;
    let tolerance = tolerance_units(sample_rate);
    for (label_index, &label_time) in label_micros.iter().enumerate() {
        while first_prediction < prediction_samples.len()
            && signed_error_units(
                prediction_samples[first_prediction],
                sample_rate,
                label_time,
            ) < -tolerance
        {
            first_prediction += 1;
        }
        let mut prediction_index = first_prediction;
        while prediction_index < prediction_samples.len() {
            let error = signed_error_units(
                prediction_samples[prediction_index],
                sample_rate,
                label_time,
            );
            if error > tolerance {
                break;
            }
            try_push(
                &mut candidates,
                CandidateEdge {
                    prediction_index,
                    label_index,
                    abs_error_units: error.unsigned_abs(),
                    tail_score: Score::default(),
                },
                "candidate edges",
            )?;
            prediction_index += 1;
        }
    }
    Ok(candidates)
}

fn indices_by_prediction(candidates: &[CandidateEdge]) -> Result<Vec<usize>, String> {
    let mut indices = try_vec_with_capacity(candidates.len(), "prediction edge index")?;
    indices.extend(0..candidates.len());
    indices.sort_unstable_by_key(|index| {
        let edge = candidates[*index];
        (edge.prediction_index, edge.label_index)
    });
    Ok(indices)
}

fn extend_score(tail: Score, abs_error_units: u128) -> Result<Score, String> {
    Ok(Score {
        match_count: tail
            .match_count
            .checked_add(1)
            .ok_or("matching cardinality overflow")?,
        total_abs_error_units: tail
            .total_abs_error_units
            .checked_add(abs_error_units)
            .ok_or("matching total error overflow")?,
    })
}

fn better_score(left: Score, right: Score) -> Score {
    match left.match_count.cmp(&right.match_count) {
        Ordering::Less => right,
        Ordering::Greater => left,
        Ordering::Equal if right.total_abs_error_units < left.total_abs_error_units => right,
        Ordering::Equal => left,
    }
}

fn validate_inputs(
    prediction_samples: &[i64],
    sample_rate: u32,
    label_micros: &[u64],
) -> Result<(), String> {
    if sample_rate == 0 {
        return Err("matching sample rate must be nonzero".to_string());
    }
    if !prediction_samples.windows(2).all(|pair| pair[0] <= pair[1]) {
        return Err("matching predictions must be sorted".to_string());
    }
    if !label_micros.windows(2).all(|pair| pair[0] <= pair[1]) {
        return Err("matching labels must be sorted".to_string());
    }
    Ok(())
}

fn signed_error_units(prediction_sample: i64, sample_rate: u32, label_micros: u64) -> i128 {
    i128::from(prediction_sample) * 1_000_000 - i128::from(label_micros) * i128::from(sample_rate)
}

fn tolerance_units(sample_rate: u32) -> i128 {
    i128::from(MATCH_TOLERANCE_MICROS) * i128::from(sample_rate)
}

fn try_default_scores(length: usize, name: &str) -> Result<Vec<Score>, String> {
    let mut values = try_vec_with_capacity(length, name)?;
    values.resize(length, Score::default());
    Ok(values)
}

fn try_vec_with_capacity<T>(capacity: usize, name: &str) -> Result<Vec<T>, String> {
    let mut values = Vec::new();
    values
        .try_reserve_exact(capacity)
        .map_err(|_| format!("cannot allocate {name}"))?;
    Ok(values)
}

fn try_push<T>(values: &mut Vec<T>, value: T, name: &str) -> Result<(), String> {
    if values.len() == values.capacity() {
        values
            .try_reserve(1)
            .map_err(|_| format!("cannot allocate {name}"))?;
    }
    values.push(value);
    Ok(())
}

#[cfg(test)]
#[path = "matching_tests.rs"]
mod tests;
