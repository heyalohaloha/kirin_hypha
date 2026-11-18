//! Bounded source contract for optional analysis ingress, not a whole-program RT proof.
use super::tests::function_body;

const INGRESS: &str = include_str!("../../crates/kirin_measure/src/spectrum_runtime_ingress.rs");
const RUNTIME: &str = include_str!("../../crates/kirin_measure/src/spectrum_runtime.rs");
const SELECTION: &str = include_str!("../../crates/kirin_measure/src/analysis_selection.rs");

const SURFACE: &[(&str, &str)] = &[
    (INGRESS, "pub fn push_block_from_audio("),
    (INGRESS, "pub fn push_block_from_audio_with_clock("),
    (INGRESS, "fn coordinate_and_definition("),
    (INGRESS, "pub(super) fn definition_is_aligned("),
    (INGRESS, "fn advance_stream_generation("),
    (INGRESS, "pub fn note_analysis_clock_drop_from_audio("),
    (INGRESS, "pub(super) fn note_drop("),
    (RUNTIME, "pub fn analysis_mode("),
    (SELECTION, "pub(super) fn selection("),
    (SELECTION, "pub fn decode("),
    (SELECTION, "pub fn is_valid_for("),
];

// Whitespace normalization also rejects `.lock (` and `Vec :: new (`.
// Like the neighboring source contracts this is a bounded lexical guard;
// aliases, macros, and transitive dependency implementations require review.
fn forbidden_operation(body: &str) -> Option<&'static str> {
    let compact: String = body.chars().filter(|c| !c.is_whitespace()).collect();
    [
        "Vec::",
        "vec!",
        "Box::",
        "String::",
        "format!",
        ".to_vec(",
        ".to_owned(",
        ".to_string(",
        ".collect",
        ".reserve(",
        ".resize(",
        "Mutex::",
        "RwLock::",
        ".lock(",
        ".try_lock(",
        ".read().",
        ".write().",
        "std::fs",
        "File::",
        "OpenOptions::",
        "std::io",
        "println!",
        "eprintln!",
        "print!",
        "eprint!",
        "log::",
        "thread::",
        ".spawn(",
        ".sleep(",
        ".wait(",
        ".join(",
        "std::alloc",
        "alloc::alloc",
        "libc::malloc",
        "libc::calloc",
        "libc::realloc",
        "libc::free",
    ]
    .into_iter()
    .find(|operation| compact.contains(*operation))
}

#[test]
fn spectrum_ingress_and_local_helpers_avoid_blocking_work() {
    for &(source, signature) in SURFACE {
        let body = function_body(source, signature);
        assert_eq!(forbidden_operation(&body), None, "{signature}");
    }
}

#[test]
fn spectrum_ingress_guard_rejects_mutated_bodies() {
    for &(source, signature) in SURFACE {
        let body = function_body(source, signature);
        for mutation in [
            "let _ = Vec :: <u8> :: new ();",
            "let _ = vec![0_u8; 8];",
            "let _ = Box::new(1);",
            "let _ = format!(\"value\");",
            "let _ = samples.to_vec();",
            "let _ = samples.iter().collect::<Vec<_>>();",
            "buffer.reserve(4);",
            "let _guard = mutex . lock ();",
            "let _guard = mutex . try_lock ();",
            "let _ = std :: fs :: read(path);",
            "let _ = File :: open(path);",
            "std::thread::sleep(duration);",
            "condition.wait(guard);",
            "worker.join();",
            "let _ = unsafe { libc::malloc(8) };",
        ] {
            // Exercise the same body extraction as the real-source contract.
            let fixture = format!("fn fixture() {{\n{body}\n{mutation}\n}}");
            let mutated = function_body(&fixture, "fn fixture(");
            assert!(
                forbidden_operation(&mutated).is_some(),
                "{signature} accepted {mutation}"
            );
        }
    }
}

#[test]
fn spectrum_ingress_guard_accepts_atomic_ring_and_arithmetic() {
    let source = r#"
        fn fixture() {
            // File::open and Vec::new are not executed by this comment.
            let enabled = flag.load(Ordering::Acquire);
            generation.compare_exchange(old, next, Ordering::AcqRel, Ordering::Relaxed);
            let enough = samples.slots() >= input.len();
            for sample in input { let _ = samples.push(*sample); }
            let _ = blocks.push(descriptor);
            dropped.fetch_add(1, Ordering::Relaxed);
            let next = current.checked_add(1).unwrap_or(0);
        }
    "#;
    assert_eq!(
        forbidden_operation(&function_body(source, "fn fixture(")),
        None
    );
}
