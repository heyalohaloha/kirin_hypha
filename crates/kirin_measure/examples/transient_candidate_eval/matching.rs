use std::cmp::Ordering;

use super::input::LabelEvent;

pub(crate) const MATCH_TOLERANCE_SECS: f64 = 0.025;

#[derive(Clone, Copy, Debug, PartialEq)]
pub(crate) struct MatchRecord {
    pub(crate) prediction_index: usize,
    pub(crate) label_index: usize,
    pub(crate) signed_error_secs: f64,
}

#[derive(Clone, Copy, Debug, Default)]
struct Score {
    match_count: usize,
    total_abs_error_secs: f64,
}

#[derive(Clone, Copy, Debug)]
struct CandidateEdge {
    prediction_index: usize,
    label_index: usize,
    abs_error_secs: f64,
}

/// Matches sorted predictions to sorted labels without reusing either event.
///
/// The objective is lexicographic: maximize match cardinality, minimize the
/// total absolute timing error, then prefer earlier label and prediction
/// indices. The returned records are ordered by both label and prediction.
pub(crate) fn match_events(predictions: &[f64], labels: &[LabelEvent]) -> Vec<MatchRecord> {
    let label_times = labels
        .iter()
        .map(|label| label.time_secs)
        .collect::<Vec<_>>();
    match_times(predictions, &label_times)
}

fn match_times(predictions: &[f64], label_times: &[f64]) -> Vec<MatchRecord> {
    debug_assert!(is_sorted_and_finite(predictions));
    debug_assert!(is_sorted_and_finite(label_times));

    if predictions.is_empty() || label_times.is_empty() {
        return Vec::new();
    }

    let label_stride = label_times.len() + 1;
    let mut scores = vec![Score::default(); (predictions.len() + 1) * label_stride];
    for prediction_index in (0..predictions.len()).rev() {
        for label_index in (0..label_times.len()).rev() {
            let mut best = better_score(
                scores[score_index(prediction_index + 1, label_index, label_stride)],
                scores[score_index(prediction_index, label_index + 1, label_stride)],
            );
            let abs_error_secs = (predictions[prediction_index] - label_times[label_index]).abs();
            if abs_error_secs <= MATCH_TOLERANCE_SECS {
                let tail = scores[score_index(prediction_index + 1, label_index + 1, label_stride)];
                let matched = Score {
                    match_count: tail.match_count + 1,
                    total_abs_error_secs: tail.total_abs_error_secs + abs_error_secs,
                };
                best = better_score(best, matched);
            }
            scores[score_index(prediction_index, label_index, label_stride)] = best;
        }
    }

    reconstruct_matches(predictions, label_times, label_stride, &scores)
}

fn reconstruct_matches(
    predictions: &[f64],
    label_times: &[f64],
    label_stride: usize,
    scores: &[Score],
) -> Vec<MatchRecord> {
    let candidates = candidate_edges(predictions, label_times);
    let mut matches = Vec::with_capacity(scores[0].match_count);
    let mut prediction_start = 0;
    let mut label_start = 0;
    let mut candidate_start = 0;

    while matches.len() < scores[0].match_count {
        let remaining = scores[score_index(prediction_start, label_start, label_stride)];
        let selected = candidates[candidate_start..]
            .iter()
            .position(|candidate| {
                if candidate.prediction_index < prediction_start
                    || candidate.label_index < label_start
                {
                    return false;
                }
                let tail = scores[score_index(
                    candidate.prediction_index + 1,
                    candidate.label_index + 1,
                    label_stride,
                )];
                tail.match_count + 1 == remaining.match_count
                    && (tail.total_abs_error_secs + candidate.abs_error_secs)
                        .total_cmp(&remaining.total_abs_error_secs)
                        == Ordering::Equal
            })
            .map(|offset| candidate_start + offset)
            .expect("an optimal matching edge must exist");
        let candidate = candidates[selected];
        matches.push(MatchRecord {
            prediction_index: candidate.prediction_index,
            label_index: candidate.label_index,
            signed_error_secs: predictions[candidate.prediction_index]
                - label_times[candidate.label_index],
        });
        prediction_start = candidate.prediction_index + 1;
        label_start = candidate.label_index + 1;
        candidate_start = selected + 1;
        while candidate_start < candidates.len()
            && candidates[candidate_start].label_index < label_start
        {
            candidate_start += 1;
        }
    }

    matches
}

