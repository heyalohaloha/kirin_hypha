use std::fs;
use std::path::PathBuf;

fn repo_root() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .parent()
        .and_then(|p| p.parent())
        .expect("crate lives under crates/")
        .to_path_buf()
}

fn read_repo(path: &str) -> String {
    let path = repo_root().join(path);
    fs::read_to_string(&path).unwrap_or_else(|e| panic!("read {}: {e}", path.display()))
}

fn slice_between<'a>(src: &'a str, start_marker: &str, end_marker: &str) -> &'a str {
    let start = src.find(start_marker).expect("start marker must exist");
    let end = src[start..]
        .find(end_marker)
        .map(|idx| start + idx)
        .expect("end marker must follow start marker");
    &src[start..end]
}

#[test]
fn juce_prepare_does_not_destroy_engine_while_recording() {
    let src = read_repo("juce_shell/src/PluginProcessor.cpp");
    let body = slice_between(
        &src,
        "void KirinHyphaProcessorBase::prepareToPlay",
        "void KirinHyphaProcessorBase::releaseResources",
    );

    let guard = body
        .find("kirin_hypha_is_recording (hyphaHandle)")
        .expect("prepareToPlay must guard Record before engine rebuild");
    let destroy = body
        .find("kirin_hypha_destroy (hyphaHandle)")
        .expect("prepareToPlay contains the incompatible rebuild destroy path");
    assert!(
        guard < destroy,
        "Record guard must run before kirin_hypha_destroy in prepareToPlay"
    );

    let guarded_return = body[guard..destroy].contains("return;");
    assert!(
        guarded_return,
        "Record guard must return before the incompatible rebuild can destroy the engine"
    );
}

#[test]
fn egui_pre_initialize_does_not_shutdown_threads_while_recording() {
    let src = read_repo("crates/hypha_pre/src/lib.rs");
    let body = slice_between(&src, "fn initialize(", "fn reset(");

    let guard = body
        .find("self.record_sm.is_recording()")
        .expect("PRE initialize must guard Record before thread shutdown");
    let shutdown = body
        .find("self.watchdog_shutdown.store(true")
        .expect("PRE initialize contains the watchdog shutdown path");
    assert!(
        guard < shutdown,
        "PRE Record guard must run before initialize() can stop threads"
    );

    let guarded_return = body[guard..shutdown].contains("return true;");
    assert!(
        guarded_return,
        "PRE Record guard must return before initialize() can stop threads"
    );
}

#[test]
fn egui_post_initialize_does_not_shutdown_threads_while_recording() {
    let src = read_repo("crates/hypha_post/src/lib.rs");
    let body = slice_between(&src, "fn initialize(", "fn reset(");

    let guard = body
        .find("self.record_sm.is_recording()")
        .expect("POST initialize must guard Record before thread shutdown");
    let shutdown = body
        .find("self.watchdog_shutdown.store(true")
        .expect("POST initialize contains the watchdog shutdown path");
    assert!(
        guard < shutdown,
        "POST Record guard must run before initialize() can stop threads"
    );

    let guarded_return = body[guard..shutdown].contains("return true;");
    assert!(
        guarded_return,
        "POST Record guard must return before initialize() can stop threads"
    );
}

#[test]
fn io_thread_shutdown_paths_mark_lifecycle_shutdown() {
    for (path, start_marker) in [
        (
            "crates/kirin_measure/src/io_thread_pre.rs",
            "if let Some(mut ctx) = writer_ctx.take()",
        ),
        (
            "crates/kirin_measure/src/io_thread_post.rs",
            "if let Some(mut ctx) = recording.take()",
        ),
    ] {
        let src = read_repo(path);
        let start = src
            .find(start_marker)
            .expect("shutdown-during-record writer context must exist");
        let close_marker = "writer_close_with_summary(ctx, summary);";
        let close_end = src[start..]
            .find(close_marker)
            .map(|idx| start + idx + close_marker.len())
            .expect("shutdown path must close the writer");
        let body = &src[start..close_end];

        assert!(
            body.contains("ctx.writer.add_integrity_reason(\"lifecycle_shutdown\")"),
            "{path} must tag shutdown-during-record artifacts"
        );
        let reason = body
            .find("ctx.writer.add_integrity_reason(\"lifecycle_shutdown\")")
            .expect("reason call exists");
        let close = body
            .find("writer_close_with_summary(ctx, summary);")
            .expect("writer close exists");
        assert!(
            reason < close,
            "{path} must tag lifecycle shutdown before closing the writer"
        );
    }
}
