//! Source-level R-12 gate for the three Audio Thread entry points that own the Measure ring.
//!
//! Runtime restart tests prove recovery. This cheap gate separately prevents a future recovery
//! refactor from putting locks, allocation, deallocation, logging, or filesystem I/O back into an
//! audio callback.

fn section<'a>(source: &'a str, start: &str, end: &str) -> &'a str {
    let start = source.find(start).expect("start marker must exist");
    let end = source[start..]
        .find(end)
        .map(|offset| start + offset)
        .expect("end marker must exist");
    &source[start..end]
}

fn assert_rt_safe_source(label: &str, source: &str) {
    for forbidden in [
        ".lock(",
        ".try_lock(",
        "Box::",
        "Vec::",
        "vec![",
        "String::",
        "format!",
        ".to_string()",
        ".to_owned()",
        "Arc::new",
        "Mutex::new",
        "drop(",
        "log::",
        "println!",
        "eprintln!",
        "std::fs",
        "File::",
    ] {
        assert!(
            !source.contains(forbidden),
            "{label} must not contain Audio Thread operation {forbidden}"
        );
    }
}

#[test]
fn ffi_and_legacy_audio_entries_keep_restart_handoff_rt_safe() {
    let ffi = include_str!("../src/lib.rs");
    assert_rt_safe_source(
        "FFI push_samples",
        section(
            ffi,
            "pub fn push_samples(&self",
            "/// 最新の RT 計測結果を取得",
        ),
    );

    let pre = include_str!("../../hypha_pre/src/lib.rs");
    assert_rt_safe_source(
        "legacy PRE process",
        section(pre, "    fn process(\n", "        ProcessStatus::Normal"),
    );

    let post = include_str!("../../hypha_post/src/lib.rs");
    assert_rt_safe_source(
        "legacy POST process",
        section(post, "    fn process(\n", "        ProcessStatus::Normal"),
    );

    let handoff = include_str!("../../kirin_measure/src/watchdog_handoff.rs");
    assert_rt_safe_source(
        "Watch producer adoption",
        section(
            handoff,
            "pub unsafe fn swap_pending_from_audio",
            "/// 現 active Producer",
        ),
    );
    assert_rt_safe_source(
        "Watch producer access",
        section(
            handoff,
            "pub unsafe fn with_active_producer_from_audio",
            "/// 現 active Producer の空き",
        ),
    );

    let record = include_str!("../../kirin_measure/src/record_ingress.rs");
    assert_rt_safe_source(
        "Record producer adoption",
        section(
            record,
            "pub unsafe fn adopt_from_audio",
            "/// Copy one Record callback",
        ),
    );
    assert_rt_safe_source(
        "Record producer push",
        section(
            record,
            "pub unsafe fn push_from_audio",
            "/// Run one bounded copy operation",
        ),
    );
    assert_rt_safe_source(
        "Record producer access",
        section(
            record,
            "pub unsafe fn with_producer_from_audio",
            "/// Publish the global capture-clock frame",
        ),
    );
}
