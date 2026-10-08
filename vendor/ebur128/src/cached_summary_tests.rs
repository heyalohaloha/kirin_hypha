use super::*;
use crate::history::History;
use std::time::Instant;

fn near(actual: f64, expected: f64) {
    if actual.is_finite() || expected.is_finite() {
        assert!((actual - expected).abs() < 1e-9, "{actual} != {expected}");
    } else {
        assert_eq!(actual.is_nan(), expected.is_nan());
        assert_eq!(actual.is_infinite(), expected.is_infinite());
    }
}

#[test]
fn exact_energy_gates_duplicates_and_lra_quantiles_match_scalar_history() {
    for values in [
        [0.1 * (1.0 - 1e-12), 1.9],
        [0.1, 1.9],
        [0.1 * (1.0 + 1e-12), 1.9],
        [0.01 * (1.0 - 1e-12), 1.99],
        [0.01, 1.99],
        [0.01 * (1.0 + 1e-12), 1.99],
    ] {
        let mut cache = Energies::default();
        let mut scalar = History::new(false, usize::MAX);
        for value in values {
            cache.add(value);
            scalar.add(value);
        }
        near(cache.integrated(), scalar.gated_loudness());
        near(
            cache.range_with_canonical(|| scalar.loudness_range()),
            scalar.loudness_range(),
        );
    }
    let mut cache = Energies::default();
    let mut scalar = History::new(false, usize::MAX);
    let absolute = crate::histogram_bins::BOUNDARIES[0];
    // Unquantized values straddle the absolute gate and exercise a moving relative gate.
    let special = [
        0.0,
        absolute * (1.0 - 1e-12),
        absolute,
        absolute * (1.0 + 1e-12),
        0.7,
        7.0,
        70.0,
    ];
    for index in 0..36_000 {
        let energy = if index < special.len() {
            special[index]
        } else if index % 11 == 0 {
            0.7
        } else {
            10f64.powf(-6.0 + (index * 7919 % 36_000) as f64 / 5000.0)
        };
        cache.add(energy);
        scalar.add(energy);
        if index < 16 || index % 1000 == 999 {
            near(cache.integrated(), scalar.gated_loudness());
            near(
                cache.range_with_canonical(|| scalar.loudness_range()),
                scalar.loudness_range(),
            );
        }
    }
    for energy in [f64::NAN, f64::INFINITY] {
        let mut cache = Energies::default();
        let mut scalar = History::new(false, usize::MAX);
        for value in [0.1, energy] {
            cache.add(value);
            scalar.add(value);
        }
        near(cache.integrated(), scalar.gated_loudness());
        near(
            cache.range_with_canonical(|| scalar.loudness_range()),
            scalar.loudness_range(),
        );
    }
}