fn candidate_edges(predictions: &[f64], label_times: &[f64]) -> Vec<CandidateEdge> {
    let mut candidates = Vec::new();
    let mut first_prediction = 0;
    for (label_index, &label_time) in label_times.iter().enumerate() {
        while first_prediction < predictions.len()
            && predictions[first_prediction] < label_time - MATCH_TOLERANCE_SECS
        {
            first_prediction += 1;
        }
        let mut prediction_index = first_prediction;
        while prediction_index < predictions.len()
            && predictions[prediction_index] <= label_time + MATCH_TOLERANCE_SECS
        {
            let abs_error_secs = (predictions[prediction_index] - label_time).abs();
            if abs_error_secs <= MATCH_TOLERANCE_SECS {
                candidates.push(CandidateEdge {
                    prediction_index,
                    label_index,
                    abs_error_secs,
                });
            }
            prediction_index += 1;
        }
    }
    candidates
}

fn better_score(left: Score, right: Score) -> Score {
    match left.match_count.cmp(&right.match_count) {
        Ordering::Less => right,
        Ordering::Greater => left,
        Ordering::Equal => {
            if right
                .total_abs_error_secs
                .total_cmp(&left.total_abs_error_secs)
                == Ordering::Less
            {
                right
            } else {
                left
            }
        }
    }
}

fn score_index(prediction_index: usize, label_index: usize, label_stride: usize) -> usize {
    prediction_index * label_stride + label_index
}

fn is_sorted_and_finite(values: &[f64]) -> bool {
    values.iter().all(|value| value.is_finite()) && values.windows(2).all(|pair| pair[0] <= pair[1])
}

#[cfg(test)]
mod tests {
    use super::{match_times, MatchRecord};

    #[test]
    fn maximizes_cardinality_before_distance() {
        let actual = match_times(&[0.019, 0.044], &[0.0, 0.020]);

        assert_eq!(actual.len(), 2);
        assert_eq!((actual[0].prediction_index, actual[0].label_index), (0, 0));
        assert_eq!((actual[1].prediction_index, actual[1].label_index), (1, 1));
        assert!((actual[0].signed_error_secs - 0.019).abs() < 1.0e-12);
        assert!((actual[1].signed_error_secs - 0.024).abs() < 1.0e-12);
    }

    #[test]
    fn accepts_exact_twenty_five_millisecond_boundary() {
        let actual = match_times(&[0.025], &[0.0]);

        assert_eq!(
            actual,
            vec![MatchRecord {
                prediction_index: 0,
                label_index: 0,
                signed_error_secs: 0.025,
            }]
        );
    }

    #[test]
    fn rejects_event_beyond_twenty_five_milliseconds() {
        assert!(match_times(&[0.025_000_001], &[0.0]).is_empty());
    }

    #[test]
    fn matches_duplicate_times_one_to_one() {
        let actual = match_times(&[1.0, 1.0], &[1.0, 1.0]);

        assert_eq!(
            actual,
            vec![
                MatchRecord {
                    prediction_index: 0,
                    label_index: 0,
                    signed_error_secs: 0.0,
                },
                MatchRecord {
                    prediction_index: 1,
                    label_index: 1,
                    signed_error_secs: 0.0,
                },
            ]
        );
    }

    #[test]
    fn empty_inputs_have_no_matches() {
        assert!(match_times(&[], &[0.0]).is_empty());
        assert!(match_times(&[0.0], &[]).is_empty());
        assert!(match_times(&[], &[]).is_empty());
    }

    #[test]
    fn equal_cost_ties_prefer_earlier_label_then_prediction() {
        let earlier_label = match_times(&[0.0], &[-0.010, 0.010]);
        assert_eq!(earlier_label[0].label_index, 0);

        let earlier_prediction = match_times(&[-0.010, 0.010], &[0.0]);
        assert_eq!(earlier_prediction[0].prediction_index, 0);
    }
}
