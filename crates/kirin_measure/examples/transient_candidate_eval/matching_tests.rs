use super::*;

#[test]
fn maximizes_cardinality_before_distance() {
    let actual = match_times(&[19, 44], 1_000, &[0, 20_000]).unwrap();

    assert_eq!(actual.len(), 2);
    assert_eq!((actual[0].prediction_index, actual[0].label_index), (0, 0));
    assert_eq!((actual[1].prediction_index, actual[1].label_index), (1, 1));
    assert!((actual[0].signed_error_seconds(1_000) - 0.019).abs() < 1.0e-12);
    assert!((actual[1].signed_error_seconds(1_000) - 0.024).abs() < 1.0e-12);
}

#[test]
fn forty_four_one_boundary_is_exactly_inclusive() {
    let actual = match_times(&[1_323], 44_100, &[5_000]).unwrap();

    assert_eq!(
        actual,
        vec![MatchRecord {
            prediction_index: 0,
            label_index: 0,
            signed_error_units: 25_000_i128 * 44_100,
        }]
    );
    assert!(match_times(&[1_323], 44_100, &[4_999]).unwrap().is_empty());
}

#[test]
fn distance_precedes_earlier_index_tie_breaks() {
    let closest = match_times(&[9, 11], 1_000, &[11_000]).unwrap();
    assert_eq!(closest[0].prediction_index, 1);

    let earlier_label = match_times(&[10], 1_000, &[0, 20_000]).unwrap();
    assert_eq!(earlier_label[0].label_index, 0);

    let earlier_prediction = match_times(&[0, 20], 1_000, &[10_000]).unwrap();
    assert_eq!(earlier_prediction[0].prediction_index, 0);
}

#[test]
fn duplicate_times_match_one_to_one_and_empty_inputs_stay_empty() {
    let actual = match_times(&[1_000, 1_000], 1_000, &[1_000_000, 1_000_000]).unwrap();
    assert_eq!(
        actual,
        vec![
            MatchRecord {
                prediction_index: 0,
                label_index: 0,
                signed_error_units: 0,
            },
            MatchRecord {
                prediction_index: 1,
                label_index: 1,
                signed_error_units: 0,
            },
        ]
    );
    assert!(match_times(&[], 44_100, &[0]).unwrap().is_empty());
    assert!(match_times(&[0], 44_100, &[]).unwrap().is_empty());
}

#[test]
fn invalid_rate_and_unsorted_inputs_fail_closed() {
    assert!(match_times(&[0], 0, &[0]).is_err());
    assert!(match_times(&[1, 0], 44_100, &[0]).is_err());
    assert!(match_times(&[0], 44_100, &[1, 0]).is_err());
}

#[test]
fn rolling_dp_matches_the_old_dense_oracle_exhaustively_on_small_inputs() {
    let predictions = nondecreasing_sequences(&[-26_i64, 0, 24, 25, 26, 50], 3);
    let labels = nondecreasing_sequences(&[0_u64, 1_000, 25_000, 26_000, 50_000], 3);
    assert_eq!((predictions.len(), labels.len()), (84, 56));
    for prediction in &predictions {
        for label in &labels {
            let rolling = match_times(prediction, 1_000, label).unwrap();
            let dense = dense_match_times(prediction, 1_000, label);
            assert_eq!(
                rolling, dense,
                "predictions={prediction:?} labels={label:?}"
            );
        }
    }
}

#[test]
fn twenty_thousand_by_five_thousand_does_not_allocate_the_dense_matrix() {
    let mut predictions = (-1_000_000_i64..-980_001).collect::<Vec<_>>();
    predictions.push(0);
    let mut labels = Vec::with_capacity(5_000);
    labels.push(0_u64);
    labels.extend((1_u64..5_000).map(|index| 1_000_000 + index * 1_000));

    let dense_slots = 20_001_usize * 5_001;
    let dense_bytes = dense_slots * std::mem::size_of::<Score>();
    assert!(dense_bytes > 3_000_000_000);
    let actual = match_times(&predictions, 1_000, &labels).unwrap();
    assert_eq!(actual.len(), 1);
    assert_eq!(actual[0].prediction_index, 19_999);
    assert_eq!(actual[0].label_index, 0);
}

fn dense_match_times(
    prediction_samples: &[i64],
    sample_rate: u32,
    label_micros: &[u64],
) -> Vec<MatchRecord> {
    validate_inputs(prediction_samples, sample_rate, label_micros).unwrap();
    if prediction_samples.is_empty() || label_micros.is_empty() {
        return Vec::new();
    }
    let stride = label_micros.len() + 1;
    let mut scores = vec![Score::default(); (prediction_samples.len() + 1) * stride];
    for prediction_index in (0..prediction_samples.len()).rev() {
        for label_index in (0..label_micros.len()).rev() {
            let mut best = better_score(
                scores[dense_index(prediction_index + 1, label_index, stride)],
                scores[dense_index(prediction_index, label_index + 1, stride)],
            );
            let error = signed_error_units(
                prediction_samples[prediction_index],
                sample_rate,
                label_micros[label_index],
            );
            if error.unsigned_abs() <= tolerance_units(sample_rate) as u128 {
                let tail = scores[dense_index(prediction_index + 1, label_index + 1, stride)];
                best = better_score(best, extend_score(tail, error.unsigned_abs()).unwrap());
            }
            scores[dense_index(prediction_index, label_index, stride)] = best;
        }
    }

    let mut candidates = candidate_edges(prediction_samples, sample_rate, label_micros).unwrap();
    for candidate in &mut candidates {
        candidate.tail_score = scores[dense_index(
            candidate.prediction_index + 1,
            candidate.label_index + 1,
            stride,
        )];
    }
    reconstruct_matches(
        prediction_samples,
        sample_rate,
        label_micros,
        &candidates,
        scores[0],
    )
    .unwrap()
}

fn dense_index(prediction_index: usize, label_index: usize, stride: usize) -> usize {
    prediction_index * stride + label_index
}

fn nondecreasing_sequences<T: Copy>(values: &[T], max_len: usize) -> Vec<Vec<T>> {
    fn extend<T: Copy>(
        values: &[T],
        start: usize,
        remaining: usize,
        current: &mut Vec<T>,
        output: &mut Vec<Vec<T>>,
    ) {
        output.push(current.clone());
        if remaining == 0 {
            return;
        }
        for index in start..values.len() {
            current.push(values[index]);
            extend(values, index, remaining - 1, current, output);
            current.pop();
        }
    }

    let mut output = Vec::new();
    extend(values, 0, max_len, &mut Vec::new(), &mut output);
    output
}
