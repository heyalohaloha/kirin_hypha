use std::fs;
use std::path::PathBuf;

fn repo_root() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .parent()
        .and_then(|path| path.parent())
        .expect("crate lives below the repository root")
        .to_path_buf()
}

fn read_repo(path: &str) -> String {
    let full_path = repo_root().join(path);
    fs::read_to_string(&full_path)
        .unwrap_or_else(|error| panic!("read {}: {error}", full_path.display()))
}

fn slice_between<'a>(source: &'a str, start: &str, end: &str) -> &'a str {
    let start_index = source.find(start).expect("start marker");
    let end_index = source[start_index..]
        .find(end)
        .map(|offset| start_index + offset)
        .expect("end marker");
    &source[start_index..end_index]
}

#[test]
fn vst_audio_callback_reaches_the_internal_default_off_attack_lane() {
    let processor = read_repo("juce_shell/src/PluginProcessor.cpp");
    let callback = slice_between(
        &processor,
        "void KirinHyphaProcessorBase::processBlock",
        "bool KirinHyphaProcessorBase::bufferIsSilent",
    );
    let clock = callback
        .find("kirin_hypha_note_capture_window")
        .expect("VST callback must stage an exact clock");
    let audio = callback
        .find("kirin_hypha_push_samples")
        .expect("VST callback must submit the same interleaved audio");
    assert!(clock < audio);

    let ffi = read_repo("crates/kirin_hypha_ffi/src/lib.rs");
    let transaction = slice_between(
        &ffi,
        "fn push_samples_transaction",
        "pub fn note_oversized_drop",
    );
    assert!(transaction.contains("self.attack_runtime.as_ref()"));
    assert!(transaction.contains("runtime.push_block_from_audio("));
    assert!(transaction.contains("spectrum_presentation_start"));

    let runtime = read_repo("crates/kirin_measure/src/attack_runtime.rs");
    let ingress = slice_between(
        &runtime,
        "pub fn push_block_from_audio",
        "pub fn try_history",
    );
    let gate = ingress
        .find("if !self.enabled.load")
        .expect("ATTACK ingress must begin with its atomic OFF gate");
    let producer = ingress
        .find("self.sample_producer.get()")
        .expect("enabled ATTACK uses its dedicated bounded producer");
    assert!(gate < producer);
}

#[test]
fn attack_abi_is_internal_and_does_not_create_a_public_navigation_route() {
    let header = read_repo("crates/kirin_hypha_ffi/include/kirin_hypha_ffi.h");
    for required in [
        "KIRIN_ATTACK_BATCH_CAPACITY 64u",
        "KirinAttackOdfFrame",
        "KirinAttackBatch",
        "KirinAttackStats",
        "kirin_hypha_set_internal_attack_enabled",
        "kirin_hypha_poll_internal_attack_batch",
        "kirin_hypha_internal_attack_stats",
    ] {
        assert!(header.contains(required), "ATTACK ABI missing {required}");
    }

    let editor = read_repo("juce_shell/src/PluginEditor.cpp");
    let processor = read_repo("juce_shell/src/PluginProcessor.cpp");
    assert!(!editor.contains("set_internal_attack"));
    assert!(!processor.contains("set_internal_attack"));
}
