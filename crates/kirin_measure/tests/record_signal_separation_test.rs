//! record_signal が pairing / candidate の facade に戻らないことを固定する。
//!
//! `record_signal.rs` は POST→PRE signal JSON の読み書きだけを所有する。PRE 候補列挙や
//! target selection を re-export すると、呼び出し側が再び record_signal 経由へ寄り、
//! AU/VST3 差・候補メニュー・Keep 確定側の境界が曖昧になる。

use std::fs;
use std::path::PathBuf;

fn crate_root() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR"))
}

fn read(rel: &str) -> String {
    let path = crate_root().join(rel);
    fs::read_to_string(&path).unwrap_or_else(|e| panic!("read {rel}: {e}"))
}

#[test]
fn record_signal_does_not_reexport_pairing_or_pre_candidates() {
    let src = read("src/record_signal.rs");
    for forbidden in [
        "pub use crate::pairing_scope",
        "pub use crate::pre_candidates",
        "pub fn discover_pre_dirs_for_post_project",
        "pub fn enumerate_active_pre_pair_candidates_for_post_project",
    ] {
        assert!(
            !src.contains(forbidden),
            "record_signal.rs must not expose pairing/candidate facade `{forbidden}`"
        );
    }
}

#[test]
fn public_root_exports_pairing_and_candidates_directly() {
    let src = read("src/lib.rs");
    for required in [
        "pub use pairing_scope::",
        "pub use pre_candidates::",
        "select_target_pre_for_arm_for_post_project",
        "enumerate_active_pre_pair_candidates_for_post_project",
    ] {
        assert!(
            src.contains(required),
            "crate root must expose `{required}` directly without record_signal facade"
        );
    }
}
