//! runtime 側の selector 検証（承認事項 3A / D-5）。
//!
//! 値空間そのものは `channel_layout::spectrum_view` が持つ。ここで見るのは
//! 「runtime が layout を実際に持っていて、選んだ役割を**実際にその役割として測るか**」である。
//!
//! B-970 以前は単一チャンネル view を受理しなかった（解析経路が役割で入力を選ばなかったため）。
//! いまは worker が入力チャンネル数ぶん pop して役割の位置を取るので、受理して測る。

use super::*;
use crate::channel_layout::{ChannelLayout, ChannelRole, LayoutId};

#[test]
fn a_view_the_layout_does_not_offer_is_refused() {
    let runtime = SpectrumRuntime::new(48_000, ChannelLayout::stereo());
    assert_eq!(runtime.view(), Some(SpectrumView::Lr));

    // stereo が持つもの。
    assert!(runtime.set_view(SpectrumView::Mid));
    assert_eq!(runtime.view(), Some(SpectrumView::Mid));

    // stereo が持たないもの。**拒否して、選択はそのまま。** 近い view へ丸めない。
    assert!(!runtime.set_view(SpectrumView::Channel(ChannelRole::Centre)));
    assert!(!runtime.set_view(SpectrumView::Channel(ChannelRole::Lfe)));
    assert_eq!(runtime.view(), Some(SpectrumView::Mid));
}

use std::time::{Duration, Instant};

/// `view` を選んで `amplitudes` の 1 kHz 正弦を流し、最初のフレームの最大 dBFS を返す。
///
/// 期待値は振幅比から算術で出す。**製品の出力から期待値を作らない**（試験規律 §9.1）。
fn peak_dbfs_for(layout: ChannelLayout, view: SpectrumView, amplitudes: &[f32]) -> f32 {
    let channels = layout.channel_count();
    assert_eq!(amplitudes.len(), channels, "1 チャンネルにつき 1 振幅");
    let runtime = SpectrumRuntime::new(48_000, layout);
    assert!(runtime.set_view(view), "{:?} must be accepted", view);
    assert!(runtime.set_enabled(true), "{:?} must enable", layout.id());

    let mut position = 0_i64;
    let deadline = Instant::now() + Duration::from_secs(5);
    let mut peak = None;
    while Instant::now() < deadline && peak.is_none() {
        let count = 2_048usize;
        let mut samples = Vec::with_capacity(count * channels);
        for index in 0..count {
            let phase =
                std::f32::consts::TAU * 1_000.0 * (position as usize + index) as f32 / 48_000.0;
            for amplitude in amplitudes {
                samples.push(amplitude * phase.sin());
            }
        }
        runtime.push_block_from_audio(&samples, channels, Some(position));
        position += count as i64;
        std::thread::sleep(Duration::from_millis(5));
        peak = runtime
            .try_history()
            .and_then(|history| history.newest().cloned())
            .map(|frame| {
                // フレーム自身がどの観測対象で作られたかを名乗る（B-971）。
                // `channel_mode` は単一チャンネル view でも `Lr` のままなので名札にしない。
                assert_eq!(frame.view, view.to_abi(), "frame は選んだ view を名乗る");
                frame.dbfs.iter().copied().fold(f32::NEG_INFINITY, f32::max)
            });
    }
    let stats = runtime.stats();
    assert_eq!(stats.dropped_blocks, 0, "block を落とさない");
    assert!(stats.analyzed_frames > 0, "解析が進んでいる");
    runtime.shutdown_and_join();
    peak.expect("worker did not publish a frame")
}

/// 振幅比 4 倍 = 12.0412 dB。**この期待値は製品ではなく選んだ振幅から出ている。**
const RATIO_4X_DB: f32 = 12.041_2;

#[test]
fn a_single_channel_view_measures_that_channel_not_the_first_one() {
    // 5.1。L に 0.5、C に 0.125、残りは 0.01。選んだ役割の実レベルが出ることを差で確かめる。
    // 位置ではなく役割で指しているので、L が slot 0、C が slot 2 という並びに依存しない。
    let layout = ChannelLayout::by_id(LayoutId::Surround5_1);
    let amplitudes: Vec<f32> = layout
        .roles()
        .iter()
        .map(|role| match role {
            ChannelRole::Left => 0.5,
            ChannelRole::Centre => 0.125,
            _ => 0.01,
        })
        .collect();

    let left = peak_dbfs_for(
        layout,
        SpectrumView::Channel(ChannelRole::Left),
        &amplitudes,
    );
    let centre = peak_dbfs_for(
        layout,
        SpectrumView::Channel(ChannelRole::Centre),
        &amplitudes,
    );
    assert!(
        (left - centre - RATIO_4X_DB).abs() < 0.2,
        "L({left}) と C({centre}) の差は振幅比どおり {RATIO_4X_DB} dB のはず"
    );
}

