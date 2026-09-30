//! Every outcome a band measure states, the ring that keeps the audio, and measuring from it.

use super::tests::{audio_for, band, measure, rises, Burst};
use super::*;

#[test]
fn silence_and_a_band_below_the_presence_floor_are_silent() {
    let silent = measure(band(4), 48_000, 2, None, |_| 0.0).unwrap();
    assert_eq!(silent.sound, BandSound::Silent);
    assert!(silent.has_valid_layout());
    // -72 dBFS peak envelope: a sine of amplitude 10^(-72/20) x sqrt 2 sits on the floor.
    let faint = Burst {
        carrier_hz: 4_000.0,
        delay_ms: 0.0,
        rise_ms: 6.0,
        tau_ms: 45.0,
        amplitude: 10.0_f64.powf(-74.0 / 20.0) * std::f64::consts::SQRT_2,
    };
    let below = measure(band(7), 48_000, 2, None, |t| faint.sample(t)).unwrap();
    assert_eq!(below.sound, BandSound::Silent);
    let present = Burst {
        amplitude: 10.0_f64.powf(-70.0 / 20.0) * std::f64::consts::SQRT_2,
        ..faint
    };
    let above = measure(band(7), 48_000, 2, None, |t| present.sample(t)).unwrap();
    assert!(matches!(above.sound, BandSound::Rises { .. }));
}

#[test]
fn the_tail_states_why_a_release_is_missing() {
    let burst = Burst {
        carrier_hz: 500.0,
        delay_ms: 0.0,
        rise_ms: 6.0,
        tau_ms: 45.0,
        amplitude: 0.5,
    };
    // The next onset 50 ms in cuts the fall short: that is the next hit, not a long tail.
    let next = 48_000 + 2_400;
    let cut = measure(band(4), 48_000, 2, Some(next), |t| burst.sample(t)).unwrap();
    assert_eq!(
        (cut.span_end_sample, cut.span_end),
        (next, BandSpanEnd::NextHit)
    );
    assert_eq!(rises(&cut).1, BandRelease::CutByNextHit);
    assert!(matches!(rises(&cut).0, BandArrival::At { .. }));
    assert_eq!(
        cut.envelope.tail[63],
        centi_db(ATTACK_LEVEL_FLOOR_DBFS),
        "points past the tail end sit on the floor"
    );
    // A ring-out longer than the 300 ms window is at least as long as the window allows.
    let long = Burst {
        tau_ms: 200.0,
        ..burst
    };
    let measured = measure(band(4), 48_000, 2, None, |t| long.sample(t)).unwrap();
    let BandRelease::AtLeast(bound) = rises(&measured).1 else {
        panic!("{:?}", rises(&measured).1);
    };
    let expected = 14_400.0 - measured.peak_frames;
    assert!((bound - expected).abs() < 1.0, "{bound} vs {expected}");
    assert_eq!(
        span_end_for(48_000, 48_000, Some(47_000)).0,
        48_000 + 14_400
    );
    assert_eq!(
        span_end_for(48_000, 48_000, Some(200_000)),
        (48_000 + 14_400, BandSpanEnd::Window)
    );
}

#[test]
fn audio_that_ends_early_still_measures_past_the_peak_or_not_at_all() {
    let band = band(4);
    let onset = 48_000;
    let (span_end, _) = span_end_for(48_000, onset, None);
    let half = half_window_frames(band, 48_000);
    // 150 ms of audio after the onset: past the 130 ms peak search, short of the window.
    let end = audio_end_span(band, 48_000, onset, span_end, onset + 7_200).unwrap();
    assert_eq!(end, onset + 7_200 - half);
    // 100 ms is not past the peak search: not measured at all.
    assert!(audio_end_span(band, 48_000, onset, span_end, onset + 4_800).is_none());
    // Audio past the window keeps the window.
    assert_eq!(
        audio_end_span(band, 48_000, onset, span_end, span_end + 48_000),
        Some(span_end)
    );
    let long = Burst {
        carrier_hz: 500.0,
        delay_ms: 0.0,
        rise_ms: 6.0,
        tau_ms: 200.0,
        amplitude: 0.5,
    };
    let audio = audio_for(band, 48_000, 2, onset, end, |t| long.sample(t));
    let mut scratch = BandScratch::default();
    let measured = measure_band(
        band,
        48_000,
        2,
        onset,
        end,
        BandSpanEnd::AudioEnd,
        &audio,
        &mut scratch,
    )
    .unwrap();
    assert_eq!(measured.span_end, BandSpanEnd::AudioEnd);
    assert!(matches!(rises(&measured).1, BandRelease::AtLeast(_)));
    assert!(measured.has_valid_layout());
}

