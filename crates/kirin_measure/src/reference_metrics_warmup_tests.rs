//! Compare finite/missing values independently for each metric. Never infer convergence.
use super::*;
use std::time::Instant;
const TOLERANCE: f64 = 0.0001; // dB for CREST/PSR, acum for SHARP
fn input(rate: u32, channels: usize, start: u64, variant: usize) -> Vec<f32> {
    (0..rate as usize / 10)
        .flat_map(|i| {
            let at = start + i as u64;
            let t = at as f64 / f64::from(rate);
            let tone = ((t * 997.0 * std::f64::consts::TAU).sin() * 0.21
                + (t * 83.0 * std::f64::consts::TAU).sin() * 0.04
                + (t * 7111.0 * std::f64::consts::TAU).sin() * 0.08) as f32;
            let x = if variant == 0 {
                tone * if t < 11.0 { 0.03 } else { 1.0 }
            } else if (8.0..11.8).contains(&t) || (13.2..13.7).contains(&t) {
                0.0
            } else if at.is_multiple_of(u64::from(rate)) {
                0.9
            } else {
                tone
            };
            (0..channels).map(move |ch| if ch == 0 { x } else { x * -0.63 })
        })
        .collect()
}
fn run(rate: u32, channels: usize, warm: u64, variant: usize) -> Vec<ReferenceMetricFrame> {
    let hop = u64::from(rate / 10);
    let cue_start = u64::from(rate) * 12 + 137; // deliberately not on the aperture grid
    let cue_end = u64::from(rate) * 15 + 231;
    let start = if warm == 0 {
        0
    } else {
        cue_start.saturating_sub(u64::from(rate) * warm) / hop * hop
    };
    let stop = (cue_end / hop + 8) * hop; // real source lookahead, never padding
    let mut engine = ReferenceMetrics::new_at(rate, channels, start).unwrap();
    let mut frames = Vec::new();
    for at in (start..stop).step_by(hop as usize) {
        frames.extend(
            engine
                .push_aperture(&input(rate, channels, at, variant))
                .unwrap(),
        );
    }
    frames.extend(engine.finish().unwrap());
    frames.retain(|x| x.end_sample > cue_start && x.end_sample <= cue_end);
    frames
}
fn values(frame: &ReferenceMetricFrame) -> [Option<f64>; 3] {
    [frame.psr_db, frame.crest_db, frame.sharpness_acum]
}

