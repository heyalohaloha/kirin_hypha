use super::*;
fn unit(samples: &[f32], rate: u32, ch: usize) -> CaptureUnit {
    let mut index = CaptureIndex::new(rate, ch, rate).unwrap();
    for block in samples.chunks(512) {
        assert!(index.push(block));
    }
    index.finish().unwrap()
}
fn signal(rate: u32, ch: usize) -> Vec<f32> {
    (0..rate)
        .flat_map(|i| {
            (0..ch).map(move |c| {
                let t = i as f64 / rate as f64;
                (0.12 * (2.0 * std::f64::consts::PI * 173.0 * t).sin()
                    + 0.07 * (2.0 * std::f64::consts::PI * (2397.0 + c as f64 * 7.0) * t).sin())
                    as f32
            })
        })
        .collect()
}
#[test]
fn streaming_digest_canonical_and_tail() {
    let samples = [-0.0_f32, 0.0, 0.25, -0.5];
    let a = unit(&samples, 48000, 2);
    let b = unit(&[0.0, 0.0, 0.25, -0.5], 48000, 2);
    assert_eq!(a.digest, b.digest);
    let mut bytes = b"Hypha Capture Unit 1\0".to_vec();
    bytes.extend(48000_u32.to_le_bytes());
    bytes.extend(2_u32.to_le_bytes());
    for v in [0.0_f32, 0.0, 0.25, -0.5] {
        bytes.extend(v.to_le_bytes());
    }
    bytes.extend(2_u32.to_le_bytes());
    assert_eq!(&a.digest[..], &Sha256::digest(&bytes)[..]);
    assert_ne!(
        a.digest,
        unit(&[0.0, 0.0, 0.25, -0.5, 0.0, 0.0], 48000, 2).digest
    );
    let mut index = CaptureIndex::new(48000, 2, 48000).unwrap();
    assert!(!index.push(&[f32::NAN, 0.0]));
    assert!(index.finish().is_none());
    assert!(CaptureIndex::new(7999, 2, 1).is_none());
    assert!(CaptureIndex::new(48000, 3, 1).is_none());
}
#[test]
fn material_features_distinguish_dither_and_processing() {
    for rate in [44100, 48000, 96000, 192000] {
        for ch in [1, 2] {
            let pcm = signal(rate, ch);
            let a = unit(&pcm, rate, ch);
            assert_eq!(compare(&a, &a, ch), 1);
            for bits in [16, 24] {
                let mut seed = 9_u32;
                let dither: Vec<_> = pcm
                    .iter()
                    .map(|v| {
                        seed = seed.wrapping_mul(1664525).wrapping_add(1013904223);
                        let first = seed as f64 / u32::MAX as f64;
                        seed = seed.wrapping_mul(1664525).wrapping_add(1013904223);
                        v + ((first - seed as f64 / u32::MAX as f64) * 2.0_f64.powi(1 - bits))
                            as f32
                    })
                    .collect();
                let b = unit(&dither, rate, ch);
                assert_ne!(a.digest, b.digest);
                assert_eq!(compare(&a, &b, ch), 0, "dither must not notify");
            }
            let gain: Vec<_> = pcm.iter().map(|v| v * 0.5).collect();
            assert_eq!(compare(&a, &unit(&gain, rate, ch), ch), 2);
            let polarity: Vec<_> = pcm.iter().map(|v| -v).collect();
            assert_eq!(
                compare(&a, &unit(&polarity, rate, ch), ch),
                0,
                "phase-only difference is not a material guarantee"
            );
            for mode in 0..2 {
                let mut changed: Vec<f32> = pcm
                    .iter()
                    .enumerate()
                    .map(|(i, &v)| {
                        if mode == 0 {
                            v + 0.04
                                * (2.0 * std::f64::consts::PI * 75.0 * (i / ch) as f64
                                    / rate as f64)
                                    .sin() as f32
                        } else {
                            (v * 10.0).tanh()
                        }
                    })
                    .collect();
                let energy = |s: &[f32]| s.iter().map(|&v| (v as f64).powi(2)).sum::<f64>();
                let norm = (energy(&pcm) / energy(&changed)).sqrt() as f32;
                for v in &mut changed {
                    *v *= norm;
                }
                assert_eq!(
                    compare(&a, &unit(&changed, rate, ch), ch),
                    2,
                    "RMS-normalized EQ/dynamics must remain detectable"
                );
            }
            let silence = unit(&vec![0.0; pcm.len()], rate, ch);
            assert_eq!(compare(&a, &silence, ch), 2);
        }
    }
}
#[test]
fn index_worker_budget() {
    // Release execution measures the complete extra analysis, including digest and features.
    for rate in [8000, 44100, 48000, 96000, 192000, 384000, 768000] {
        let pcm = signal(rate, 2);
        let mut times = Vec::new();
        for _ in 0..3 {
            let mut index = CaptureIndex::new(rate, 2, rate).unwrap();
            for block in pcm.chunks((rate / 10) as usize * 2) {
                let start = std::time::Instant::now();
                assert!(index.push(block));
                times.push(start.elapsed().as_secs_f64());
            }
            assert!(index.finish().is_some());
        }
        times.sort_by(f64::total_cmp);
        let p95 = times[(times.len() * 95 / 100).min(times.len() - 1)];
        eprintln!("index {rate} Hz stereo / 100 ms audio: p95 {p95:.6} s");
        if !cfg!(debug_assertions) {
            assert!(p95 < 0.010, "under 10% realtime at every rate");
            if rate == 48000 {
                assert!(p95 <= 0.001, "48k stereo worker budget");
            }
        }
    }
}