#[test]
fn roundoff_sensitive_lra_gates_use_canonical_result_once_per_unchanged_history() {
    let mut repeated = vec![0.8920892089208943];
    repeated.extend([1.9999999999999645; 10]);
    repeated.extend([100.00000000000026; 89]);
    for values in [
        vec![
            0.7259995684040955,
            0.14445309236673196,
            0.0031418242611905506,
            0.3831352194442023,
        ],
        repeated,
    ] {
        let mut meter = EbuR128::new(2, 48_000, Mode::I | Mode::LRA).unwrap();
        meter.enable_cached_summary_queries().unwrap();
        for value in values {
            meter.short_term_block_energy_history.add(value);
            meter.summary_cache.as_mut().unwrap().range.add(value);
        }
        let cache = &meter.summary_cache.as_ref().unwrap().range;
        assert!(
            cache.range_fast().is_none(),
            "sensitive relative gate must use canonical sorting"
        );
        let canonical = meter.loudness_range().unwrap();
        let fallbacks = std::cell::Cell::new(0);
        let result = cache.range_with_canonical(|| {
            fallbacks.set(fallbacks.get() + 1);
            meter.loudness_range().unwrap()
        });
        assert_eq!(result.to_bits(), canonical.to_bits());
        for _ in 0..1000 {
            assert_eq!(
                cache
                    .range_with_canonical(|| {
                        fallbacks.set(fallbacks.get() + 1);
                        meter.loudness_range().unwrap()
                    })
                    .to_bits(),
                canonical.to_bits()
            );
            assert_eq!(
                meter.loudness_range_cached().unwrap().to_bits(),
                canonical.to_bits()
            );
        }
        assert_eq!(
            fallbacks.get(),
            1,
            "100 ms reads must reuse the unchanged history's fallback"
        );
        // A finite accepted energy invalidates the old result even before the next UI read.
        meter.short_term_block_energy_history.add(0.02);
        meter.summary_cache.as_mut().unwrap().range.add(0.02);
        assert_eq!(
            meter.loudness_range_cached().unwrap().to_bits(),
            meter.loudness_range().unwrap().to_bits()
        );
        // Invalid data follows canonical NaN and must not reuse a previously finite result.
        meter.short_term_block_energy_history.add(f64::NAN);
        meter.summary_cache.as_mut().unwrap().range.add(f64::NAN);
        assert!(meter.loudness_range_cached().unwrap().is_nan());
        assert!(meter.loudness_range().unwrap().is_nan());
        meter.reset();
        assert_eq!(meter.loudness_range_cached().unwrap(), 0.0);
    }
}

#[test]
fn concurrent_immutable_range_readers_share_one_history_result() {
    let mut meter = EbuR128::new(2, 48_000, Mode::I | Mode::LRA).unwrap();
    meter.enable_cached_summary_queries().unwrap();
    for value in [
        0.7259995684040955,
        0.14445309236673196,
        0.0031418242611905506,
        0.3831352194442023,
    ] {
        meter.short_term_block_energy_history.add(value);
        meter.summary_cache.as_mut().unwrap().range.add(value);
    }
    let expected = meter.loudness_range().unwrap().to_bits();
    let meter = std::sync::Arc::new(meter);
    let start = std::sync::Arc::new(std::sync::Barrier::new(8));
    let handles: Vec<_> = (0..8)
        .map(|_| {
            let meter = std::sync::Arc::clone(&meter);
            let start = std::sync::Arc::clone(&start);
            std::thread::spawn(move || {
                start.wait();
                for _ in 0..1000 {
                    assert_eq!(meter.loudness_range_cached().unwrap().to_bits(), expected);
                }
            })
        })
        .collect();
    for handle in handles {
        handle.join().unwrap();
    }
}

#[test]
fn finite_overflow_without_qualifying_integrated_blocks_matches_canonical() {
    let mut cache = Energies::default();
    let mut scalar = History::new(false, usize::MAX);
    for value in [f64::MAX; 2] {
        cache.add(value);
        scalar.add(value);
    }
    assert_eq!(cache.integrated(), f64::NEG_INFINITY);
    assert_eq!(cache.integrated(), scalar.gated_loudness());
    near(
        cache.range_with_canonical(|| scalar.loudness_range()),
        scalar.loudness_range(),
    );
}

