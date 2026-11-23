//! Independent BS.1770 oracle and discontinuity contracts for ALL PLAYED.
use kirin_measure::live_match_span::KirinLiveMatchSpan;

const RATE: usize = 48_000;

// BS.1770-5 Annex 1, Tables 1/2, at 48 kHz: frequency-response magnitude of
// both K-weighting stages at 1 kHz. No production meter or filter is the oracle.
fn k_gain_at_1khz() -> f64 {
    let omega = std::f64::consts::TAU / 48.0;
    let norm = |c: [f64; 3]| {
        let (mut re, mut im) = (0.0, 0.0);
        for (k, coefficient) in c.into_iter().enumerate() {
            let angle = omega * k as f64;
            re += coefficient * angle.cos();
            im -= coefficient * angle.sin();
        }
        re.hypot(im)
    };
    norm([
        1.535_124_859_586_97,
        -2.691_696_189_406_38,
        1.198_392_810_852_85,
    ]) / norm([1.0, -1.690_659_293_182_41, 0.732_480_774_215_85])
        * norm([1.0, -2.0, 1.0])
        / norm([1.0, -1.990_047_454_833_98, 0.990_072_250_366_21])
}

fn tone(lufs: f64, frames: usize) -> Vec<f32> {
    let amplitude = (2.0 * 10f64.powf((lufs + 0.691) / 10.0)).sqrt() / k_gain_at_1khz();
    (0..frames)
        .map(|i| (amplitude * (std::f64::consts::TAU * (i % 48) as f64 / 48.0).sin()) as f32)
        .collect()
}

fn energy(lufs: f64) -> f64 {
    10f64.powf((lufs + 0.691) / 10.0)
}

fn loudness(energy: f64) -> f64 {
    -0.691 + 10.0 * energy.log10()
}

fn expected_gate(blocks: &[(f64, usize)], absolute: bool, relative: bool) -> f64 {
    let mean = |entries: &[(f64, usize)]| {
        entries.iter().map(|&(e, n)| e * n as f64).sum::<f64>()
            / entries.iter().map(|&(_, n)| n).sum::<usize>() as f64
    };
    let unrelative: Vec<_> = blocks
        .iter()
        .filter(|&&(l, _)| !absolute || l > -70.0)
        .map(|&(l, n)| (energy(l), n))
        .collect();
    if !relative {
        return loudness(mean(&unrelative));
    }
    let threshold = mean(&unrelative) / 10.0;
    let gated: Vec<_> = unrelative
        .into_iter()
        .filter(|&(e, _)| e > threshold)
        .collect();
    loudness(mean(&gated))
}

#[test]
fn distinct_absolute_and_relative_gate_reject_independent_known_tones() {
    let blocks = [(-20.0, 7), (-35.0, 7), (-90.0, 37)]; // 1 s, 1 s, 4 s; 400/100 ms
    let expected = expected_gate(&blocks, true, true);
    assert!((expected + 20.0).abs() < 1e-8);
    assert!((expected_gate(&blocks, false, true) - expected).abs() > 2.0);
    assert!((expected_gate(&blocks, true, false) - expected).abs() > 2.0);
    let mut meter = KirinLiveMatchSpan::new(RATE as u32, 1).unwrap();
    for (&(input, _), seconds) in blocks.iter().zip([1, 1, 4]) {
        let pre = tone(input, RATE * seconds);
        let post = tone(input - 4.0, RATE * seconds);
        assert!(meter.feed(&pre, &post));
        assert!(meter.cut());
    }
    let facts = meter.measure().unwrap();
    assert!(
        (facts.pre_lufs - expected).abs() <= 0.10,
        "{facts:?}",
        facts = (facts.pre_lufs, facts.post_lufs)
    );
    assert!((facts.post_lufs - (expected - 4.0)).abs() <= 0.10);
}

#[test]
fn cuts_keep_completed_gate_and_peaks_without_completing_partial_gates() {
    let mut meter = KirinLiveMatchSpan::new(RATE as u32, 1).unwrap();
    let body = tone(-28.0, RATE);
    let partial = tone(-8.0, RATE * 39 / 100);
    assert!(meter.feed(&body, &body));
    assert!(meter.cut());
    for _ in 0..6 {
        assert!(meter.feed(&partial, &partial));
        assert!(meter.cut());
    }
    // 3.34 seconds of proven samples but no complete 400ms gate in any of the six loud chunks.
    // Those chunks still contribute to the session's per-segment true-peak maxima.
    let facts = meter.measure().unwrap();
    assert!((facts.pre_lufs + 28.0).abs() <= 0.10);
    assert!((facts.post_lufs + 28.0).abs() <= 0.10);
    let loudest_input = partial.iter().copied().fold(0.0f32, f32::max) as f64;
    assert!(facts.pre_peak >= 20.0 * loudest_input.log10() - 1e-8);
}

#[test]
fn true_peak_does_not_interpolate_across_unrelated_segments() {
    let mut cut = KirinLiveMatchSpan::new(RATE as u32, 1).unwrap();
    let mut joined = KirinLiveMatchSpan::new(RATE as u32, 1).unwrap();
    let mut first = tone(-32.0, RATE * 3);
    first.extend([0.75; 256]);
    let second = [-0.75; 256];
    for meter in [&mut cut, &mut joined] {
        assert!(meter.feed(&first, &first));
    }
    assert!(cut.cut());
    let first_peak = cut.measure().unwrap().pre_peak;
    assert!(cut.feed(&second, &second));
    assert!(cut.cut());
    assert!(joined.feed(&second, &second));
    assert!(joined.cut());
    let cut_peak = cut.measure().unwrap().pre_peak;
    assert!(cut_peak >= first_peak);
    assert!(joined.measure().unwrap().pre_peak > cut_peak + 0.2);
}