#[test]
fn a_single_channel_view_works_on_the_shipping_stereo_layout_too() {
    // 出荷中の stereo でも役割 view は成立する。L に 0.5、R に 0.125。
    let layout = ChannelLayout::stereo();
    let amplitudes = [0.5f32, 0.125];
    let left = peak_dbfs_for(
        layout,
        SpectrumView::Channel(ChannelRole::Left),
        &amplitudes,
    );
    let right = peak_dbfs_for(
        layout,
        SpectrumView::Channel(ChannelRole::Right),
        &amplitudes,
    );
    assert!(
        (left - right - RATIO_4X_DB).abs() < 0.2,
        "L({left}) と R({right}) の差は振幅比どおり {RATIO_4X_DB} dB のはず"
    );
}

/// 稼働中に view を切り替える。**解析器が見る本数が 2 → 1 に変わるので組み直しが要る。**
/// 組み直さないと frame の `channels` が現在の view と食い違い、鮮度判定で全部落ちて
/// 無言になる（値が出ないだけで、原因はどこにも出ない）。
#[test]
fn changing_the_view_while_running_rebuilds_the_analysis_instead_of_going_silent() {
    let amplitudes = [0.5f32, 0.125];
    let expected_left = peak_dbfs_for(
        ChannelLayout::stereo(),
        SpectrumView::Channel(ChannelRole::Left),
        &amplitudes,
    );

    let runtime = SpectrumRuntime::new(48_000, ChannelLayout::stereo());
    assert_eq!(runtime.view(), Some(SpectrumView::Lr));
    assert_eq!(runtime.analysis_channels(), 2);
    assert!(runtime.set_enabled(true));

    let mut position = 0_i64;
    let feed = |position: &mut i64| {
        let count = 2_048usize;
        let mut samples = Vec::with_capacity(count * 2);
        for index in 0..count {
            let phase =
                std::f32::consts::TAU * 1_000.0 * (*position as usize + index) as f32 / 48_000.0;
            for amplitude in amplitudes {
                samples.push(amplitude * phase.sin());
            }
        }
        runtime.push_block_from_audio(&samples, 2, Some(*position));
        *position += count as i64;
        std::thread::sleep(Duration::from_millis(5));
    };

    // LR のまま 1 フレーム出るまで回す。
    let deadline = Instant::now() + Duration::from_secs(5);
    while Instant::now() < deadline && runtime.stats().analyzed_frames == 0 {
        feed(&mut position);
    }
    let before = runtime.stats().analyzed_frames;
    assert!(before > 0, "LR でフレームが出ている");

    // 稼働中に 1 本の view へ切り替える。
    assert!(runtime.set_view(SpectrumView::Channel(ChannelRole::Right)));
    assert_eq!(runtime.analysis_channels(), 1);

    let deadline = Instant::now() + Duration::from_secs(5);
    let mut peak = None;
    while Instant::now() < deadline && peak.is_none() {
        feed(&mut position);
        peak = runtime
            .try_history()
            .and_then(|history| history.newest().cloned())
            .map(|frame| frame.dbfs.iter().copied().fold(f32::NEG_INFINITY, f32::max));
    }
    let peak = peak.expect("切り替え後にフレームが出ない = 無言になっている");
    assert!(
        runtime.stats().analyzed_frames > before,
        "切り替え後も解析が進んでいる"
    );
    assert!(
        (expected_left - peak - RATIO_4X_DB).abs() < 0.2,
        "切り替え後は R を測っているはず（L {expected_left} / 実測 {peak}）"
    );
    runtime.shutdown_and_join();
}