#[test]
fn audio_cache_matches_original_queries_across_partial_tail_reset_and_reconfiguration() {
    for rate in [44_117, 48_000] {
        let mode = Mode::I | Mode::LRA | Mode::TRUE_PEAK;
        let mut meter = EbuR128::new(2, rate, mode).unwrap();
        meter.enable_cached_summary_queries().unwrap();
        for block in 0..121 {
            let frames = ((rate + 5) / 10) as usize;
            let gain = [0.002, 0.08, 0.6][block / 17 % 3];
            let input: Vec<_> = (0..frames)
                .flat_map(|frame| {
                    let sample = gain
                        * (std::f64::consts::TAU * 997.0 * (block * frames + frame) as f64
                            / rate as f64)
                            .sin();
                    [sample, sample * 0.51]
                })
                .collect();
            for part in input.chunks(64) {
                meter.add_frames_f64(part).unwrap();
            }
            near(
                meter.loudness_global_cached().unwrap(),
                meter.loudness_global().unwrap(),
            );
            near(
                meter.loudness_range_cached().unwrap(),
                meter.loudness_range().unwrap(),
            );
        }
        meter.add_frames_f64(&[0.9; 18]).unwrap();
        near(
            meter.loudness_global_cached().unwrap(),
            meter.loudness_global().unwrap(),
        );
        near(
            meter.loudness_range_cached().unwrap(),
            meter.loudness_range().unwrap(),
        );
        meter.change_parameters(1, rate + 1).unwrap();
        meter.add_frames_f64(&[0.1; 20_000]).unwrap();
        near(
            meter.loudness_global_cached().unwrap(),
            meter.loudness_global().unwrap(),
        );
        near(
            meter.loudness_range_cached().unwrap(),
            meter.loudness_range().unwrap(),
        );
        meter.reset();
        near(
            meter.loudness_global_cached().unwrap(),
            meter.loudness_global().unwrap(),
        );
        near(
            meter.loudness_range_cached().unwrap(),
            meter.loudness_range().unwrap(),
        );
    }
}

#[test]
fn unsupported_histogram_and_bounded_history_do_not_enable_exact_cache() {
    let mut histogram = EbuR128::new(2, 48_000, Mode::I | Mode::LRA | Mode::HISTOGRAM).unwrap();
    assert!(histogram.enable_cached_summary_queries().is_err());
    let mut bounded = EbuR128::new(2, 48_000, Mode::I | Mode::LRA).unwrap();
    bounded.set_max_history(10_000).unwrap();
    assert!(bounded.enable_cached_summary_queries().is_err());
    let mut late = EbuR128::new(2, 48_000, Mode::I | Mode::LRA).unwrap();
    late.add_frames_f64(&[0.1; 1024]).unwrap();
    assert!(late.enable_cached_summary_queries().is_err());
}

fn add_raw(meter: &mut EbuR128, energy: f64, integrated: bool) {
    if integrated {
        meter.block_energy_history.add(energy);
        meter.summary_cache.as_mut().unwrap().add_integrated(energy);
    } else {
        meter.short_term_block_energy_history.add(energy);
        meter.summary_cache.as_mut().unwrap().add_range(energy);
    }
}

#[test]
fn shared_node_cap_falls_back_exactly_reuses_unchanged_history_and_reset_rearms() {
    let mut meter = EbuR128::new(2, 48_000, Mode::I | Mode::LRA).unwrap();
    meter.enable_cached_summary_queries().unwrap();
    meter.summary_cache.as_mut().unwrap().node_limit = 3;
    for (energy, integrated) in [(0.1, true), (0.2, true), (0.3, false)] {
        add_raw(&mut meter, energy, integrated);
    }
    for _ in 0..100 {
        add_raw(&mut meter, 0.1, true); // Exact duplicate consumes no additional node.
        add_raw(&mut meter, 0.0, false); // Rejected absolute-gate energy consumes no node.
    }
    let cache = meter.summary_cache.as_ref().unwrap();
    assert_eq!(cache.nodes, 3);
    assert!(!cache.capped);
    add_raw(&mut meter, 0.4, false);
    let cache = meter.summary_cache.as_ref().unwrap();
    assert!(cache.capped);
    assert_eq!(cache.nodes, 0);
    assert!(cache.integrated.root.is_none() && cache.range.root.is_none());
    let reads = std::cell::Cell::new(0);
    for _ in 0..1000 {
        assert_eq!(
            cache
                .integrated
                .canonical_query(|| {
                    reads.set(reads.get() + 1);
                    meter.loudness_global()
                })
                .unwrap()
                .to_bits(),
            meter.loudness_global().unwrap().to_bits()
        );
        assert_eq!(
            meter.loudness_global_cached().unwrap().to_bits(),
            meter.loudness_global().unwrap().to_bits()
        );
        assert_eq!(
            meter.loudness_range_cached().unwrap().to_bits(),
            meter.loudness_range().unwrap().to_bits()
        );
    }
    assert_eq!(
        reads.get(),
        1,
        "unchanged I history must not rescan per small input push"
    );
    add_raw(&mut meter, 7.0, true);
    add_raw(&mut meter, 10.0, false);
    assert_eq!(
        meter.loudness_global_cached().unwrap().to_bits(),
        meter.loudness_global().unwrap().to_bits()
    );
    assert_eq!(
        meter.loudness_range_cached().unwrap().to_bits(),
        meter.loudness_range().unwrap().to_bits()
    );
    assert_eq!(meter.summary_cache.as_ref().unwrap().nodes, 0);
    meter.reset();
    let cache = meter.summary_cache.as_ref().unwrap();
    assert!(!cache.capped);
    assert_eq!(cache.node_limit, SUMMARY_CACHE_MAX_NODES);
    add_raw(&mut meter, 0.2, true);
    assert_eq!(meter.summary_cache.as_ref().unwrap().nodes, 1);
    near(
        meter.loudness_global_cached().unwrap(),
        meter.loudness_global().unwrap(),
    );
}

