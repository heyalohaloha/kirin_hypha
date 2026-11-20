use super::*;

fn tone(rate: u32, channels: usize, slot: usize) -> Vec<f32> {
    (0..rate as usize / 10)
        .flat_map(|frame| {
            let time = (slot * rate as usize / 10 + frame) as f32 / rate as f32;
            let value = (time * 997.0 * std::f32::consts::TAU).sin() * 0.25;
            (0..channels).map(move |channel| value * if channel == 0 { 1.0 } else { -0.7 })
        })
        .collect()
}

#[test]
fn reference_metrics_match_existing_engines_at_identical_endpoints() {
    for rate in [44_100, 48_000, 96_000] {
        for channels in [1, 2] {
            let mut reference = ReferenceMetrics::new(rate, channels).unwrap();
            let mut level = MeasureEngine::new(
                rate,
                ChannelLayout::mono_or_stereo_by_count(channels).unwrap(),
            )
            .unwrap();
            let mut sharp = SharpnessContinuousAnalyzer::new(rate, channels).unwrap();
            sharp.reset_at_epoch(0).unwrap();
            let mut levels = VecDeque::new();
            let mut count = 0;
            for slot in 0..32 {
                let mut input = tone(rate, channels, slot);
                match slot % 8 {
                    0 => input.fill(0.0),
                    1 => {
                        input.fill(0.0);
                        input[0] = 0.9;
                    }
                    2 => input.iter_mut().for_each(|sample| *sample *= 0.01),
                    _ => {}
                }
                let end = (slot + 1) as u64 * u64::from(rate / 10);
                let expected = level
                    .push(&input.iter().map(|x| f64::from(*x)).collect::<Vec<_>>())
                    .unwrap();
                levels.push_back((end, expected));
                let expected_sharp = sharp
                    .analyze_aperture(&input, SpectrumChannelMode::Lr, end as i64, 1)
                    .unwrap();
                let actual = reference.push_aperture(&input).unwrap();
                assert_eq!(actual.len(), expected_sharp.len());
                for (actual, sharp) in actual.iter().zip(expected_sharp) {
                    let (end, level) = levels.pop_front().unwrap();
                    assert_eq!(actual.end_sample, end);
                    assert_eq!(actual.end_sample as i64, sharp.presentation_end_samples);
                    // Identical f32 -> f64 PCM and unchanged engine paths: exact equality.
                    assert_eq!(actual.crest_db, level.crest);
                    assert_eq!(actual.psr_db, level.psr);
                    assert_eq!(
                        actual.sharpness_acum,
                        sharp.sharpness.is_finite().then_some(sharp.sharpness)
                    );
                    count += 1;
                }
            }
            assert!(count >= 30);
        }
    }
}

#[test]
fn reference_metrics_reject_bad_input_without_advancing() {
    for (rate, channels) in [(0, 2), (44_105, 2), (48_000, 0), (48_000, 6), (400_000, 2)] {
        assert!(ReferenceMetrics::new(rate, channels).is_err());
    }
    let mut checked = ReferenceMetrics::new(48_000, 2).unwrap();
    let mut fresh = ReferenceMetrics::new(48_000, 2).unwrap();
    let input = tone(48_000, 2, 0);
    assert!(checked.push_aperture(&[]).is_err());
    assert!(checked.push_aperture(&input[..input.len() - 1]).is_err());
    for invalid in [f32::NAN, f32::INFINITY, f32::NEG_INFINITY] {
        let mut bad = input.clone();
        bad[42] = invalid;
        assert!(checked.push_aperture(&bad).is_err());
    }
    assert_eq!(
        checked.push_aperture(&input).unwrap(),
        fresh.push_aperture(&input).unwrap()
    );
}

#[test]
fn reference_metrics_silence_and_restart_do_not_reuse_previous_audio() {
    let mut reference = ReferenceMetrics::new(48_000, 1).unwrap();
    for _ in 0..4 {
        for frame in reference.push_aperture(&vec![0.0; 4800]).unwrap() {
            assert_eq!(frame.crest_db, None);
            assert_eq!(frame.psr_db, None);
        }
    }
    let input = tone(48_000, 1, 0);
    reference.push_aperture(&input).unwrap();
    let mut restarted = ReferenceMetrics::new(48_000, 1).unwrap();
    let first = restarted.push_aperture(&vec![0.0; 4800]).unwrap();
    assert_eq!(first[0].end_sample, 4800);
    assert_eq!(first[0].crest_db, None);
    assert_eq!(first[0].psr_db, None);
}

#[test]
fn reference_metrics_finish_keeps_complete_level_tail_without_padding_sharpness() {
    for rate in [44_100, 48_000, 96_000] {
        for channels in [1, 2] {
            for count in 1..=4 {
                let mut analyzer = ReferenceMetrics::new(rate, channels).unwrap();
                let mut direct = MeasureEngine::new(
                    rate,
                    ChannelLayout::mono_or_stereo_by_count(channels).unwrap(),
                )
                .unwrap();
                let mut expected = Vec::new();
                let mut observed = Vec::new();
                for slot in 0..count {
                    let input = tone(rate, channels, slot);
                    expected.push(
                        direct
                            .push(&input.iter().map(|x| f64::from(*x)).collect::<Vec<_>>())
                            .unwrap(),
                    );
                    observed.extend(analyzer.push_aperture(&input).unwrap());
                }
                let tail = analyzer.finish().unwrap();
                assert!(tail.iter().all(|frame| frame.sharpness_acum.is_none()));
                observed.extend(tail);
                assert_eq!(
                    observed.len(),
                    count,
                    "{rate}: every complete level aperture survives EOF"
                );
                for (slot, frame) in observed.iter().enumerate() {
                    assert_eq!(frame.end_sample, (slot + 1) as u64 * u64::from(rate / 10));
                    assert_eq!(frame.crest_db, expected[slot].crest);
                    assert_eq!(frame.psr_db, expected[slot].psr);
                }
                assert!(analyzer.finish().unwrap().is_empty());
                assert!(analyzer
                    .push_aperture(&tone(rate, channels, count))
                    .is_err());
            }
        }
    }
}
