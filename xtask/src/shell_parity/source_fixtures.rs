    const README: &str = include_str!(concat!(env!("CARGO_MANIFEST_DIR"), "/../README.md"));
    const PLUGIN_EDITOR_CPP: &str = concat!(
        include_str!("../../../juce_shell/src/PluginEditor.cpp"),
        include_str!("../../../juce_shell/src/PluginEditorMeter.cpp"),
        include_str!("../../../juce_shell/src/PluginEditorMenu.cpp"),
        include_str!("../../../juce_shell/src/PluginEditorObservatory.cpp"),
        include_str!("../../../juce_shell/src/PluginEditorLifecycle.cpp"),
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
        // B-961: format binding lives in its own translation unit; parity still reads one shell.
        include_str!("../../../juce_shell/src/PluginProcessorFormat.cpp"),
        "\n",
        include_str!("../../../juce_shell/src/PluginProcessorState.cpp"),
        include_str!("../../../juce_shell/src/PluginProcessorService.cpp")
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
    const HYPHA_OBSERVATORY_VIEW_FOOTER_CPP: &str = include_str!(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../juce_shell/src/HyphaObservatoryViewFooter.cpp"
    ));
    const HYPHA_OBSERVATORY_METRICS_CPP: &str = include_str!(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../juce_shell/src/HyphaObservatoryMetrics.cpp"
    ));
    const HYPHA_OBSERVATORY_CAPTURE_CPP: &str = include_str!(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../juce_shell/src/HyphaObservatoryCapture.cpp"
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
        let mut source = std::fs::read_to_string(path).ok()?.replace("\r\n", "\n");
        if !source.contains("setKirinAuUsesHostTransportTimeline") {
            let patch = std::path::Path::new(env!("CARGO_MANIFEST_DIR"))
                .join("../juce_shell/patches/0004-au-clock-provenance.patch");
            let additions = std::fs::read_to_string(patch).ok()?
                .lines()
                .filter_map(|line| line.strip_prefix('+'))
                .filter(|line| !line.starts_with("++"))
                .collect::<Vec<_>>()
                .join("\n");
            source.push_str(&additions);
        }
        Some(source)
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

    fn without_cpp_comments(source: &str) -> String {
        let mut out = String::with_capacity(source.len());
        let mut chars = source.chars().peekable();
        let mut quote = None;
        while let Some(c) = chars.next() {
            if let Some(q) = quote {
                out.push(c);
                if c == '\\' { if let Some(escaped) = chars.next() { out.push(escaped); } }
                else if c == q { quote = None; }
            } else if c == '"' || c == '\'' { quote = Some(c); out.push(c); }
            else if c == '/' && chars.peek() == Some(&'/') {
                chars.next();
                for next in chars.by_ref() { if next == '\n' { out.push('\n'); break; } }
            } else if c == '/' && chars.peek() == Some(&'*') {
                chars.next();
                while let Some(next) = chars.next() {
                    if next == '*' && chars.peek() == Some(&'/') { chars.next(); break; }
                }
                out.push(' ');
            } else { out.push(c); }
        }
        out
    }
