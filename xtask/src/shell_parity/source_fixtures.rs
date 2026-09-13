    const POST_CONTROLS_CPP: &str = include_str!(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../juce_shell/src/PostControls.cpp"
    ));
    const POST_CONTROLS_H: &str = include_str!(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../juce_shell/src/PostControls.h"
    ));
    const README: &str = include_str!(concat!(env!("CARGO_MANIFEST_DIR"), "/../README.md"));
    const PLUGIN_EDITOR_CPP: &str = concat!(
        include_str!("../../../juce_shell/src/PluginEditor.cpp"),
        include_str!("../../../juce_shell/src/PluginEditorMeter.cpp"),
        include_str!("../../../juce_shell/src/PluginEditorMenu.cpp"),
    );
    const PLUGIN_EDITOR_H: &str = include_str!(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../juce_shell/src/PluginEditor.h"
    ));
    const PLUGIN_EDITOR_OBSERVATORY_CPP: &str = include_str!(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../juce_shell/src/PluginEditorObservatory.cpp"
    ));
    const PLUGIN_EDITOR_CAPTURE_CPP: &str = include_str!(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../juce_shell/src/PluginEditorCapture.cpp"
    ));
    const PLUGIN_PROCESSOR_CPP: &str = concat!(
        include_str!("../../../juce_shell/src/PluginProcessor.cpp"),
        "\n",
        include_str!("../../../juce_shell/src/PluginProcessorState.cpp")
    );
    const PLUGIN_PROCESSOR_PAIRING_CPP: &str =
        include_str!("../../../juce_shell/src/PluginProcessorPairing.cpp");
    const PLUGIN_PROCESSOR_METER_CPP: &str = include_str!("../../../juce_shell/src/PluginProcessorMeter.cpp");
    const PLUGIN_PROCESSOR_CLOCK_CPP: &str = include_str!("../../../juce_shell/src/PluginProcessorHostClock.cpp");
    const WATCH_DISPLAY_FFI_RS: &str = include_str!("../../../crates/kirin_hypha_ffi/src/watch_display_ffi.rs");
    const PLUGIN_PROCESSOR_H: &str = include_str!(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../juce_shell/src/PluginProcessor.h"
    ));
    const JUCE_PLUGIN_CONFIG: &str = include_str!(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../juce_shell/src/KirinJucePluginConfig.h"
    ));
    const JUCE_CMAKE: &str = include_str!(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../juce_shell/CMakeLists.txt"
    ));
    const FFI_HEADER: &str = include_str!(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../crates/kirin_hypha_ffi/include/kirin_hypha_ffi.h"
    ));
    const HYPHA_THEME_H: &str = include_str!(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../juce_shell/src/HyphaTheme.h"
    ));
    const HYPHA_TYPOGRAPHY_CPP: &str = include_str!(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../juce_shell/src/HyphaTypography.cpp"
    ));
    const HYPHA_UI_CONTRACT_H: &str = include_str!(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../juce_shell/src/HyphaUiContract.h"
    ));
    const HYPHA_OBSERVATORY_VIEW_H: &str = include_str!(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../juce_shell/src/HyphaObservatoryView.h"
    ));
    const HYPHA_DISPLAY_CONTRACT_H: &str = include_str!(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../juce_shell/src/HyphaDisplayContract.h"
    ));
    const PRE_DISPLAY_CONTROLLER_CPP: &str = include_str!(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../juce_shell/src/pre_display/PreDisplayController.cpp"
    ));
    const PRE_DISPLAY_CONTROLLER_CONNECTION_CPP: &str = include_str!(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../juce_shell/src/pre_display/PreDisplayControllerConnection.cpp"
    ));
    const PRE_DISPLAY_REPOSITORY_CPP: &str = include_str!(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../juce_shell/src/pre_display/PreDisplayRepository.cpp"
    ));
    const PRE_DISPLAY_PROTOCOL_CPP: &str = include_str!(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../juce_shell/src/pre_display/PreDisplayProtocol.cpp"
    ));
    const PRE_DISPLAY_PROTOCOL_TIME_CPP: &str = include_str!(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../juce_shell/src/pre_display/PreDisplayProtocolTime.cpp"
    ));
    const PRE_DISPLAY_TRANSPORT_CPP: &str = include_str!(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../juce_shell/src/pre_display/PreDisplayTransport.cpp"
    ));

    fn read_juce_au_wrapper() -> Option<String> {
        let path = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join(
            "../juce_shell/JUCE/modules/juce_audio_plugin_client/juce_audio_plugin_client_AU_1.mm",
        );
        std::fs::read_to_string(path)
            .ok()
            .map(|source| source.replace("\r\n", "\n"))
    }
    fn between<'a>(source: &'a str, start: &str, end: &str) -> &'a str {
        let start_index = source.find(start).expect(start) + start.len();
        let tail = &source[start_index..];
        let end_index = tail.find(end).expect(end);
        &tail[..end_index]
    }
    fn count_occurrences(source: &str, needle: &str) -> usize {
        source.match_indices(needle).count()
    }

    // Function scope, independent of the next method's location after a responsibility extraction.
    fn cpp_body<'a>(source: &'a str, signature: &str) -> &'a str {
        let tail = &source[source.find(signature).expect(signature)..];
        let open = tail.find('{').expect(signature);
        let mut depth = 0usize;
        for (index, c) in tail[open..].char_indices() {
            match c {
                '{' => depth += 1,
                '}' => {
                    depth -= 1;
                    if depth == 0 { return &tail[open + 1..open + index]; }
                }
                _ => {}
            }
        }
        panic!("function body not closed: {signature}");
    }
