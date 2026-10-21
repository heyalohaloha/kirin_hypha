//! Signal-domain oracles: no wall clock, worker, filesystem or host is involved.
use super::*;

fn response_db(band: AttackBand, rate: u32, hz: f64) -> f64 {
    let frames = (f64::from(rate) * 40.0 / band.centre_hz()).ceil() as usize;
    let wave = |n: usize| std::f64::consts::TAU * hz * n as f64 / f64::from(rate);
    let input = (0..frames)
        .map(|n| wave(n).sin() as f32)
        .collect::<Vec<_>>();
    let mut output = Vec::new();
    BandFilter::new(band, rate).run(&input, &mut output);
    // Fit sine and cosine, so the phase shift and non-integer cycles do not bias amplitude.
    let (mut ss, mut cc, mut sc, mut ys, mut yc) = (0.0, 0.0, 0.0, 0.0, 0.0);
    for (n, y) in output.iter().enumerate().skip(frames / 2) {
        let (s, c) = wave(n).sin_cos();
        ss += s * s;
        cc += c * c;
        sc += s * c;
        ys += y * s;
        yc += y * c;
    }
    let determinant = ss * cc - sc * sc;
    let amplitude = ((ys * cc - yc * sc) / determinant).hypot((yc * ss - ys * sc) / determinant);
    20.0 * amplitude.log10()
}

// Independent Butterworth power response under the bilinear frequency map. This does not
// construct biquads or use their coefficients, state, or magnitude implementation.
fn butterworth_db(band: AttackBand, rate: u32, hz: f64) -> f64 {
    let warp = |f: f64| (std::f64::consts::PI * f / f64::from(rate)).tan();
    let power = |f: f64| {
        1.0 / ((1.0 + (warp(band.lower_hz()) / warp(f)).powi(8))
            * (1.0 + (warp(f) / warp(band.upper_hz())).powi(8)))
    };
    10.0 * (power(hz) / power(band.centre_hz())).log10()
}

#[test]
fn octave_filter_signal_response_is_fourth_order_at_every_rate() {
    for rate in [44_100, 48_000, 96_000, 192_000] {
        for index in 1..=8 {
            let band = AttackBand::from_index(index).unwrap();
            for hz in [
                band.centre_hz(),
                band.lower_hz(),
                band.upper_hz(),
                band.lower_hz() / 2.0,
                band.upper_hz() * 2.0,
                band.lower_hz() / 4.0,
                band.upper_hz() * 4.0,
            ] {
                // Frequencies beyond Nyquist have no corresponding sampled sinusoid.
                if hz >= f64::from(rate) * 0.49 {
                    continue;
                }
                let measured = response_db(band, rate, hz);
                let expected = butterworth_db(band, rate, hz);
                assert!(
                    (measured - expected).abs() < 0.08,
                    "{rate} Hz band {index}, {hz} Hz: {measured:.3} vs {expected:.3} dB"
                );
                if rate == 48_000 {
                    println!("response band {index} at {hz:.3} Hz: {measured:.3} dB");
                }
            }
        }
    }
    let kick = response_db(AttackBand::from_index(2).unwrap(), 48_000, 50.0);
    assert!((-19.6..-19.0).contains(&kick), "50 Hz in 125: {kick:.3} dB");
}

#[test]
fn analysis_lead_settles_zero_state_below_minus_sixty_db_before_the_head() {
    for rate in [44_100, 48_000, 96_000, 192_000] {
        for index in 1..=8 {
            let band = AttackBand::from_index(index).unwrap();
            let period = band.period_frames(rate);
            let onset = period * 40 + i64::from(rate);
            let (from, _) = analysis_range(band, rate, onset, onset + i64::from(rate));
            let head = onset - frames_for_micros(rate, ATTACK_BAND_HEAD_LEAD_MICROS);
            let mut worst = 0.0_f64;
            for hz in [
                0.0,
                band.lower_hz() / 2.0,
                band.lower_hz(),
                band.centre_hz(),
                band.upper_hz(),
            ] {
                for phase in [0.0, std::f64::consts::FRAC_PI_2] {
                    let input = (from - period * 32..head + period)
                        .map(|n| {
                            (std::f64::consts::TAU * hz * n as f64 / f64::from(rate) + phase).cos()
                                as f32
                        })
                        .collect::<Vec<_>>();
                    let mut settled = Vec::new();
                    let mut zero = Vec::new();
                    let filter = BandFilter::new(band, rate);
                    filter.run(&input, &mut settled);
                    filter.run(&input[(period * 32) as usize..], &mut zero);
                    for n in (head - from) as usize..zero.len() {
                        worst = worst.max((zero[n] - settled[n + (period * 32) as usize]).abs());
                    }
                }
            }
            // The two quadrature inputs bound every starting phase by sqrt(2).
            assert!(
                worst < 0.001 / std::f64::consts::SQRT_2,
                "{rate} Hz band {index}: residual {worst}"
            );
            println!(
                "settle {rate} Hz band {index}: {:.1} dB",
                20.0 * worst.log10()
            );
        }
    }
}
