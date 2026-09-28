use std::time::Instant;

use super::*;

/// A drum-like hit: silence, then a carrier at `carrier_hz` whose amplitude rises linearly over
/// `rise_ms`, then decays with the time constant `tau_ms`.
#[derive(Clone, Copy)]
struct Burst {
    carrier_hz: f64,
    delay_ms: f64,
    rise_ms: f64,
    tau_ms: f64,
    amplitude: f64,
}

impl Burst {
    fn sample(&self, seconds_from_onset: f64) -> f64 {
        let t = seconds_from_onset - self.delay_ms / 1_000.0;
        if t < 0.0 {
            return 0.0;
        }
        let rise = self.rise_ms / 1_000.0;
        let envelope = if t < rise {
            t / rise
        } else {
            (-(t - rise) / (self.tau_ms / 1_000.0)).exp()
        };
        self.amplitude
            * envelope
            * (std::f64::consts::TAU * self.carrier_hz * seconds_from_onset).sin()
    }
}

/// Interleaved audio covering the analysis range of one measurement, `channels` copies of one
/// generator evaluated at each frame's time from the onset.
fn audio_for(
    band: AttackBand,
    sample_rate: u32,
    channels: usize,
    onset: i64,
    span_end: i64,
    mut generate: impl FnMut(f64) -> f64,
) -> Vec<f32> {
    let (from, to) = analysis_range(band, sample_rate, onset, span_end);
    let mut audio = Vec::with_capacity((to - from) as usize * channels);
    for frame in from..to {
        let value = generate((frame - onset) as f64 / f64::from(sample_rate)) as f32;
        for _ in 0..channels {
            audio.push(value);
        }
    }
    audio
}

fn measure(
    band: AttackBand,
    sample_rate: u32,
    channels: usize,
    next_onset: Option<i64>,
    generate: impl FnMut(f64) -> f64,
) -> Option<AttackBandMeasure> {
    let onset = 48_000;
    let span_end = span_end_for(sample_rate, onset, next_onset);
    let audio = audio_for(band, sample_rate, channels, onset, span_end, generate);
    let mut scratch = BandScratch::default();
    measure_band(
        band,
        sample_rate,
        channels,
        onset,
        span_end,
        &audio,
        &mut scratch,
    )
}

fn ms(frames: f32, sample_rate: u32) -> f64 {
    f64::from(frames) * 1_000.0 / f64::from(sample_rate)
}

fn band(index: u8) -> AttackBand {
    AttackBand::from_index(index).unwrap()
}

#[test]
fn bands_tile_the_octaves_without_gaps() {
    assert!(AttackBand::from_index(0).is_none());
    assert!(AttackBand::from_index(9).is_none());
    let labels = (1..=8)
        .map(|index| band(index).nominal_label())
        .collect::<Vec<_>>();
    assert_eq!(labels, ["63", "125", "250", "500", "1k", "2k", "4k", "8k"]);
    for index in 1..8 {
        assert!((band(index).upper_hz() - band(index + 1).lower_hz()).abs() < 1e-9);
    }
    assert_eq!(band(1).centre_hz(), 62.5);
    assert_eq!(band(8).centre_hz(), 8_000.0);
    assert_eq!(band(1).period_frames(48_000), 768);
    assert_eq!(band(8).period_frames(48_000), 6);
    assert_eq!(band(1).resolution_micros(), 16_000);
    assert_eq!(band(5).resolution_micros(), 1_000);
}

fn steady_gain_db(filter: &BandFilter, frequency_hz: f64, sample_rate: u32) -> f64 {
    let frames = sample_rate as usize;
    let input = (0..frames)
        .map(|frame| {
            (std::f64::consts::TAU * frequency_hz * frame as f64 / f64::from(sample_rate)).sin()
                as f32
        })
        .collect::<Vec<_>>();
    let mut output = Vec::new();
    filter.run(&input, &mut output);
    let settled = &output[frames / 2..];
    let peak = settled
        .iter()
        .fold(0.0_f64, |peak, value| peak.max(value.abs()));
    20.0 * peak.log10()
}