#[test]
fn actual_default_node_budget_never_grows_the_auxiliary_index_past_65536_nodes() {
    let mut meter = EbuR128::new(2, 48_000, Mode::I | Mode::LRA).unwrap();
    meter.enable_cached_summary_queries().unwrap();
    for index in 0..SUMMARY_CACHE_MAX_NODES {
        add_raw(&mut meter, 0.001 + index as f64 / 100_000.0, true);
    }
    let cache = meter.summary_cache.as_ref().unwrap();
    assert_eq!(cache.nodes, 65_536);
    assert!(!cache.capped);
    near(
        meter.loudness_global_cached().unwrap(),
        meter.loudness_global().unwrap(),
    );
    add_raw(
        &mut meter,
        0.001 + SUMMARY_CACHE_MAX_NODES as f64 / 100_000.0,
        true,
    );
    assert!(meter.summary_cache.as_ref().unwrap().capped);
    assert_eq!(meter.summary_cache.as_ref().unwrap().nodes, 0);
    assert_eq!(
        meter.loudness_global_cached().unwrap().to_bits(),
        meter.loudness_global().unwrap().to_bits()
    );
}

// Tree-algorithm scaling benchmark; the product switches to canonical queries at its node cap.
// The long fixture intentionally exercises the unbounded tree directly, outside that policy.
#[test]
#[ignore]
fn repeated_exact_summary_queries_remain_bounded_as_duration_grows() {
    let mut times = Vec::new();
    for count in [600, 360_000] {
        let mut cache = Energies::default();
        for index in 0..count {
            cache.add(0.001 + (index * 7919 % count) as f64 / count as f64);
        }
        let started = Instant::now();
        assert!(
            cache.range_fast().is_some(),
            "normal fixture must exercise the fast range path"
        );
        for _ in 0..100_000 {
            let cache = std::hint::black_box(&cache);
            std::hint::black_box((
                cache.integrated(),
                cache.range_with_canonical(|| panic!("normal fixture must not sort")),
            ));
        }
        let elapsed = started.elapsed();
        assert!(height(&cache.root) <= 2 * (count as f64).log2().ceil() as u32);
        fn nodes(node: &Link) -> usize {
            node.as_ref()
                .map_or(0, |node| 1 + nodes(&node.left) + nodes(&node.right))
        }
        let distinct = nodes(&cache.root);
        assert_eq!(distinct, count);
        println!("{count} energies: 100000 I/LRA queries {:?}, tree height {}, {distinct} distinct nodes, Node {} bytes, tree payload {} bytes (allocator overhead and canonical histories excluded)", elapsed, height(&cache.root), std::mem::size_of::<Node>(), distinct * std::mem::size_of::<Node>());
        times.push(elapsed.as_secs_f64());
    }
    // 600x more history must not cause duration-proportional query cost; broad scheduling margin.
    assert!(times[1] < times[0] * 10.0, "duration scaling {times:?}");
}
