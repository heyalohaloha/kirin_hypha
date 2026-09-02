use super::*;

#[test]
fn unsafe_relative_names_and_noncanonical_numbers_are_rejected() {
    for value in [
        "/absolute.midi",
        "a/../b.midi",
        "a/./b.midi",
        "a\\b.midi",
        "a/b\0.midi",
        "C:/a/b.midi",
    ] {
        assert!(validate_relative_name(value, ".midi", "MIDI", 2).is_err());
    }
    assert!(validate_relative_name("a/b/c.midi", ".midi", "MIDI", 2).is_ok());
    assert!(canonical_u64("01", "number", 2).is_err());
    assert!(canonical_u64("1", "number", 2).is_ok());
}

#[test]
fn malformed_or_incomplete_manifests_fail_closed() {
    assert!(parse_pinned_manifest(b"").is_err());
    assert!(parse_pinned_manifest(format!("{HEADER}\n").as_bytes()).is_err());
    assert!(parse_pinned_manifest(b"wrong\n").is_err());
}

#[test]
fn digest_shape_requires_lowercase_hex() {
    assert!(require_sha256(&"a".repeat(64), "digest", 2).is_ok());
    assert!(require_sha256(&"A".repeat(64), "digest", 2).is_err());
    assert!(require_sha256(&"0".repeat(63), "digest", 2).is_err());
}