#[test]
fn filter_passes_the_centre_and_rejects_two_octaves_away() {
    let filter = BandFilter::new(band(3), 48_000);
    assert!(steady_gain_db(&filter, 250.0, 48_000).abs() < 0.3);
    // Two octaves from the centre is 1.5 octaves past an edge: 12 dB per octave from there.
    assert!(steady_gain_db(&filter, 62.5, 48_000) < -14.0);
    assert!(steady_gain_db(&filter, 1_000.0, 48_000) < -14.0);
    assert!(steady_gain_db(&filter, 31.25, 48_000) < -26.0);
    assert!(steady_gain_db(&filter, 2_000.0, 48_000) < -26.0);
    // The nominal edges sit a little inside the -3 dB points.
    for edge in [band(3).lower_hz(), band(3).upper_hz()] {
        let gain = steady_gain_db(&filter, edge, 48_000);
        assert!((-2.5..-0.7).contains(&gain), "edge {edge} Hz: {gain} dB");
    }
    // The same holds for the top band at the lowest supported rate.
    let top = BandFilter::new(band(8), 44_100);
    assert!(steady_gain_db(&top, 8_000.0, 44_100).abs() < 0.3);
}

#[test]
fn measure_reads_arrival_attack_release_and_level_of_a_shaped_burst() {
    let burst = Burst {
        carrier_hz: 500.0,
        delay_ms: 0.0,
        rise_ms: 6.0,
        tau_ms: 45.0,
        amplitude: 0.5,
    };
    let measured = measure(band(4), 48_000, 2, None, |t| burst.sample(t)).unwrap();
    assert_eq!(measured.band, band(4));
    assert_eq!(measured.span_end_sample, 48_000 + 14_400);
    // The envelope is the one-period RMS of a sine of amplitude 0.5: -9.03 dBFS.
    assert!(
        (measured.level_dbfs + 9.03).abs() < 0.6,
        "{}",
        measured.level_dbfs
    );
    let arrival = ms(measured.arrival_frames.unwrap(), 48_000);
    let attack = ms(measured.attack_frames.unwrap(), 48_000);
    let release = ms(measured.release_frames.unwrap(), 48_000);
    // 10 % of a 6 ms linear rise is at 0.6 ms; 10 % to 90 % takes 4.8 ms; the decay reaches
    // -20 dB at tau x ln 10 = 103.6 ms. Each within the band's 2 ms period.
    assert!((arrival - 0.6).abs() < 2.0, "arrival {arrival} ms");
    assert!((attack - 4.8).abs() < 2.0, "attack {attack} ms");
    assert!((release - 103.6).abs() < 4.0, "release {release} ms");
    assert!(measured.has_valid_layout());
    let head_peak = measured
        .head_dbfs
        .iter()
        .fold(f32::MIN, |peak, value| peak.max(*value));
    assert!((head_peak - measured.level_dbfs).abs() < 1.0);
    // The tail falls: its last point is far below its first.
    assert!(measured.tail_dbfs[63] < measured.tail_dbfs[1] - 20.0);
}

#[test]
fn post_minus_pre_delay_reads_a_known_shift_in_low_and_mid_bands() {
    for (index, carrier_hz, tolerance_ms) in [(1, 62.5, 0.5), (4, 500.0, 0.3)] {
        let pre_burst = Burst {
            carrier_hz,
            delay_ms: 0.0,
            rise_ms: 6.0,
            tau_ms: 45.0,
            amplitude: 0.5,
        };
        let post_burst = Burst {
            delay_ms: 2.4,
            ..pre_burst
        };
        let pre = measure(band(index), 48_000, 2, None, |t| pre_burst.sample(t)).unwrap();
        let post = measure(band(index), 48_000, 2, None, |t| post_burst.sample(t)).unwrap();
        let delay = ms(band_delay_frames(&pre, &post).unwrap(), 48_000);
        assert!(
            (delay - 2.4).abs() < tolerance_ms,
            "band {index}: delay {delay} ms"
        );
        assert!((pre.level_dbfs - post.level_dbfs).abs() < 0.2);
    }
}

