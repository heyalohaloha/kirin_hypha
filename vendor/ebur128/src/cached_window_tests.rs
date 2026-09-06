use super::*;
use crate::Mode;

fn close(actual: f64, expected: f64) {
    if actual == expected {
        return;
    }
    assert!(
        actual.is_finite() && expected.is_finite(),
        "{} vs {}",
        actual,
        expected
    );
    assert!(
        (actual - expected).abs() <= 1.0e-9,
        "{} vs {}",
        actual,
        expected
    );
}

fn compare(ebu: &EbuR128) {
    close(
        ebu.loudness_momentary_cached().unwrap(),
        ebu.loudness_momentary().unwrap(),
    );
    match (ebu.loudness_shortterm_cached(), ebu.loudness_shortterm()) {
        (Ok(a), Ok(b)) => close(a, b),
        (Err(_), Err(_)) => (),
        pair => panic!("different mode contract: {:?}", pair),
    }
}

#[test]
fn tiled_ranges_and_partial_overwrites_match_scalar_oracle() {
    for channels in [1, 2, 6] {
        let stride = 1003;
        let mut audio: Vec<f64> = (0..stride * channels)
            .map(|i| ((i * 313 % 1021) as f64 - 511.0) / 512.0)
            .collect();
        let map = crate::ebur128::default_channel_map(channels as u32);
        let mut cache = EnergyCache::new(&audio, channels as u32);
        for (start, count) in [(0, 1), (1, 127), (127, 2), (129, 512), (1000, 3), (0, 1003)] {
            for c in 0..channels {
                audio[c * stride + start..c * stride + start + count].fill(1.0e-20);
            }
            cache.refresh(&audio, start, count);
            for end in [0, 1, 127, 128, 129, 512, 999, 1003] {
                for frames in [1, 127, 128, 129, 256, 1003] {
                    let scalar =
                        crate::filter::Filter::calc_gating_block(frames, &audio, end, &map);
                    let fast = cache.energy(frames, &audio, end, &map);
                    assert!((scalar - fast).abs() <= 1e-12 * scalar.abs().max(1e-100));
                }
            }
        }
    }
}

#[test]
fn rates_channels_partial_blocks_and_wrap_match() {
    for rate in [8_000, 11_025, 44_100, 44_105, 48_000, 96_000, 192_000] {
        for channels in [1, 2] {
            let mut ebu = EbuR128::new(channels, rate, Mode::M | Mode::S).unwrap();
            ebu.enable_cached_window_queries();
            let mut position = 0usize;
            let mut block = 0;
            while position < rate as usize * 7 {
                let frames = [1, 17, 127, 128, 129, 513, rate as usize / 10][block % 7];
                let input: Vec<f64> = (0..frames * channels as usize)
                    .map(|i| {
                        let t = (position + i / channels as usize) as f64 / rate as f64;
                        let amplitude = if t < 2.0 {
                            0.6
                        } else if t < 4.0 {
                            1e-15
                        } else {
                            0.0
                        };
                        amplitude
                            * (t * 733.0 * std::f64::consts::TAU + i as f64 % channels as f64).sin()
                    })
                    .collect();
                ebu.add_frames_f64(&input).unwrap();
                compare(&ebu);
                position += frames;
                block += 1;
            }
            // More than a full zero window: no subtractive running-sum residual remains.
            ebu.add_frames_f64(&vec![0.0; rate as usize * channels as usize * 4])
                .unwrap();
            compare(&ebu);
            assert_eq!(ebu.loudness_shortterm_cached().unwrap(), -f64::INFINITY);
        }
    }
}

#[test]
fn opt_in_after_input_maps_reset_and_reconfiguration() {
    let mut ebu = EbuR128::new(1, 48_000, Mode::M | Mode::S).unwrap();
    ebu.add_frames_f64(&vec![0.2; 24017]).unwrap();
    compare(&ebu); // scalar fallback before opt-in
    ebu.enable_cached_window_queries();
    compare(&ebu);
    ebu.enable_cached_window_queries(); // idempotent, not reset
    ebu.set_channel(0, Channel::DualMono).unwrap();
    compare(&ebu);
    ebu.set_channel_map(&[Channel::Unused]).unwrap();
    compare(&ebu);
    ebu.add_frames_f64(&vec![0.1; 137]).unwrap();
    ebu.set_channel(0, Channel::Left).unwrap();
    compare(&ebu);
    ebu.reset();
    compare(&ebu);
    assert_eq!(ebu.loudness_momentary_cached().unwrap(), -f64::INFINITY);
    for (channels, rate) in [(2, 44_105), (6, 8_000), (1, 96_000)] {
        ebu.change_parameters(channels, rate).unwrap();
        compare(&ebu);
        ebu.add_frames_f64(&vec![0.3; channels as usize * rate as usize])
            .unwrap();
        compare(&ebu);
        ebu.set_max_window(4701).unwrap();
        compare(&ebu);
        ebu.add_frames_f64(&vec![0.2; channels as usize * rate as usize * 5])
            .unwrap();
        compare(&ebu);
        ebu.set_max_window(3000).unwrap();
        compare(&ebu);
        assert!(ebu.change_parameters(0, rate).is_err());
        compare(&ebu);
    }
    let mut momentary = EbuR128::new(2, 48_000, Mode::M).unwrap();
    momentary.enable_cached_window_queries();
    compare(&momentary); // S remains invalid; no fake data
}

#[test]
fn real_wav_and_silence_preserve_canonical_integrated_lra_and_peaks_bitwise() {
    let path = std::path::Path::new(env!("CARGO_MANIFEST_DIR"))
        .join("../../test_signals/S-1_1kHz_sine_m6dBFS_10s.wav");
    let mut reader = hound::WavReader::open(path).unwrap();
    assert_eq!(reader.spec().sample_rate, 48000);
    assert_eq!(reader.spec().channels, 2);
    let mut input: Vec<f64> = reader
        .samples::<f32>()
        .map(|s| f64::from(s.unwrap()))
        .collect();
    assert_eq!(input.len(), 960000);
    input.extend(std::iter::repeat(0.0).take(8 * 48000 * 2));
    let mode = Mode::M | Mode::S | Mode::I | Mode::LRA | Mode::TRUE_PEAK;
    let mut scalar = EbuR128::new(2, 48000, mode).unwrap();
    let mut cached = EbuR128::new(2, 48000, mode).unwrap();
    cached.enable_cached_window_queries();
    for chunk in input.chunks(960) {
        scalar.add_frames_f64(chunk).unwrap();
        cached.add_frames_f64(chunk).unwrap();
        compare(&cached);
        close(
            cached.loudness_momentary_cached().unwrap(),
            scalar.loudness_momentary().unwrap(),
        );
        close(
            cached.loudness_shortterm_cached().unwrap(),
            scalar.loudness_shortterm().unwrap(),
        );
        assert_eq!(
            cached.loudness_global().unwrap().to_bits(),
            scalar.loudness_global().unwrap().to_bits()
        );
        assert_eq!(
            cached.loudness_range().unwrap().to_bits(),
            scalar.loudness_range().unwrap().to_bits()
        );
        for channel in 0..2 {
            assert_eq!(
                cached.true_peak(channel).unwrap().to_bits(),
                scalar.true_peak(channel).unwrap().to_bits()
            );
        }
    }
}
