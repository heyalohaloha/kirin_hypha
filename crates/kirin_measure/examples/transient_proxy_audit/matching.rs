use std::cmp::Ordering;

pub(crate) const TOLERANCE_MICROS: i64 = 25_000;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub(crate) struct MatchRecord {
    pub(crate) prediction_index: usize,
    pub(crate) reference_index: usize,
    pub(crate) signed_error_micros: i64,
}

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
struct Score {
    matches: usize,
    total_absolute_error_micros: u64,
}

/// Strict one-to-one matching with an inclusive ±25 ms boundary.
///
/// The objective is lexicographic: maximum cardinality, minimum total absolute
/// timing error, then earlier reference and earlier prediction.
pub(crate) fn match_events(predictions: &[i64], references: &[i64]) -> Vec<MatchRecord> {
    debug_assert!(sorted(predictions));
    debug_assert!(sorted(references));
    if predictions.is_empty() || references.is_empty() {
        return Vec::new();
    }
    let stride = references.len() + 1;
    let mut scores = vec![Score::default(); (predictions.len() + 1) * stride];
    for prediction in (0..predictions.len()).rev() {
        for reference in (0..references.len()).rev() {
            let skip_prediction = scores[index(prediction + 1, reference, stride)];
            let skip_reference = scores[index(prediction, reference + 1, stride)];
            let mut best = better(skip_prediction, skip_reference);
            let error = predictions[prediction].abs_diff(references[reference]);
            if error <= TOLERANCE_MICROS as u64 {
                let tail = scores[index(prediction + 1, reference + 1, stride)];
                best = better(
                    best,
                    Score {
                        matches: tail.matches + 1,
                        total_absolute_error_micros: tail.total_absolute_error_micros + error,
                    },
                );
            }
            scores[index(prediction, reference, stride)] = best;
        }
    }
    reconstruct(predictions, references, stride, &scores)
}

fn reconstruct(
    predictions: &[i64],
    references: &[i64],
    stride: usize,
    scores: &[Score],
) -> Vec<MatchRecord> {
    let mut records = Vec::with_capacity(scores[0].matches);
    let (mut prediction, mut reference) = (0_usize, 0_usize);
    while prediction < predictions.len() && reference < references.len() {
        let current = scores[index(prediction, reference, stride)];
        let error = predictions[prediction].abs_diff(references[reference]);
        if error <= TOLERANCE_MICROS as u64 {
            let tail = scores[index(prediction + 1, reference + 1, stride)];
            let matched = Score {
                matches: tail.matches + 1,
                total_absolute_error_micros: tail.total_absolute_error_micros + error,
            };
            if matched == current {
                records.push(MatchRecord {
                    prediction_index: prediction,
                    reference_index: reference,
                    signed_error_micros: predictions[prediction] - references[reference],
                });
                prediction += 1;
                reference += 1;
                continue;
            }
        }
        let skip_prediction = scores[index(prediction + 1, reference, stride)];
        let skip_reference = scores[index(prediction, reference + 1, stride)];
        // Keeping the earlier reference has priority when both skips are equal.
        if skip_prediction == current || skip_prediction == skip_reference {
            prediction += 1;
        } else {
            reference += 1;
        }
    }
    records
}

fn better(left: Score, right: Score) -> Score {
    match left.matches.cmp(&right.matches) {
        Ordering::Less => right,
        Ordering::Greater => left,
        Ordering::Equal => {
            if right.total_absolute_error_micros < left.total_absolute_error_micros {
                right
            } else {
                left
            }
        }
    }
}

fn index(prediction: usize, reference: usize, stride: usize) -> usize {
    prediction * stride + reference
}

fn sorted(values: &[i64]) -> bool {
    values.windows(2).all(|pair| pair[0] <= pair[1])
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn cardinality_precedes_distance() {
        let matches = match_events(&[19_000, 44_000], &[0, 20_000]);
        assert_eq!(matches.len(), 2);
        assert_eq!(
            matches
                .iter()
                .map(|record| (record.prediction_index, record.reference_index))
                .collect::<Vec<_>>(),
            [(0, 0), (1, 1)]
        );
    }

    #[test]
    fn inclusive_boundary_and_one_to_one_are_exact() {
        assert_eq!(match_events(&[25_000], &[0]).len(), 1);
        assert!(match_events(&[25_001], &[0]).is_empty());
        assert_eq!(match_events(&[0, 1], &[0]).len(), 1);
        assert_eq!(match_events(&[0], &[0, 1]).len(), 1);
    }

    #[test]
    fn equal_ties_prefer_earlier_reference_then_prediction() {
        assert_eq!(match_events(&[0], &[-10_000, 10_000])[0].reference_index, 0);
        assert_eq!(
            match_events(&[-10_000, 10_000], &[0])[0].prediction_index,
            0
        );
    }
}
