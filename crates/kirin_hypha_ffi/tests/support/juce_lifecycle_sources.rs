use std::{fs, path::PathBuf};

pub fn read_repo(path: &str) -> String {
    let root = PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .parent()
        .and_then(|p| p.parent())
        .expect("crate lives under crates/")
        .to_path_buf();
    let read = |relative: &str| {
        let path = root.join(relative);
        fs::read_to_string(&path).unwrap_or_else(|e| panic!("read {}: {e}", path.display()))
    };
    let mut source = read(path);
    // Keep assertions on the actual common shell implementation after responsibility
    // extraction. Individual tests still name their primary translation unit.
    let extracted: &[&str] = match path {
        "juce_shell/src/PluginEditor.cpp" => &[
            "juce_shell/src/PluginEditorMeter.cpp",
            "juce_shell/src/PluginEditorMenu.cpp",
            "juce_shell/src/PluginEditorInformation.cpp",
        ],
        "juce_shell/src/PluginProcessor.cpp" => &[
            "juce_shell/src/PluginProcessorState.cpp",
            "juce_shell/src/PluginProcessorAnalysis.cpp",
            "juce_shell/src/PluginProcessorMeter.cpp",
            "juce_shell/src/PluginProcessorSignal.cpp",
        ],
        _ => &[],
    };
    for relative in extracted {
        source.push('\n');
        source.push_str(&read(relative));
    }
    source
}