fn cpu_seconds() -> Option<f64> {
    #[cfg(unix)]
    {
        let mut usage = std::mem::MaybeUninit::<libc::rusage>::uninit();
        if unsafe { libc::getrusage(libc::RUSAGE_SELF, usage.as_mut_ptr()) } != 0 {
            return None;
        }
        let usage = unsafe { usage.assume_init() };
        Some(
            usage.ru_utime.tv_sec as f64
                + usage.ru_stime.tv_sec as f64
                + (usage.ru_utime.tv_usec + usage.ru_stime.tv_usec) as f64 / 1_000_000.0,
        )
    }
    #[cfg(not(unix))]
    {
        None
    }
}
fn timed_run(rate: u32, channels: usize, warm: u64, variant: usize) -> Vec<ReferenceMetricFrame> {
    let wall = Instant::now();
    let cpu = cpu_seconds();
    let frames = run(rate, channels, warm, variant);
    let elapsed = wall.elapsed().as_secs_f64();
    let used = cpu.zip(cpu_seconds()).map(|(a, b)| b - a);
    eprintln!("WARMUP_COST rate={rate} channels={channels} variant={variant} warm={warm} wall_s={elapsed:.6} process_cpu_s={used:?} core_percent={:?}", used.map(|x|100.0*x/elapsed));
    frames
}
#[test]
fn reference_metrics_warmup_is_compared_and_falls_back_per_metric() {
    let mut worst = [[0.0_f64; 3]; 2];
    let mut valid = [[true; 3]; 2];
    for rate in [44_100, 48_000, 96_000] {
        for channels in [1, 2] {
            for variant in [0, 1] {
                let baseline = timed_run(rate, channels, 0, variant);
                assert_eq!(baseline.len(), 30);
                assert!(baseline
                    .iter()
                    .any(|x| values(x).iter().all(|v| v.is_some_and(f64::is_finite))));
                for (which, warm) in [5, 10].into_iter().enumerate() {
                    let now = Instant::now();
                    let actual = timed_run(rate, channels, warm, variant);
                    assert_eq!(baseline.len(), actual.len());
                    for (a, b) in baseline.iter().zip(&actual) {
                        assert_eq!(a.end_sample, b.end_sample);
                        for (metric, (x, y)) in values(a).into_iter().zip(values(b)).enumerate() {
                            match (x, y) {
                                (None, None) => {}
                                (Some(x), Some(y)) if x.is_finite() && y.is_finite() => {
                                    worst[which][metric] = worst[which][metric].max((x - y).abs());
                                }
                                _ => valid[which][metric] = false,
                            }
                        }
                    }
                    eprintln!("WARMUP_FIXTURE rate={rate} channels={channels} variant={variant} warm={warm} elapsed_s={:.6}",now.elapsed().as_secs_f64());
                }
            }
        }
    }
    for (metric, name) in ["PSR", "CREST", "SHARP"].into_iter().enumerate() {
        let selected = if valid[0][metric] && worst[0][metric] <= TOLERANCE {
            5
        } else if valid[1][metric] && worst[1][metric] <= TOLERANCE {
            10
        } else {
            0
        };
        eprintln!("WARMUP_DECISION metric={name} tolerance={TOLERANCE} five_valid={} five_max_abs={} ten_valid={} ten_max_abs={} selected_seconds={selected} (0=continuous-origin)",valid[0][metric],worst[0][metric],valid[1][metric],worst[1][metric]);
        // A failed approximation remains an explicit zero-origin result, never a relaxed gate.
        assert!(
            selected == 0
                || (selected == 5 && valid[0][metric] && worst[0][metric] <= TOLERANCE)
                || (selected == 10 && valid[1][metric] && worst[1][metric] <= TOLERANCE)
        );
    }
}
#[test]
fn reference_metrics_absolute_origin_rejects_unaligned_and_overflow() {
    assert!(ReferenceMetrics::new_at(48000, 2, 1).is_err());
    assert!(ReferenceMetrics::new_at(48000, 2, u64::MAX).is_err());
    let mut at = ReferenceMetrics::new_at(48000, 2, 9600).unwrap();
    let mut zero = ReferenceMetrics::new(48000, 2).unwrap();
    // Exceed the bounded eight-aperture SHARP delay, then include the real EOF tail.
    let mut absolute = Vec::new();
    let mut relative = Vec::new();
    for slot in 0..16 {
        let samples = input(48000, 2, 9600 + slot * 4800, 0);
        let a = at.push_aperture(&samples).unwrap();
        let b = zero.push_aperture(&samples).unwrap();
        assert_eq!(a.len(), b.len(), "origin must not change publication delay");
        absolute.extend(a);
        relative.extend(b);
    }
    assert!(!absolute.is_empty(), "real SHARP publications precede EOF");
    for frames in [&absolute, &relative] {
        assert!(
            frames.iter().any(|frame| {
                values(frame)
                    .iter()
                    .all(|value| value.is_some_and(f64::is_finite))
            }),
            "all three metrics must produce real finite values before finish"
        );
    }
    let absolute_tail = at.finish().unwrap();
    let relative_tail = zero.finish().unwrap();
    assert_eq!(absolute_tail.len(), relative_tail.len());
    absolute.extend(absolute_tail);
    relative.extend(relative_tail);
    assert_eq!(absolute.len(), 16);
    assert_eq!(relative.len(), 16);
    for (index, (a, b)) in absolute.iter().zip(&relative).enumerate() {
        assert_eq!(b.end_sample, (index as u64 + 1) * 4800);
        assert_eq!(a.end_sample, (index as u64 + 1) * 4800 + 9600);
        assert_eq!(values(a), values(b));
    }
}
