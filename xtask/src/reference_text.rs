//! Kirin OS names reach Hypha already trimmed by Kirin OS's own set (JavaScript `\s` plus U+0085,
//! U+180E and U+200B). The receiver checks them with that same set (`ReferenceTextEdges.h`), never
//! with JUCE's `trim()`, which on Windows reads only the low 16 bits of a character and so takes a
//! kanji such as U+2000B for a space, rejecting a whole library over one valid Preset or CHECK name.

#[cfg(test)]
mod tests {
    use std::fs;
    use std::path::Path;

    #[test]
    fn the_reference_receiver_checks_names_without_juce_trim() {
        let directory =
            Path::new(env!("CARGO_MANIFEST_DIR")).join("../juce_shell/src/reference_audition");
        let mut checked = 0;
        for entry in fs::read_dir(&directory).expect("the reference receiver sources") {
            let path = entry.expect("a receiver source").path();
            let name = path
                .file_name()
                .and_then(|name| name.to_str())
                .unwrap_or_default()
                .to_owned();
            if !(name.ends_with(".cpp") || name.ends_with(".h")) {
                continue;
            }
            let source = fs::read_to_string(&path).expect("a readable receiver source");
            for call in [".trim()", ".trimEnd()", ".trimStart()"] {
                assert!(
                    !source.contains(call),
                    "{name} calls {call}; use hypha::reference_text (ReferenceTextEdges.h) for Kirin OS names"
                );
            }
            checked += 1;
        }
        assert!(checked >= 40, "the receiver sources were found ({checked})");
    }
}
