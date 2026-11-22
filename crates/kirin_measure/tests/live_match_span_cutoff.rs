//! A read-only MATCH cutoff must see an intersample peak not yet emitted by the running FIR.
use ebur128::{EbuR128, Mode};
use kirin_measure::live_match_span::{KirinLiveMatchSpan, KirinLiveMatchSpanFacts};

const RATE: usize = 48_000;
const BURST: [f32; 4] = [0.8, 0.8, -0.8, -0.8];

fn stereo_pair() -> (Vec<f32>, Vec<f32>) {
    let mut pre = Vec::with_capacity((RATE * 3 + BURST.len()) * 2);
    let mut post = Vec::with_capacity(pre.capacity());
    for i in 0..RATE * 3 {
        let tone = (std::f64::consts::TAU * (i % 48) as f64 / 48.0).sin() as f32 * 0.05;
        pre.extend([tone, tone * 0.8]);
        post.extend([tone * 0.7, tone * 0.9]);
    }
    // Only the right PRE channel contains the larger intersample peak at the cutoff.
    for x in BURST {
        pre.extend([x * 0.2, x]);
        post.extend([x * 0.25, x * 0.4]);
    }
    (pre, post)
}

fn numbers(facts: KirinLiveMatchSpanFacts) -> [f64; 4] {
    [
        facts.pre_lufs,
        facts.post_lufs,
        facts.pre_peak,
        facts.post_peak,
    ]
}

#[test]
fn cutoff_matches_closed_segment_and_keeps_continuous_input_unchanged() {
    let (pre, post) = stereo_pair();
    let mut ordinary = EbuR128::new(2, RATE as u32, Mode::I | Mode::TRUE_PEAK).unwrap();
    ordinary.add_frames_f32(&pre).unwrap();
    let buffered_peak = ordinary.true_peak(1).unwrap();
    let cutoff_peak = ordinary.true_peak_at_cutoff().unwrap();
    println!(
        "48k stereo cutoff: running {:.4} dBTP, cutoff {:.4} dBTP",
        20.0 * buffered_peak.log10(),
        20.0 * cutoff_peak.log10()
    );
    assert!(
        20.0 * (cutoff_peak / buffered_peak).log10() > 0.25,
        "fixture must put a real intersample maximum past the running FIR: {buffered_peak} -> {cutoff_peak}"
    );
    let mut read = KirinLiveMatchSpan::new(RATE as u32, 2).unwrap();
    let mut untouched = KirinLiveMatchSpan::new(RATE as u32, 2).unwrap();
    let mut closed = KirinLiveMatchSpan::new(RATE as u32, 2).unwrap();
    for meter in [&mut read, &mut untouched, &mut closed] {
        assert!(meter.feed(&pre, &post));
    }
    let at_cutoff = read.measure().unwrap();
    assert!((at_cutoff.pre_peak - 20.0 * cutoff_peak.log10()).abs() < 1.0e-9);
    assert_eq!(numbers(read.measure().unwrap()), numbers(at_cutoff));
    assert!(closed.cut());
    assert_eq!(
        numbers(closed.measure().unwrap()),
        numbers(read.measure().unwrap())
    );
    let mut next = [0.0; 96];
    for (i, frame) in next.as_chunks_mut::<2>().0.iter_mut().enumerate() {
        frame[0] = 0.01;
        frame[1] = if i % 8 < 4 { 0.93 } else { -0.93 };
    }
    for meter in [&mut read, &mut untouched] {
        assert!(meter.feed(&next, &next));
    }
    let after = read.measure().unwrap();
    assert!(after.pre_peak > 20.0 * cutoff_peak.log10());
    assert_eq!(numbers(after), numbers(untouched.measure().unwrap()));
}

#[test]
fn no_interpolator_cutoff_still_uses_full_channel_sample_peak() {
    let mut meter = EbuR128::new(2, 192_000, Mode::I | Mode::TRUE_PEAK).unwrap();
    meter.add_frames_f32(&[0.12, 0.8, -0.4, -0.23]).unwrap();
    assert_eq!(
        meter.true_peak_at_cutoff().unwrap(),
        meter.true_peak(1).unwrap()
    );
    assert!((meter.true_peak_at_cutoff().unwrap() - 0.8).abs() < 1.0e-6);
}