#[test]
fn a_wider_layout_is_carried_as_it_is_instead_of_being_clamped_to_two() {
    // B-963 以前は clamp(1, 2) で 2ch として記録され、以後の block がすべて拒否されて
    // Spectrum が無言で何も出さなかった。実チャンネル数を持てば、測れることが現れる。
    let layout = ChannelLayout::by_id(LayoutId::Surround5_1);
    let runtime = SpectrumRuntime::new(48_000, layout);
    assert_eq!(runtime.num_channels(), 6);
    assert_eq!(runtime.layout().id(), LayoutId::Surround5_1);

    // 5.1 は導出 view を提供しないので、既定はバッファ先頭の役割になる。
    assert_eq!(
        runtime.view(),
        Some(SpectrumView::Channel(layout.roles()[0]))
    );
    // 解析器が見るのは 1 本。**入力の 6 本と別の量である。**
    assert_eq!(runtime.analysis_channels(), 1);
    for view in SpectrumView::available_in(layout) {
        assert!(runtime.set_view(view), "{:?} must be accepted", view);
        assert_eq!(runtime.view(), Some(view));
    }
    // 提供しない導出 view は引き続き拒否する。
    assert!(!runtime.set_view(SpectrumView::Lr));
    assert!(!runtime.set_view(SpectrumView::Mid));
    assert!(!runtime.set_view(SpectrumView::Side));
}

#[test]
fn a_surround_layout_analyses_instead_of_jamming() {
    // B-965 の状態: 5.1 は enable を拒否されていた。worker の de-interleave が
    // `num_channels == 2` のときだけ 2 本 pop していたため、受理すると ring が詰まったからである。
    //
    // **判定は「全部流し切ったあと ring が空になるか」にする**（B-974）。
    // 1 フレームにつき入力チャンネル数ぶん pop していなければ、読み残しが ring に溜まる。
    // 溜まっていれば ring 容量ちょうどの block は入らない。
    //
    // 一度「一定時間内に drop が出ない」で書いて false failure を出し、次に
    // 「各 chunk が最終的に受理される」で書いて**変異を検出できなくなった**
    // （読み残しがあっても retry すれば通ってしまう）。どちらも時間を条件にしたのが誤りで、
    // **空になるかどうかは時間ではなく残量の問題**である。
    let layout = ChannelLayout::by_id(LayoutId::Surround5_1);
    let runtime = SpectrumRuntime::new(48_000, layout);
    assert!(runtime.set_enabled(true), "5.1 must enable");

    // ring は `aperture_samples * 2 * channels` サンプル = その 1/6 のフレーム数。
    let ring_frames = crate::SPECTRUM_WINDOW_SIZE * 2;
    let chunk_frames = 256usize;
    let chunk: Vec<f32> = (0..chunk_frames * 6)
        .map(|i| ((i % 6) as f32 + 1.0) * 0.1)
        .collect();
    let mut position = 0_i64;
    for _ in 0..(ring_frames * 2 / chunk_frames) {
        // 詰まっていれば入らない。ここでは通し切ることだけが目的なので待つ。
        let deadline = Instant::now() + Duration::from_secs(10);
        while Instant::now() < deadline && !runtime.push_block_from_audio(&chunk, 6, Some(position))
        {
            std::thread::sleep(Duration::from_millis(1));
        }
        position += chunk_frames as i64;
    }

    // **ring 容量ちょうどの block が入る = 読み残しがゼロ。** 残量の判定であって時間の判定ではない。
    // 待ち時間は hang を切るためだけにある。詰まっていれば何秒待っても空かない。
    // （settle を「analyzed_frames が 50 ms 変わらない」で判定していた版は、全体試験の
    //   並列実行下で worker が 50 ms 以上止まると誤検出した。B-976 で残量の retry に置き換え。）
    let full: Vec<f32> = vec![0.1; ring_frames * 6];
    let deadline = Instant::now() + Duration::from_secs(10);
    let mut drained = false;
    while Instant::now() < deadline {
        if runtime.push_block_from_audio(&full, 6, Some(position)) {
            drained = true;
            break;
        }
        std::thread::sleep(Duration::from_millis(5));
    }
    let stats = runtime.stats();
    assert!(
        drained,
        "引き切ったあとの ring は空のはず（pushed {} / dropped {}）",
        stats.pushed_blocks, stats.dropped_blocks
    );
    assert!(stats.pushed_blocks > 0, "block が入っている");

    // 解析が進んでいることは別に待つ。ring が空になることと、frame が組み上がることは別の事象で、
    // 後者は窓が満ちるまで起きない。
    let deadline = Instant::now() + Duration::from_secs(10);
    while Instant::now() < deadline && runtime.stats().analyzed_frames == 0 {
        std::thread::sleep(Duration::from_millis(5));
    }
    assert!(runtime.stats().analyzed_frames > 0, "解析が進んでいる");
    runtime.shutdown_and_join();
}

