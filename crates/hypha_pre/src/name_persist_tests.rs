use super::{sanitize_name, HyphaPreParams};
use nih_plug::prelude::Params;

#[test]
fn name_roundtrip_empty() {
    let original = "";
    let params = HyphaPreParams::default();
    *params.name.write().unwrap() = original.into();
    let fields = params.serialize_fields();
    assert_eq!(fields.get("name").unwrap(), &format!("\"{original}\""));
    let restored = HyphaPreParams::default();
    *restored.name.write().unwrap() = "replace-me".into();
    restored.deserialize_fields(&fields);
    assert_eq!(*restored.name.read().unwrap(), original);
}

#[test]
fn name_roundtrip_ascii_16() {
    let original = "AbcDefGhi1234567";
    assert_eq!(original.chars().count(), 16);
    let params = HyphaPreParams::default();
    *params.name.write().unwrap() = original.into();
    let fields = params.serialize_fields();
    assert_eq!(fields.get("name").unwrap(), &format!("\"{original}\""));
    let restored = HyphaPreParams::default();
    *restored.name.write().unwrap() = "replace-me".into();
    restored.deserialize_fields(&fields);
    assert_eq!(*restored.name.read().unwrap(), original);
}

#[test]
fn name_sanitize_truncates_over_16() {
    let raw = "a".repeat(20);
    let sanitized = sanitize_name(&raw);
    assert_eq!(sanitized.len(), 16);
    assert_eq!(sanitized, "a".repeat(16));
}

#[test]
fn name_sanitize_keeps_non_ascii() {
    let raw = "日本語Snare";
    let sanitized = sanitize_name(raw);
    assert_eq!(sanitized, "日本語Snare");
}

#[test]
fn name_sanitize_strips_control_chars() {
    let raw = "\x01Snare\x7f";
    let sanitized = sanitize_name(raw);
    assert_eq!(sanitized, "Snare");
}
