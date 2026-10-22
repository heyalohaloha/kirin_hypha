//! Calibration and policy for explicit release-mode performance probes only.
use rustfft::{num_complex::Complex, FftPlanner};
use std::{hint::black_box, time::Instant};

fn calibrated_reference_ms(mut measure_batch: impl FnMut(usize) -> f64) -> f64 {
    const FFTS_PER_BATCH: usize = 200;
    let mut trials = [0.0; 5];
    for trial in &mut trials {
        *trial = measure_batch(FFTS_PER_BATCH) / FFTS_PER_BATCH as f64;
    }
    trials.sort_by(f64::total_cmp);
    trials[2]
}

/// Median of sustained FFT batches on the same worker immediately before the measured window.
/// Each batch spans roughly a tenth of that window; return milliseconds per 9,600-point FFT so
/// existing worker/reference ratios keep their units. Planning and allocation are not timed.
pub(crate) fn reference_ms() -> f64 {
    let fft = FftPlanner::<f64>::new().plan_fft_forward(9_600);
    let input: Vec<_> = (0..9_600)
        .map(|index| Complex::new((index as f64 * 0.137).sin(), 0.0))
        .collect();
    let mut samples = input.clone();
    let mut scratch = vec![Complex::default(); fft.get_inplace_scratch_len()];
    let reference = calibrated_reference_ms(|count| {
        let start = Instant::now();
        for _ in 0..count {
            samples.copy_from_slice(&input);
            fft.process_with_scratch(black_box(&mut samples), black_box(&mut scratch));
            black_box(&samples);
        }
        start.elapsed().as_secs_f64() * 1_000.0
    });
    eprintln!("reference_ms={reference:.6} per FFT (200 FFTs/batch, median of 5 batches)");
    reference
}

fn within_budget(percent: f64, ratio: f64, limit: f64, absolute: bool) -> bool {
    percent.is_finite()
        && percent >= 0.0
        && ratio.is_finite()
        && ratio >= 0.0
        && limit.is_finite()
        && limit > 0.0
        && if absolute {
            percent < 18.0
        } else {
            ratio <= limit
        }
}

pub(crate) fn report_budget(name: &str, mut percent: [f64; 5], mut ratios: [f64; 5]) {
    let limit = include_str!("../../../scripts/perf_budget.tsv")
        .lines()
        .filter(|line| !line.starts_with('#'))
        .filter_map(|line| line.split_once('\t'))
        .find_map(|(id, limit)| {
            (id == name).then(|| limit.parse::<f64>().expect("numeric ratio budget"))
        })
        .expect("performance probe is registered in perf_budget.tsv");
    percent.sort_by(f64::total_cmp);
    ratios.sort_by(f64::total_cmp);
    let absolute = std::env::var("KIRIN_HYPHA_PERF_ABSOLUTE").as_deref() == Ok("1");
    let policy = if absolute {
        "CPU <18%"
    } else {
        "calibrated ratio"
    };
    eprintln!("{name}: median projected CPU {:.3}%, worst {:.3}%; calibrated ratio {:.3}, CI limit {limit:.3}; policy={policy}", percent[2], percent[4], ratios[2]);
    assert!(
        within_budget(percent[2], ratios[2], limit, absolute),
        "worker performance budget exceeded"
    );
}

#[test]
fn calibrated_and_absolute_policies_have_independent_limits() {
    assert!(within_budget(40.0, 2.0, 3.0, false));
    assert!(!within_budget(40.0, 2.0, 3.0, true));
    assert!(within_budget(17.9, 3.0, 3.0, true));
    assert!(!within_budget(18.0, 2.0, 3.0, true));
    assert!(!within_budget(1.0, 3.1, 3.0, false));
    for invalid in [f64::NAN, f64::INFINITY, -1.0] {
        assert!(!within_budget(invalid, 1.0, 3.0, false));
        assert!(!within_budget(1.0, invalid, 3.0, false));
    }
}

#[test]
fn reference_calibration_uses_sustained_batches_and_rejects_an_interrupted_trial() {
    let mut batches = Vec::new();
    let mut trials = [6.0, 6.0, 60.0, 6.0, 6.0].into_iter();
    let reference = calibrated_reference_ms(|count| {
        batches.push(count);
        trials.next().unwrap_or(6.0)
    });
    assert!(batches.iter().all(|count| *count >= 200));
    assert!((reference - 0.03).abs() < 1e-12);
}

#[test]
fn local_cpu_budget_accepts_measurements_above_ci_ratio_limits() {
    for (cpu, ratio, limit) in [(10.508, 132.257, 86.052), (11.476, 287.234, 181.973)] {
        assert!(within_budget(cpu, ratio, limit, true));
    }
}

#[test]
fn ci_ratio_budget_rejects_measurements_below_local_cpu_ceiling() {
    for (cpu, ratio, limit) in [(10.508, 132.257, 86.052), (11.476, 287.234, 181.973)] {
        assert!(!within_budget(cpu, ratio, limit, false));
    }
}
