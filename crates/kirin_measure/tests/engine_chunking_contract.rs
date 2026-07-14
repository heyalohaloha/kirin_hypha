use kirin_measure::{MeasureEngine, MeasureResult};

fn patterned_chunk(sample_rate: u32, channels: usize, chunk_index: usize) -> Vec<f64> {
    let frames = sample_rate as usize / 10;
    let mut samples = Vec::with_capacity(frames * channels);
    for frame in 0..frames {
        let phase = 2.0 * std::f64::consts::PI * 997.0 * frame as f64 / sample_rate as f64;
        let mono = match chunk_index {
            0 => {
                if frame == 0 {
                    0.95
                } else {
                    0.02
                }
            }
            1 => 0.20,
            2 => 0.43 * phase.sin(),
            3 => 0.08 * (2.0 * phase).sin(),
            _ => 0.03,
        };
        for channel in 0..channels {
            samples.push(if channel == 0 { mono } else { mono * 0.5 });
        }
    }
    samples
}

fn collect_results(engine: &mut MeasureEngine, samples: &[f64]) -> Vec<MeasureResult> {
    let mut results = Vec::new();
    engine.push_observed(samples, |_, result, _| results.push(result.clone()));
    results
}

fn assert_optional_close(label: &str, left: Option<f64>, right: Option<f64>) {
    match (left, right) {
        (Some(left), Some(right)) => assert!(
            (left - right).abs() <= 1.0e-12,
            "{label}: {left} != {right}"
        ),
        (None, None) => {}
        _ => panic!("{label}: presence differs: {left:?} != {right:?}"),
    }
}

#[test]
fn one_large_push_matches_sequential_100ms_pushes_at_standard_rates() {
    for sample_rate in [44_100, 48_000, 96_000, 192_000] {
        for channels in [1, 2] {
            let chunks: Vec<Vec<f64>> = (0..5)
                .map(|index| patterned_chunk(sample_rate, channels, index))
                .collect();
            let all_samples: Vec<f64> = chunks.iter().flatten().copied().collect();

            let mut batched = MeasureEngine::new(sample_rate, channels).unwrap();
            let batched_results = collect_results(&mut batched, &all_samples);

            let mut sequential = MeasureEngine::new(sample_rate, channels).unwrap();
            let sequential_results: Vec<MeasureResult> = chunks
                .iter()
                .flat_map(|chunk| collect_results(&mut sequential, chunk))
                .collect();

            assert_eq!(batched_results.len(), 5);
            assert_eq!(batched_results.len(), sequential_results.len());
            for (index, (batched, sequential)) in
                batched_results.iter().zip(&sequential_results).enumerate()
            {
                let context = format!("{sample_rate}Hz/{channels}ch/chunk {index}");
                assert_optional_close(
                    &format!("{context}/lufs_m"),
                    batched.lufs_m,
                    sequential.lufs_m,
                );
                assert_optional_close(
                    &format!("{context}/true_peak"),
                    batched.true_peak,
                    sequential.true_peak,
                );
                assert_optional_close(
                    &format!("{context}/tp_session_max"),
                    batched.tp_session_max,
                    sequential.tp_session_max,
                );
                assert_optional_close(&format!("{context}/crest"), batched.crest, sequential.crest);
                assert_optional_close(&format!("{context}/psr"), batched.psr, sequential.psr);
            }
        }
    }
}

#[test]
fn crest_pooling_uses_all_interleaved_channel_samples() {
    let sample_rate = 48_000;
    let frames = sample_rate as usize * 4 / 10;
    let stereo: Vec<f64> = (0..frames).flat_map(|_| [1.0, 0.0]).collect();
    let mut engine = MeasureEngine::new(sample_rate, 2).unwrap();

    let result = engine.push(&stereo).expect("four 100ms results");
    let crest = result.crest.expect("non-silent crest");

    assert!(
        (crest - 3.010_299_956_639_812).abs() <= 1.0e-12,
        "interleaved RMS should average both channels, got {crest} dB"
    );
}
