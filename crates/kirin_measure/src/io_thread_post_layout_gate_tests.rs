//! live Δ の比較可否（B-976 / Gate A1）。
//!
//! **ファイルを読めることと、その測定値どうしを比較してよいことは別である。**
//! `pre.json` の `v` は 2 のままで旧 POST も読める（transport 互換）。比べてよいかは
//! ここで判定する（measurement compatibility）。

use super::*;
use crate::channel_layout::ChannelLayout;
use crate::plugin_data::MeasurementLayout;
use std::sync::atomic::AtomicU64;

fn isolated_dir(label: &str) -> PathBuf {
    static COUNTER: AtomicU64 = AtomicU64::new(0);
    let n = COUNTER.fetch_add(1, Ordering::Relaxed);
    let dir = std::env::temp_dir()
        .join(format!(
            "kirin_layout_gate_{label}_{}_{n}",
            std::process::id()
        ))
        .join("ph");
    let _ = fs::remove_dir_all(&dir);
    fs::create_dir_all(&dir).unwrap();
    dir
}

/// PRE を 1 本書く。`layout` が `None` なら旧版の PRE（配置を名乗らない）。
fn write_pre(project_dir: &Path, layout: Option<ChannelLayout>) -> PathBuf {
    write_pre_aged(project_dir, layout, 0)
}

/// `age_secs` 秒前の時刻で PRE を書く。鮮度と配置の優先順位を見るため。
fn write_pre_aged(project_dir: &Path, layout: Option<ChannelLayout>, age_secs: i64) -> PathBuf {
    let dir = project_dir.join("pre-iid");
    fs::create_dir_all(&dir).unwrap();
    let t = (chrono::Utc::now() - chrono::Duration::seconds(age_secs))
        .format("%Y-%m-%dT%H:%M:%S%.3fZ");
    let fragment = layout.map_or(String::new(), |layout| {
        format!(
            r#","layout":{}"#,
            serde_json::to_string(&MeasurementLayout::new(layout)).unwrap()
        )
    });
    let json = format!(
        r#"{{"v":2,"role":"PRE","instance_id":"pre-iid","signal_state":"active","t":"{t}","lufs_m":-14.0,"lufs_s":-15.0,"true_peak":-1.0,"crest":12.0,"psr":8.0{fragment}}}"#
    );
    let path = dir.join("pre.json");
    fs::write(&path, json).unwrap();
    path
}

fn post() -> MeasureResult {
    MeasureResult {
        lufs_m: Some(-10.0),
        lufs_s: Some(-11.0),
        true_peak: Some(-1.0),
        crest: Some(12.0),
        psr: Some(8.0),
        ..MeasureResult::default()
    }
}

fn delta_for(pre_layout: Option<ChannelLayout>, post_layout: ChannelLayout) -> DeltaResult {
    let dir = isolated_dir("pair");
    let pre_json = write_pre(&dir, pre_layout);
    let (delta, _) = compute_delta_for_pre_file(&pre_json, &post(), &MeasurementLayout::new(post_layout))
        .unwrap();
    delta
}

#[test]
fn the_same_layout_on_both_sides_still_compares() {
    for layout in [ChannelLayout::mono(), ChannelLayout::stereo()] {
        let delta = delta_for(Some(layout), layout);
        assert_eq!(
            delta.mode,
            DeltaMode::Active,
            "{} 同士は従来どおり比較する",
            layout.id().as_str()
        );
        assert_eq!(delta.lufs, Some(4.0));
        assert_eq!(delta.lufs_s, Some(4.0));
    }
}

/// 同じ音を通しても mono と stereo では loudness が 3.01 LU ずれる。その差は連鎖が
/// 加えたものではない。**出荷中の mono / stereo だけで起きる**（bus はどちらも受理される）。
#[test]
fn a_different_layout_is_not_subtracted_in_either_direction() {
    for (pre, post_layout) in [
        (ChannelLayout::mono(), ChannelLayout::stereo()),
        (ChannelLayout::stereo(), ChannelLayout::mono()),
    ] {
        let delta = delta_for(Some(pre), post_layout);
        assert_eq!(
            delta.mode,
            DeltaMode::LayoutMismatch,
            "{} PRE と {} POST は引き算しない",
            pre.id().as_str(),
            post_layout.id().as_str()
        );
        assert_eq!(delta.lufs, None);
        assert_eq!(delta.lufs_s, None);
    }
}

/// 旧版の PRE は配置を名乗らない。**compatible だと確認できない。**
/// ここで従来どおり Δ を出すと、unknown を compatible とみなすことになる。
#[test]
fn an_unstated_layout_is_not_treated_as_compatible() {
    for layout in [ChannelLayout::mono(), ChannelLayout::stereo()] {
        let delta = delta_for(None, layout);
        assert_eq!(delta.mode, DeltaMode::LayoutUnknown);
        assert_eq!(delta.lufs, None);
    }
}

