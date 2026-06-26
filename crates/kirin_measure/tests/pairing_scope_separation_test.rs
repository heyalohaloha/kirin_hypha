//! pairing_scope が IO Thread 実装へ依存しないことを固定する。

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
fn pairing_scope_uses_post_candidates_not_io_thread_post() {
    let src = read("src/pairing_scope.rs");
    assert!(
        src.contains("crate::post_candidates::scan_post_candidates_in"),
        "pairing_scope must read POST candidates through post_candidates"
    );
    for forbidden in [
        "crate::io_thread_post::scan_post_candidates_in",
        "io_thread_post::scan_post_candidates_in",
        "use crate::io_thread_post",
    ] {
        assert!(
            !src.contains(forbidden),
            "pairing_scope must not depend on IO Thread facade `{forbidden}`"
        );
    }
}
