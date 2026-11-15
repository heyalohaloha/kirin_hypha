// A worker may outlive the host's static destruction sequence.
#[test]
fn reference_worker_validation_has_no_static_regex_lifetime() {
    let directory = std::path::Path::new(env!("CARGO_MANIFEST_DIR"))
        .join("../juce_shell/src/reference_audition");
    let mut scanned = 0;
    for entry in std::fs::read_dir(directory).expect("reference worker sources") {
        let path = entry.expect("source entry").path();
        if !matches!(
            path.extension().and_then(|ext| ext.to_str()),
            Some("cpp" | "h")
        ) {
            continue;
        }
        scanned += 1;
        let source = std::fs::read_to_string(&path).expect("source text");
        assert!(
            !source.contains("static const std::regex") && !source.contains("static std::regex"),
            "{} retains a destructible regex",
            path.display()
        );
    }
    assert!(scanned > 0, "no reference worker sources were scanned");
}