#[test]
fn a_band_still_ringing_before_the_onset_has_no_timed_start() {
    // The previous hit's ring-out sits above this hit's peak - 20 dB throughout the lead, and
    // the hit still rises more than 3 dB above it.
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
    assert_eq!(rises(&measured).0, BandArrival::Ringing);
    assert!(measured.has_valid_layout());
}

#[test]
fn a_hit_that_brings_nothing_to_the_band_only_rings_on() {
    // A kick's 63 Hz ring-out decays through a hi-hat's onset: the hat adds nothing at 63 Hz, so
    // neither the ring-out's level nor its fall is the hat's.
    let ring_out =
        |t: f64| 0.5 * (-(t + 0.1) / 0.08).exp() * (std::f64::consts::TAU * 62.5 * t).sin();
    let hat = |t: f64| {
        if t < 0.0 {
            0.0
        } else {
            0.3 * (-t / 0.02).exp() * (std::f64::consts::TAU * 8_000.0 * t).sin()
        }
    };
    let low = measure(band(1), 48_000, 2, None, |t| ring_out(t) + hat(t)).unwrap();
    assert_eq!(low.sound, BandSound::RingsOn);
    assert!(low.has_valid_layout());
    // The same audio in the hat's own band rises.
    let high = measure(band(8), 48_000, 2, None, |t| ring_out(t) + hat(t)).unwrap();
    assert!(matches!(high.sound, BandSound::Rises { .. }));
}

#[test]
fn envelope_points_keep_a_hundredth_of_a_db() {
    for value in [-120.0_f32, -72.0, -12.34, 0.0, 6.5] {
        assert!((dbfs_from_centi(centi_db(value)) - value).abs() <= 0.005);
    }
    assert!(BandEnvelope::default().is_valid());
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
    // The whole capacity is reserved up front: the ring never reallocates while it runs.
    let reserved = ring.samples.capacity();
    assert!(reserved >= 2 * 7_000);
    ring.push_block(8_111, &vec![0.25; 2 * 9_000]);
    assert_eq!((ring.first(), ring.end()), (8_111 + 2_000, 8_111 + 9_000));
    assert_eq!(ring.samples.capacity(), reserved);
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
    let (span_end, reason) = span_end_for(48_000, onset, None);
    let (from, to) = analysis_range(band(4), 48_000, onset, span_end);
    let mut ring = AttackBandRing::new(48_000, 2);
    let mut scratch = BandScratch::default();
    // Everything but the last frame: not yet.
    let audio = audio_for(band(4), 48_000, 2, onset, span_end, |t| burst.sample(t));
    ring.push_block(from, &audio[..audio.len() - 2]);
    let at = |ring: &AttackBandRing, scratch: &mut BandScratch| {
        measure_from_ring(ring, band(4), 48_000, 2, onset, span_end, reason, scratch)
    };
    assert!(at(&ring, &mut scratch).is_none());
    ring.push_block(to - 1, &audio[audio.len() - 2..]);
    let measured = at(&ring, &mut scratch).unwrap();
    assert_eq!(measured.event_sample, onset);
    assert!(matches!(rises(&measured).1, BandRelease::At(_)));
}