#[test]
fn a_slower_rise_a_longer_ring_out_and_a_lower_level_read_as_such() {
    let pre_burst = Burst {
        carrier_hz: 250.0,
        delay_ms: 0.0,
        rise_ms: 6.0,
        tau_ms: 45.0,
        amplitude: 0.5,
    };
    let post_burst = Burst {
        rise_ms: 7.0,
        tau_ms: 55.0,
        amplitude: 0.5 * 10.0_f64.powf(-0.8 / 20.0),
        ..pre_burst
    };
    let pre = measure(band(3), 48_000, 2, None, |t| pre_burst.sample(t)).unwrap();
    let post = measure(band(3), 48_000, 2, None, |t| post_burst.sample(t)).unwrap();
    let attack = ms(
        post.attack_frames.unwrap() - pre.attack_frames.unwrap(),
        48_000,
    );
    let release = ms(
        post.release_frames.unwrap() - pre.release_frames.unwrap(),
        48_000,
    );
    assert!((attack - 0.8).abs() < 1.5, "attack difference {attack} ms");
    assert!(
        (release - 23.0).abs() < 4.0,
        "release difference {release} ms"
    );
    assert!((post.level_dbfs - pre.level_dbfs + 0.8).abs() < 0.2);
}

/// Group delay of one biquad at `frequency_hz`, from the phase slope of its response.
fn group_delay_seconds(stage: &Biquad, frequency_hz: f64, sample_rate: u32) -> f64 {
    let phase = |hz: f64| {
        let w = std::f64::consts::TAU * hz / f64::from(sample_rate);
        let (re, im) = (|k: f64| (w * k).cos(), |k: f64| -(w * k).sin());
        let numerator = (
            stage.b0 + stage.b1 * re(1.0) + stage.b2 * re(2.0),
            stage.b1 * im(1.0) + stage.b2 * im(2.0),
        );
        let denominator = (
            1.0 + stage.a1 * re(1.0) + stage.a2 * re(2.0),
            stage.a1 * im(1.0) + stage.a2 * im(2.0),
        );
        numerator.1.atan2(numerator.0) - denominator.1.atan2(denominator.0)
    };
    let delta = 0.01;
    let mut slope = phase(frequency_hz + delta) - phase(frequency_hz - delta);
    if slope > std::f64::consts::PI {
        slope -= std::f64::consts::TAU;
    } else if slope < -std::f64::consts::PI {
        slope += std::f64::consts::TAU;
    }
    -slope / (std::f64::consts::TAU * 2.0 * delta)
}

#[test]
fn a_minimum_phase_high_pass_delays_the_low_band_by_its_group_delay() {
    // A 40 Hz second-order Butterworth high-pass, as a crossover or a low cut would add.
    let high_pass = Biquad::new(true, 40.0, 48_000);
    let expected_ms = group_delay_seconds(&high_pass, 62.5, 48_000) * 1_000.0;
    assert!(expected_ms > 1.0 && expected_ms < 8.0, "{expected_ms}");
    let burst = Burst {
        carrier_hz: 62.5,
        delay_ms: 0.0,
        rise_ms: 8.0,
        tau_ms: 60.0,
        amplitude: 0.5,
    };
    let pre = measure(band(1), 48_000, 1, None, |t| burst.sample(t)).unwrap();
    let mut state = [0.0_f64; 4];
    let post = measure(band(1), 48_000, 1, None, |t| {
        let x = burst.sample(t);
        let [x1, x2, y1, y2] = state;
        let y = high_pass.b0 * x + high_pass.b1 * x1 + high_pass.b2 * x2
            - high_pass.a1 * y1
            - high_pass.a2 * y2;
        state = [x, x1, y, y1];
        y
    })
    .unwrap();
    let delay_ms = ms(band_delay_frames(&pre, &post).unwrap(), 48_000);
    assert!(
        (delay_ms - expected_ms).abs() < 0.25 * expected_ms + 0.5,
        "measured {delay_ms} ms, group delay {expected_ms} ms"
    );
}

