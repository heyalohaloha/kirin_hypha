#[cfg(test)]
mod tests {
    use std::collections::BTreeSet;

    const FFI_LIB_RS: &str = include_str!(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../crates/kirin_hypha_ffi/src/lib.rs"
    ));
    const PLUGIN_PROCESSOR_CPP: &str = include_str!(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../juce_shell/src/PluginProcessor.cpp"
    ));
    const RECORD_TAKE_RS: &str = include_str!(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../crates/kirin_measure/src/record_take.rs"
    ));

    fn strip_line_comments(source: &str) -> String {
        source
            .lines()
            .map(|line| line.split_once("//").map_or(line, |(code, _)| code))
            .collect::<Vec<_>>()
            .join("\n")
    }

    fn function_body(source: &str, signature: &str) -> String {
        let source = strip_line_comments(source);
        let start = source.find(signature).expect(signature);
        let after = &source[start..];
        let open = after.find('{').expect(signature);
        let mut depth = 0usize;
        for (idx, ch) in after[open..].char_indices() {
            match ch {
                '{' => depth += 1,
                '}' => {
                    depth -= 1;
                    if depth == 0 {
                        return after[open + 1..open + idx].to_string();
                    }
                }
                _ => {}
            }
        }
        panic!("function body not closed: {signature}");
    }

    fn ffi_calls(body: &str) -> BTreeSet<String> {
        let mut calls = BTreeSet::new();
        let mut rest = body;
        while let Some(pos) = rest.find("kirin_hypha_") {
            let after = &rest[pos..];
            let end = after
                .find(|c: char| !(c.is_ascii_alphanumeric() || c == '_'))
                .unwrap_or(after.len());
            calls.insert(after[..end].to_string());
            rest = &after[end..];
        }
        calls
    }

    #[test]
    fn process_block_calls_only_rt_safe_ffi_surface() {
        let body = function_body(
            PLUGIN_PROCESSOR_CPP,
            "void KirinHyphaProcessorBase::processBlock",
        );
        let calls = ffi_calls(&body);
        let expected = BTreeSet::from([
            "kirin_hypha_get_signal_state".to_string(),
            "kirin_hypha_is_recording".to_string(),
            "kirin_hypha_note_capture_window".to_string(),
            "kirin_hypha_note_transport_block".to_string(),
            "kirin_hypha_note_record_window".to_string(),
            "kirin_hypha_note_oversized_drop".to_string(),
            "kirin_hypha_push_samples".to_string(),
            "kirin_hypha_set_signal_state".to_string(),
        ]);

        assert_eq!(calls, expected);
    }

    #[test]
    fn process_block_keeps_audio_thread_free_of_blocking_work() {
        let body = function_body(
            PLUGIN_PROCESSOR_CPP,
            "void KirinHyphaProcessorBase::processBlock",
        );

        for forbidden in [
            "juce::ScopedLock",
            "handleLock",
            "enableWritesNow",
            "kirin_hypha_create",
            "kirin_hypha_destroy",
            "kirin_hypha_enable_",
            "kirin_hypha_set_identity",
            "kirin_hypha_load_license",
            ".assign",
            ".resize",
            "new ",
            "delete ",
            "malloc",
            "calloc",
            "realloc",
            "free(",
            "juce::File",
            "std::filesystem",
            "std::ifstream",
            "std::ofstream",
            "std::thread",
            "sleep",
            "wait",
            "triggerAsyncUpdate",
        ] {
            assert!(
                !body.contains(forbidden),
                "processBlock must not contain {forbidden}"
            );
        }

        assert!(body.contains("getReadPointer"));
        assert!(!body.contains("getWritePointer"));
    }

    #[test]
    fn ffi_push_samples_core_avoids_io_allocation_and_blocking_locks() {
        let body = function_body(
            FFI_LIB_RS,
            "fn push_samples_transaction(&self, interleaved: &[f32], num_channels: u32)",
        );

        for forbidden in [
            "StoragePaths",
            "std::fs",
            "read_to_string",
            "write(",
            "File::",
            ".lock(",
            ".try_lock(",
            "Vec::",
            "Box::",
            "String::",
            "format!",
            "thread::",
            "spawn",
            "sleep",
            "join",
        ] {
            assert!(
                !body.contains(forbidden),
                "push_samples core must not contain {forbidden}"
            );
        }

        assert!(body.contains("heartbeat.fetch_add"));
        assert!(body.contains("producer_handoff.swap_pending_from_audio"));
        assert!(body.contains("record_ingress.adopt_from_audio"));
        assert!(body.contains(".with_producer_from_audio("));
        assert!(body.contains(".with_active_producer_from_audio("));
        assert!(body.contains("producer.push"));
    }

    #[test]
    fn capture_clock_core_avoids_io_allocation_and_blocking_locks() {
        let body = function_body(
            RECORD_TAKE_RS,
            "pub fn note_capture_window_with_presentation_boundary(\n        &self,",
        );

        for forbidden in [
            "StoragePaths",
            "std::fs",
            "read_to_string",
            "write(",
            "File::",
            ".lock(",
            ".try_lock(",
            "Vec::",
            "Box::",
            "String::",
            "format!",
            "thread::",
            "spawn",
            "sleep",
            "join",
        ] {
            assert!(
                !body.contains(forbidden),
                "capture clock core must not contain {forbidden}"
            );
        }

        assert!(body.contains("capture_frames_total"));
        assert!(body.contains(".fetch_add(num_frames"));
        assert!(body.contains("capture_clock_slots[index].extend"));
        assert!(body.contains("capture_clock_slots[index].publish"));
    }

    #[test]
    fn audio_thread_c_abi_wrappers_remain_thin() {
        for signature in [
            "pub unsafe extern \"C\" fn kirin_hypha_get_signal_state",
            "pub unsafe extern \"C\" fn kirin_hypha_push_samples",
            "pub unsafe extern \"C\" fn kirin_hypha_note_record_block",
            "pub unsafe extern \"C\" fn kirin_hypha_note_record_window",
            "pub unsafe extern \"C\" fn kirin_hypha_note_capture_window",
            "pub unsafe extern \"C\" fn kirin_hypha_note_transport_block",
            "pub unsafe extern \"C\" fn kirin_hypha_note_oversized_drop",
        ] {
            let body = function_body(FFI_LIB_RS, signature);
            for forbidden in [
                "StoragePaths",
                "std::fs",
                "read_to_string",
                "write(",
                "File::",
                ".lock(",
                ".try_lock(",
                "Vec::",
                "Box::",
                "String::",
                "format!",
                "thread::",
                "spawn",
                "sleep",
                "join",
            ] {
                assert!(
                    !body.contains(forbidden),
                    "{signature} must not contain {forbidden}"
                );
            }
        }
    }
}