/// 名乗ってはいるが読めない `layout` も unknown として扱う。**近い配置へ丸めない。**
#[test]
fn a_layout_field_that_cannot_be_read_is_unknown_not_rounded() {
    let dir = isolated_dir("garbage");
    let pre_dir = dir.join("pre-iid");
    fs::create_dir_all(&pre_dir).unwrap();
    let t = chrono::Utc::now().format("%Y-%m-%dT%H:%M:%S%.3fZ");
    fs::write(
        pre_dir.join("pre.json"),
        format!(
            r#"{{"v":2,"role":"PRE","instance_id":"pre-iid","signal_state":"active","t":"{t}","lufs_m":-14.0,"true_peak":-1.0,"crest":12.0,"psr":8.0,"layout":"stereo"}}"#
        ),
    )
    .unwrap();
    let (delta, _) = compute_delta_for_pre_file(
        &pre_dir.join("pre.json"),
        &post(),
        &MeasurementLayout::new(ChannelLayout::stereo()),
    )
    .unwrap();
    assert_eq!(delta.mode, DeltaMode::LayoutUnknown);
}

/// 配置が違っても **transport は壊れない**。JSON は読め、PRE は候補として見つかる。
/// 失われるのは Δ だけで、ペアリングそのものではない。
#[test]
fn a_layout_mismatch_does_not_break_transport_or_pairing() {
    let dir = isolated_dir("transport");
    let pre_json = write_pre(&dir, Some(ChannelLayout::mono()));
    let text = fs::read_to_string(&pre_json).unwrap();
    let parsed: serde_json::Value = serde_json::from_str(&text).unwrap();
    assert_eq!(parsed["v"], 2, "v は据え置き。旧 POST も読める");
    assert_eq!(parsed["instance_id"], "pre-iid", "ペアリングの鍵は読める");
    assert_eq!(parsed["signal_state"], "active");

    let (delta, signal) = compute_delta_for_pre_file(
        &pre_json,
        &post(),
        &MeasurementLayout::new(ChannelLayout::stereo()),
    )
    .unwrap();
    assert_eq!(delta.mode, DeltaMode::LayoutMismatch);
    assert_eq!(
        signal,
        Some(SignalState::Active),
        "PRE が生きていることは伝わる"
    );
}

/// 拒否したときに凍結値を残さない。**配置が変わったなら、前の Δ はもう何も指していない。**
#[test]
fn a_rejected_comparison_does_not_keep_the_frozen_value() {
    let previous = DeltaSnapshot {
        lufs: Some(1.0),
        lufs_s: None,
        psr: None,
        tp: None,
        n_prime_total: None,
        crest: None,
        sharpness: None,
        psb_bark: None,
    };
    for mode in [DeltaMode::LayoutMismatch, DeltaMode::LayoutUnknown] {
        let merged = merge_last_active(
            Some(previous.clone()),
            DeltaResult {
                mode: mode.clone(),
                ..Default::default()
            },
        );
        assert_eq!(merged.mode, mode);
        assert!(merged.last_active.is_none(), "{mode:?} は凍結値を残さない");
    }
}

/// **不在が不一致に優先する。** とうに消えた PRE に「配置が違う」と言わない。
///
/// 理由を UI へ出すのは Gate D だが、**出す前に理由が正しい順序で決まっていなければ
/// 意味がない。** B-976 は配置を鮮度より先に見ており、10 秒以上前の pre.json に対しても
/// `LayoutMismatch` を返していた（B-978 で直した）。
#[test]
fn an_absent_pre_outranks_an_incompatible_one() {
    // 10 秒より古い = 実質いない。配置が違っても `NoPre` である。
    let dir = isolated_dir("stale");
    let pre_json = write_pre_aged(&dir, Some(ChannelLayout::mono()), 30);
    let (delta, _) = compute_delta_for_pre_file(
        &pre_json,
        &post(),
        &MeasurementLayout::new(ChannelLayout::stereo()),
    )
    .unwrap();
    assert_eq!(
        delta.mode,
        DeltaMode::NoPre,
        "消えた PRE に配置の話をしない"
    );

    // 5〜10 秒（Stale）は同じペアの続きなので、不一致の方が行動可能な理由である。
    let dir = isolated_dir("aged");
    let pre_json = write_pre_aged(&dir, Some(ChannelLayout::mono()), 7);
    let (delta, _) = compute_delta_for_pre_file(
        &pre_json,
        &post(),
        &MeasurementLayout::new(ChannelLayout::stereo()),
    )
    .unwrap();
    assert_eq!(delta.mode, DeltaMode::LayoutMismatch);

    // 同じ配置なら Stale のまま（従来どおり）。
    let dir = isolated_dir("aged-same");
    let pre_json = write_pre_aged(&dir, Some(ChannelLayout::stereo()), 7);
    let (delta, _) = compute_delta_for_pre_file(
        &pre_json,
        &post(),
        &MeasurementLayout::new(ChannelLayout::stereo()),
    )
    .unwrap();
    assert_eq!(delta.mode, DeltaMode::Stale);
}
