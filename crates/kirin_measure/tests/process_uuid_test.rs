//! B-020 / γ-3 chunk-persistent UUID: プロセス cell + setter の決定論的構造テスト。
//!
//! - `process_project_hash` / `daw_session_id` の cell が空のとき lazy fallback が
//!   非空文字列を返す
//! - `set_project_uuid` / `set_daw_session_id` で cell を上書きできる
//! - `peek_project_uuid` は lazy fallback なし（POST sync の判定に使う）
//! - `generate_project_hash` は呼び出しごとに異なる文字列を返す
//!
//! cell はプロセス global であり、整合性のため tests 内では `TEST_LOCK` で
//! 直列化する（cargo test の parallel runner で他テストと交錯しないように）。

use kirin_measure::{
    daw_session_id, generate_project_hash, peek_project_uuid, process_project_hash,
    set_daw_session_id, set_project_uuid,
};
use std::sync::Mutex;

static TEST_LOCK: Mutex<()> = Mutex::new(());

/// poison しても続行できるように lock 取得をラップ。
fn lock() -> std::sync::MutexGuard<'static, ()> {
    TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner())
}

#[test]
fn process_project_hash_lazy_fallback_returns_nonempty() {
    let _guard = lock();
    // cell をクリアして lazy fallback を発火させる
    set_project_uuid(String::new());
    let v = process_project_hash();
    assert!(!v.is_empty(), "lazy fallback must produce non-empty UUID");
}

#[test]
fn set_project_uuid_overrides_cell() {
    let _guard = lock();
    set_project_uuid("test_uuid_b020_a".to_string());
    assert_eq!(process_project_hash(), "test_uuid_b020_a");
    set_project_uuid("test_uuid_b020_b".to_string());
    assert_eq!(process_project_hash(), "test_uuid_b020_b");
}

#[test]
fn peek_project_uuid_returns_empty_when_cell_unset() {
    let _guard = lock();
    set_project_uuid(String::new());
    assert_eq!(
        peek_project_uuid(),
        "",
        "peek must NOT lazy-init (POST sync depends on this)"
    );
    set_project_uuid("xx_value".to_string());
    assert_eq!(peek_project_uuid(), "xx_value");
}

#[test]
fn set_daw_session_id_overrides_cell() {
    let _guard = lock();
    set_daw_session_id("session_b020_a".to_string());
    assert_eq!(daw_session_id(), "session_b020_a");
    set_daw_session_id("session_b020_b".to_string());
    assert_eq!(daw_session_id(), "session_b020_b");
}

#[test]
fn generate_project_hash_produces_unique_uuids() {
    // この関数は cell に触れないので TEST_LOCK 不要
    let a = generate_project_hash();
    let b = generate_project_hash();
    assert_ne!(a, b, "generate_project_hash must produce unique strings");
    // UUID v4 形式 (36 chars with dashes at 8/13/18/23)
    assert_eq!(a.len(), 36, "UUID v4 string length is 36");
    assert_eq!(&a[8..9], "-");
    assert_eq!(&a[13..14], "-");
    assert_eq!(&a[18..19], "-");
    assert_eq!(&a[23..24], "-");
}
