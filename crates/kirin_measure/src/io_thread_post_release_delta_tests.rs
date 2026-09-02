use super::*;

/// (C-1) IO Thread C-4 release block の `*delta_result.lock() = DeltaResult::default()`
/// パターンが last_active を None に / mode を NoPre に / Δ 6 field を None に
/// する事を verify。spawn_io_thread_post 経由のフル統合は既存 timing flake test
/// (test_pair_pre_name_arc_roundtrip_to_post_json) と同経路のため、本 unit test
/// では A-1 の mutation pattern 単体を直接検証する (gui_wiring C-3 invariant 検査と
/// 組合せで release 経路全体カバー)。
#[test]
fn release_clears_delta_result_when_self_check_releases() {
    // 解放前: last_active=Some / mode=Active / 6 field=Some (W-281 release 直前状態)。
    let delta = Arc::new(Mutex::new(DeltaResult {
        lufs: Some(-2.0),
        lufs_s: Some(-1.0),
        psr: Some(1.5),
        tp: Some(-0.5),
        n_prime_total: Some(0.1),
        crest: Some(3.0),
        sharpness: Some(0.8),
        mode: DeltaMode::Active,
        last_active: Some(DeltaSnapshot {
            lufs: Some(-2.0),
            lufs_s: Some(-1.0),
            psr: Some(1.5),
            tp: Some(-0.5),
            n_prime_total: Some(0.1),
            crest: Some(3.0),
            sharpness: Some(0.8),
        }),
    }));

    // A-1 release mutation (W-282 io_thread_post.rs C-4 release block と同一 pattern)。
    if let Ok(mut d) = delta.lock() {
        *d = DeltaResult::default();
    }

    // 解放後: last_active=None / mode=NoPre / 6 field=None。
    let r = delta.lock().unwrap();
    assert!(
        r.last_active.is_none(),
        "release must clear last_active (B-048 LKG bypass)"
    );
    assert_eq!(
        r.mode,
        DeltaMode::NoPre,
        "release must reset mode to NoPre (Default)"
    );
    assert!(r.lufs.is_none());
    assert!(r.lufs_s.is_none());
    assert!(r.psr.is_none());
    assert!(r.tp.is_none());
    assert!(r.n_prime_total.is_none());
    assert!(r.crest.is_none());
    assert!(r.sharpness.is_none());
}

/// (C-2) R-9 補足 1 検証: release 直後の同 tick run_tick 再走で
/// `compute_delta_with_state` が NoPre を返した場合、`merge_last_active(prev=None,
/// new=NoPre)` で last_active=None が維持される (= 復活しない)。
#[test]
fn merge_last_active_after_release_keeps_none() {
    // release 直後の状態: prev_last_active = None (A-1 で reset 済)。
    let prev_last_active: Option<DeltaSnapshot> = None;
    // 同 tick 再走の compute_delta_with_state が返す new_delta (instance 2+ 環境 /
    // pair filter で 0 件 → NoPre)。
    let new_delta = DeltaResult {
        mode: DeltaMode::NoPre,
        ..Default::default()
    };

    let merged = merge_last_active(prev_last_active, new_delta);

    // last_active=None 維持 / Active 経路を通っていないため復活しない。
    assert!(
        merged.last_active.is_none(),
        "merge with prev=None + NoPre must keep last_active=None"
    );
    assert_eq!(merged.mode, DeltaMode::NoPre);
}