#[test]
fn mid_side_does_not_stay_on_over_a_view_that_cannot_carry_it() {
    let runtime = SpectrumRuntime::new(48_000, ChannelLayout::stereo());
    assert!(runtime.set_mid_side_enabled(true), "LR なら有効にできる");
    assert!(runtime.mid_side_enabled());

    // 1 本の view へ移ると自動で降りる。**有効なまま残さない。**
    assert!(runtime.set_view(SpectrumView::Channel(ChannelRole::Right)));
    assert!(
        !runtime.mid_side_enabled(),
        "1 本の view では Mid/Side は成立しない"
    );

    // その状態からは有効にできない。
    assert!(!runtime.set_mid_side_enabled(true));
    assert!(!runtime.mid_side_enabled());

    // 2 本へ戻せば また有効にできる。
    assert!(runtime.set_view(SpectrumView::Lr));
    assert!(runtime.set_mid_side_enabled(true));
    assert!(runtime.mid_side_enabled());
}

#[test]
fn mono_and_stereo_still_enable() {
    // 現行の対応範囲を巻き込んでいないこと。mono は 1ch の pop が正しい。
    for layout in [ChannelLayout::mono(), ChannelLayout::stereo()] {
        let runtime = SpectrumRuntime::new(48_000, layout);
        assert!(runtime.set_enabled(true), "{:?}", layout.id().as_str());
        runtime.shutdown_and_join();
    }
}

#[test]
fn stale_generation_channel_mode_or_view_can_never_be_republished() {
    let runtime = SpectrumRuntime::new(48_000, crate::channel_layout::ChannelLayout::stereo());
    assert!(runtime.set_enabled(true));
    let generation = runtime.selection().generation;
    let frame = SpectrumFrame {
        schema_version: crate::SPECTRUM_SCHEMA_VERSION,
        sample_rate: 48_000,
        aperture_samples: crate::SPECTRUM_WINDOW_SIZE as u32,
        fft_size: crate::SPECTRUM_FFT_SIZE as u32,
        band_count: crate::SPECTRUM_BAND_COUNT as u16,
        presentation_end_samples: 4_800,
        generation,
        channel_mode: SpectrumChannelMode::Lr,
        view: crate::channel_layout::SpectrumView::Lr.to_abi(),
        channels: 2,
        min_hz: 10.0,
        max_hz: 22_000.0,
        dbfs: [-24.0; crate::SPECTRUM_BAND_COUNT],
    };
    let stream = runtime.stream_generation.load(Ordering::Acquire);
    assert!(runtime.frame_is_current(&frame, stream));

    assert!(runtime.set_channel_mode(SpectrumChannelMode::Mid));
    assert!(!runtime.frame_is_current(&frame, stream));
    let mut current = frame.clone();
    current.generation = runtime.selection().generation;
    current.channel_mode = SpectrumChannelMode::Mid;
    // B-974: 名札も合わせないと通らない。`channel_mode` だけを直して view を旧いまま
    // 残した frame は、generation が現在でも公開されない。
    assert!(
        !runtime.frame_is_current(&current, stream),
        "channel_mode だけ直して view が旧い frame は公開しない"
    );
    current.view = crate::channel_layout::SpectrumView::Mid.to_abi();
    assert!(runtime.frame_is_current(&current, stream));

    // 役割 view でも同じ。単一チャンネル view は `channel_mode` が `Lr` のままなので、
    // **名札を見ないと L/R の frame と区別できない。**
    assert!(
        runtime.set_view(crate::channel_layout::SpectrumView::Channel(
            crate::channel_layout::ChannelRole::Right
        ))
    );
    let mut role = current.clone();
    role.generation = runtime.selection().generation;
    role.channels = 1;
    role.channel_mode = SpectrumChannelMode::Lr;
    assert!(
        !runtime.frame_is_current(&role, stream),
        "view が MID のままの frame は R の観測として公開しない"
    );
    role.view =
        crate::channel_layout::SpectrumView::Channel(crate::channel_layout::ChannelRole::Right)
            .to_abi();
    assert!(runtime.frame_is_current(&role, stream));

    assert!(runtime.set_enabled(false));
    assert!(!runtime.frame_is_current(&role, stream));
    runtime.shutdown_and_join();
}