#[test]
fn silence_and_a_band_below_the_presence_floor_measure_nothing() {
    assert!(measure(band(4), 48_000, 2, None, |_| 0.0).is_none());
    // -72 dBFS peak envelope: a sine of amplitude 10^(-72/20) x sqrt 2 sits on the floor.
    let faint = Burst {
        carrier_hz: 4_000.0,
        delay_ms: 0.0,
        rise_ms: 6.0,
        tau_ms: 45.0,
        amplitude: 10.0_f64.powf(-74.0 / 20.0) * std::f64::consts::SQRT_2,
    };
    assert!(measure(band(7), 48_000, 2, None, |t| faint.sample(t)).is_none());
    let present = Burst {
        amplitude: 10.0_f64.powf(-70.0 / 20.0) * std::f64::consts::SQRT_2,
        ..faint
    };
    assert!(measure(band(7), 48_000, 2, None, |t| present.sample(t)).is_some());
}

#[test]
fn a_tail_cut_by_the_next_onset_has_no_release() {
    let burst = Burst {
        carrier_hz: 500.0,
        delay_ms: 0.0,
        rise_ms: 6.0,
        tau_ms: 45.0,
        amplitude: 0.5,
    };
    let next = 48_000 + 2_400;
    let measured = measure(band(4), 48_000, 2, Some(next), |t| burst.sample(t)).unwrap();
    assert_eq!(measured.span_end_sample, next);
    assert!(measured.release_frames.is_none());
    assert!(measured.arrival_frames.is_some());
    assert!(measured.tail_dbfs[63] == ATTACK_LEVEL_FLOOR_DBFS);
    assert_eq!(span_end_for(48_000, 48_000, Some(47_000)), 48_000 + 14_400);
    assert_eq!(span_end_for(48_000, 48_000, Some(200_000)), 48_000 + 14_400);
}

#[test]
fn a_band_still_ringing_before_the_onset_has_no_arrival() {
    // The previous hit's ring-out sits above this hit's peak - 20 dB throughout the lead.
    let burst = Burst {
        carrier_hz: 500.0,
        delay_ms: 0.0,
        rise_ms: 6.0,
        tau_ms: 45.0,
        amplitude: 0.5,
    };
    let measured = measure(band(4), 48_000, 2, None, |t| {
        burst.sample(t) + 0.2 * (std::f64::consts::TAU * 500.0 * t).sin()
    })
    .unwrap();
    assert!(measured.arrival_frames.is_none());
    assert!(measured.attack_frames.is_none());
    assert!(measured.has_valid_layout());
}

#[test]
fn ring_addresses_content_positions_and_restarts_on_a_gap() {
    let mut ring = AttackBandRing::new(1_000, 2);
    assert!(ring.is_empty());
    ring.push_block(100, &[1.0, 2.0, 3.0, 4.0]);
    ring.push_block(102, &[5.0, 6.0]);
    assert_eq!((ring.first(), ring.end()), (100, 103));
    let mut copied = Vec::new();
    assert!(ring.copy_frames(101, 103, &mut copied));
    assert_eq!(copied, [3.0, 4.0, 5.0, 6.0]);
    assert!(!ring.copy_frames(99, 101, &mut copied));
    assert!(!ring.copy_frames(102, 104, &mut copied));
    assert!(!ring.copy_frames(102, 102, &mut copied));
    ring.push_block(110, &[7.0, 8.0]);
    assert_eq!((ring.first(), ring.end()), (110, 111));
    // Seven seconds at 1 kHz is 7 000 frames: older frames fall off the front.
    let block = vec![0.5; 2 * 4_000];
    ring.push_block(111, &block);
    ring.push_block(4_111, &block);
    assert_eq!(ring.end(), 8_111);
    assert_eq!(ring.first(), 8_111 - 7_000);
    ring.push_block(8_111, &[1.0]);
    assert_eq!(ring.end(), 8_111, "an odd sample count is refused");
}

