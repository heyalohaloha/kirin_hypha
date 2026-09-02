use super::*;

fn snap(lufs: f64) -> DeltaSnapshot {
    DeltaSnapshot {
        lufs: Some(lufs),
        lufs_s: None,
        psr: None,
        tp: None,
        n_prime_total: None,
        crest: None,
        sharpness: None,
    }
}

/// B-059: NoPre（select_target_pre が None）→ **last_active クリア**（B-048 の保持を廃止）。
/// 「見えないのに直近 Δ が凍結表示される」のを防ぐ＝表示=commit 一本化の核。
#[test]
fn nopre_clears_last_active() {
    let prev = Some(snap(1.0));
    let nopre = DeltaResult {
        mode: DeltaMode::NoPre,
        ..Default::default()
    };
    let r = resolve_delta_for_store(nopre, prev);
    assert_eq!(r.mode, DeltaMode::NoPre);
    assert!(
        r.last_active.is_none(),
        "NoPre は last_active をクリア（凍結 Δ を残さない）"
    );
}

/// Stale（一意有効 pair の 5-10s）→ 前回 last_active 保持（B-048 維持）。
#[test]
fn stale_keeps_last_active() {
    let prev = Some(snap(2.0));
    let stale = DeltaResult {
        mode: DeltaMode::Stale,
        ..Default::default()
    };
    let r = resolve_delta_for_store(stale, prev);
    assert!(
        r.last_active.is_some(),
        "Stale は同一有効 pair の last_active を保持"
    );
    assert_eq!(r.last_active.unwrap().lufs, Some(2.0));
}

/// Active → 新 snapshot 保存。
#[test]
fn active_stores_snapshot() {
    let active = DeltaResult {
        mode: DeltaMode::Active,
        lufs: Some(3.0),
        ..Default::default()
    };
    let r = resolve_delta_for_store(active, None);
    assert!(r.last_active.is_some(), "Active は新 snapshot を保存");
    assert_eq!(r.last_active.unwrap().lufs, Some(3.0));
}
