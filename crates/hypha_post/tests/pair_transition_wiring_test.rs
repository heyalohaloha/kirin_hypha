//! Cross-shell guard for the POST name-selector transition.

use std::fs;
use std::path::PathBuf;

fn editor_source() -> String {
    let root = PathBuf::from(env!("CARGO_MANIFEST_DIR"));
    fs::read_to_string(root.join("src/editor.rs")).expect("read POST editor")
}

#[test]
fn name_change_detaches_all_exact_pair_state_before_reselection() {
    let source = editor_source();
    let start = source
        .find("fn replace_pair_pre_name(")
        .expect("transactional pair transition helper");
    let end = source[start..]
        .find("\n}\n\n")
        .map(|offset| start + offset)
        .expect("pair transition helper end");
    let body = &source[start..end];
    let compact: String = body.split_whitespace().collect();

    for required in [
        "reservation::release_pairing",
        "mark_released_with_reason",
        "exit_record_preserve_pair",
        "state.record_acknowledged.store(false",
        "state.paired_pre_target.lock()",
        "state.latched_pre.lock()",
        "state.pair_label.lock()",
        "state.delta.lock()",
        "state.pair_pre_name.write()",
    ] {
        assert!(
            compact.contains(required),
            "pair transition must contain {required}"
        );
    }

    assert_eq!(
        source.matches("replace_pair_pre_name(state,").count(),
        2,
        "typed-name and dropdown selection must share one detach transaction"
    );
}