#[test]
fn measures_are_the_same_at_every_supported_rate() {
    let burst = Burst {
        carrier_hz: 250.0,
        delay_ms: 1.5,
        rise_ms: 6.0,
        tau_ms: 45.0,
        amplitude: 0.5,
    };
    let reference = measure(band(3), 48_000, 2, None, |t| burst.sample(t)).unwrap();
    for rate in [44_100, 96_000, 192_000] {
        let measured = measure(band(3), rate, 2, None, |t| burst.sample(t)).unwrap();
        for (label, a, b) in [
            ("arrival", reference.arrival_frames, measured.arrival_frames),
            ("attack", reference.attack_frames, measured.attack_frames),
            ("release", reference.release_frames, measured.release_frames),
        ] {
            let difference = (ms(a.unwrap(), 48_000) - ms(b.unwrap(), rate)).abs();
            assert!(
                difference < 0.5,
                "{label} differs by {difference} ms at {rate}"
            );
        }
        assert!((reference.level_dbfs - measured.level_dbfs).abs() < 0.2);
    }
}

#[test]
fn measure_from_ring_needs_the_whole_analysis_range() {
    let burst = Burst {
        carrier_hz: 500.0,
        delay_ms: 0.0,
        rise_ms: 6.0,
        tau_ms: 45.0,
        amplitude: 0.5,
    };
    let onset = 48_000;
    let span_end = span_end_for(48_000, onset, None);
    let (from, to) = analysis_range(band(4), 48_000, onset, span_end);
    let mut ring = AttackBandRing::new(48_000, 2);
    let mut scratch = BandScratch::default();
    // Everything but the last frame: not yet.
    let audio = audio_for(band(4), 48_000, 2, onset, span_end, |t| burst.sample(t));
    ring.push_block(from, &audio[..audio.len() - 2]);
    assert!(measure_from_ring(&ring, band(4), 48_000, 2, onset, span_end, &mut scratch).is_none());
    ring.push_block(to - 1, &audio[audio.len() - 2..]);
    let measured =
        measure_from_ring(&ring, band(4), 48_000, 2, onset, span_end, &mut scratch).unwrap();
    assert_eq!(measured.event_sample, onset);
    assert!(measured.release_frames.is_some());
}

/// Run with `-- --ignored --nocapture`: the cost of one band measurement per hit.
#[test]
#[ignore]
fn reports_the_cost_of_one_band_measurement() {
    for (rate, channels) in [(48_000, 2), (96_000, 2), (192_000, 2)] {
        for index in [1, 4, 8] {
            let burst = Burst {
                carrier_hz: band(index).centre_hz(),
                delay_ms: 0.0,
                rise_ms: 6.0,
                tau_ms: 45.0,
                amplitude: 0.5,
            };
            let onset = 48_000;
            let span_end = span_end_for(rate, onset, None);
            let audio = audio_for(band(index), rate, channels, onset, span_end, |t| {
                burst.sample(t)
            });
            let mut scratch = BandScratch::default();
            let iterations = 200;
            let started = Instant::now();
            for _ in 0..iterations {
                let measured = measure_band(
                    band(index),
                    rate,
                    channels,
                    onset,
                    span_end,
                    &audio,
                    &mut scratch,
                );
                assert!(measured.is_some());
            }
            let micros = started.elapsed().as_secs_f64() * 1e6 / f64::from(iterations);
            println!(
                "band {} at {rate} Hz x{channels}: {micros:.0} us per hit ({} frames filtered)",
                band(index).nominal_label(),
                audio.len() / channels
            );
        }
    }
}
