use kirin_measure::phase_d::stream::{PhaseDResult, PhaseDStream};
use kirin_measure::phase_d::tables::FieldType;
use serde::Deserialize;

const FIXTURE: &str = include_str!("fixtures/mosqito_phase_d_1_2_1.json");
const SAMPLE_RATE: usize = 48_000;

#[derive(Deserialize)]
struct Fixture {
    schema: u32,
    reference: Reference,
    tolerance: Tolerance,
    signals: Vec<SignalReference>,
}

#[derive(Deserialize)]
struct Reference {
    library: String,
    version: String,
    sample_rate_hz: usize,
    output_period_ms: f64,
}

#[derive(Deserialize)]
struct Tolerance {
    loudness_relative: f64,
    loudness_absolute_sone: f64,
    sharpness_absolute_acum: f64,
}

#[derive(Deserialize)]
struct SignalReference {
    id: String,
    sample_count: usize,
    frames: usize,
    statistics_from_frame: usize,
    n_mean: f64,
    n_max: f64,
    s_mean: f64,
    s_max: f64,
    checkpoints: Vec<Checkpoint>,
}

#[derive(Deserialize)]
struct Checkpoint {
    frame: usize,
    n: f64,
    s: f64,
}

fn tone(sample_count: usize) -> Vec<f64> {
    let peak = 2.0e-5 * 10.0_f64.powf(94.0 / 20.0) * 2.0_f64.sqrt();
    (0..sample_count)
        .map(|index| {
            peak * (2.0 * std::f64::consts::PI * 1000.0 * index as f64 / SAMPLE_RATE as f64).sin()
        })
        .collect()
}

fn input(reference: &SignalReference) -> Vec<f64> {
    match reference.id.as_str() {
        "1khz_94db" => tone(reference.sample_count),
        "step_1khz_94db" => {
            let source = tone(reference.sample_count);
            let mut samples = vec![0.0; reference.sample_count];
            samples[SAMPLE_RATE / 2..SAMPLE_RATE * 2]
                .copy_from_slice(&source[SAMPLE_RATE / 2..SAMPLE_RATE * 2]);
            samples
        }
        "silence" => vec![1.0e-20; reference.sample_count],
        unknown => panic!("unknown golden signal: {unknown}"),
    }
}

fn assert_loudness(label: &str, actual: f64, expected: f64, tolerance: &Tolerance) {
    let allowed = tolerance
        .loudness_absolute_sone
        .max(expected.abs() * tolerance.loudness_relative);
    assert!(
        (actual - expected).abs() <= allowed,
        "{label}: {actual} sone differs from MoSQITo {expected} by more than {allowed}"
    );
}

fn assert_sharpness(label: &str, actual: f64, expected: f64, tolerance: &Tolerance) {
    assert!(
        (actual - expected).abs() <= tolerance.sharpness_absolute_acum,
        "{label}: {actual} acum differs from MoSQITo {expected} by more than {}",
        tolerance.sharpness_absolute_acum
    );
}

fn mean(values: impl Iterator<Item = f64>) -> f64 {
    let (sum, count) = values.fold((0.0, 0usize), |(sum, count), value| {
        (sum + value, count + 1)
    });
    sum / count as f64
}

#[test]
fn phase_d_matches_mosqito_1_2_1_time_varying_reference() {
    let fixture: Fixture = serde_json::from_str(FIXTURE).unwrap();
    assert_eq!(fixture.schema, 1);
    assert_eq!(fixture.reference.library, "MoSQITo");
    assert_eq!(fixture.reference.version, "1.2.1");
    assert_eq!(fixture.reference.sample_rate_hz, SAMPLE_RATE);
    assert_eq!(fixture.reference.output_period_ms, 2.0);

    for signal in &fixture.signals {
        let mut stream = PhaseDStream::new(FieldType::Free);
        let all_results = stream.push(&input(signal));
        let results: Vec<&PhaseDResult> = all_results.iter().step_by(4).collect();
        assert_eq!(
            results.len(),
            signal.frames,
            "{} reference frame count",
            signal.id
        );

        let statistics = &results[signal.statistics_from_frame..];
        let n_mean = mean(statistics.iter().map(|result| result.loudness));
        let n_max = statistics
            .iter()
            .map(|result| result.loudness)
            .fold(f64::NEG_INFINITY, f64::max);
        let s_mean = mean(statistics.iter().map(|result| result.sharpness));
        let s_max = statistics
            .iter()
            .map(|result| result.sharpness)
            .fold(f64::NEG_INFINITY, f64::max);
        assert_loudness(
            &format!("{}/mean N", signal.id),
            n_mean,
            signal.n_mean,
            &fixture.tolerance,
        );
        assert_loudness(
            &format!("{}/max N", signal.id),
            n_max,
            signal.n_max,
            &fixture.tolerance,
        );
        assert_sharpness(
            &format!("{}/mean S", signal.id),
            s_mean,
            signal.s_mean,
            &fixture.tolerance,
        );
        assert_sharpness(
            &format!("{}/max S", signal.id),
            s_max,
            signal.s_max,
            &fixture.tolerance,
        );

        for checkpoint in &signal.checkpoints {
            let actual = results[checkpoint.frame];
            assert_loudness(
                &format!("{}/frame {}/N", signal.id, checkpoint.frame),
                actual.loudness,
                checkpoint.n,
                &fixture.tolerance,
            );
            assert_sharpness(
                &format!("{}/frame {}/S", signal.id, checkpoint.frame),
                actual.sharpness,
                checkpoint.s,
                &fixture.tolerance,
            );
        }
    }
}
