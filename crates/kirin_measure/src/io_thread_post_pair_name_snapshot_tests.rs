use super::*;

/// 通常ケース: 設定値が snapshot として返る。
#[test]
fn normal_value_returned() {
    let arc = Arc::new(RwLock::new(String::from("PRE-Master")));
    let snap = snapshot_pair_pre_name(&arc);
    assert_eq!(snap, "PRE-Master");
}

/// 空文字 (default 状態) → 空文字 snapshot。
#[test]
fn empty_string_returned_as_empty() {
    let arc = Arc::new(RwLock::new(String::new()));
    let snap = snapshot_pair_pre_name(&arc);
    assert_eq!(snap, "");
}

/// poison error → 空文字 fallback (R-28 機能的沈黙 / 旧 schema 互換)。
#[test]
fn poisoned_lock_returns_empty_fallback() {
    let arc = Arc::new(RwLock::new(String::from("PRE-Should-Not-Be-Returned")));
    let arc_clone = Arc::clone(&arc);
    // 別 thread で write guard 保持中に panic → poison 状態化。
    let _ = std::thread::spawn(move || {
        let _guard = arc_clone.write().unwrap();
        panic!("intentional poison for test");
    })
    .join();
    // 上記 thread は join() で error を返すが poison 化は完了している。
    assert!(arc.is_poisoned(), "lock should be poisoned");
    let snap = snapshot_pair_pre_name(&arc);
    assert_eq!(snap, "", "poisoned lock must fall back to empty string");
}
