use super::{sanitize_name, HyphaPostParams};
use nih_plug::prelude::Params;

#[test]
fn pair_pre_name_roundtrip_empty() {
    let original = "";
    let params = HyphaPostParams::default();
    *params.pair_pre_name.write().unwrap() = original.into();
    let fields = params.serialize_fields();
    assert_eq!(
        fields.get("pair_pre_name").unwrap(),
        &format!("\"{original}\"")
    );
    let restored = HyphaPostParams::default();
    *restored.pair_pre_name.write().unwrap() = "replace-me".into();
    restored.deserialize_fields(&fields);
    assert_eq!(*restored.pair_pre_name.read().unwrap(), original);
}

#[test]
fn pair_pre_name_roundtrip_ascii_16() {
    let original = "AbcDefGhi1234567";
    assert_eq!(original.chars().count(), 16);
    let params = HyphaPostParams::default();
    *params.pair_pre_name.write().unwrap() = original.into();
    let fields = params.serialize_fields();
    assert_eq!(
        fields.get("pair_pre_name").unwrap(),
        &format!("\"{original}\"")
    );
    let restored = HyphaPostParams::default();
    *restored.pair_pre_name.write().unwrap() = "replace-me".into();
    restored.deserialize_fields(&fields);
    assert_eq!(*restored.pair_pre_name.read().unwrap(), original);
}

#[test]
fn pair_pre_name_sanitize_truncates_over_16() {
    let params = HyphaPostParams::default();
    *params.pair_pre_name.write().unwrap() = sanitize_name("0123456789日本語ABCDEZ");
    let fields = params.serialize_fields();
    let restored = HyphaPostParams::default();
    restored.deserialize_fields(&fields);
    assert_eq!(
        *restored.pair_pre_name.read().unwrap(),
        "0123456789日本語ABC"
    );
}

#[test]
fn pair_pre_name_sanitize_keeps_non_ascii() {
    let params = HyphaPostParams::default();
    *params.pair_pre_name.write().unwrap() = sanitize_name("低音 Snare 🎵");
    let fields = params.serialize_fields();
    let restored = HyphaPostParams::default();
    restored.deserialize_fields(&fields);
    assert_eq!(*restored.pair_pre_name.read().unwrap(), "低音 Snare 🎵");
}

#[test]
fn pair_pre_name_sanitize_strips_control_chars() {
    let params = HyphaPostParams::default();
    *params.pair_pre_name.write().unwrap() = sanitize_name("\x00Snare\n\t\x7f");
    let fields = params.serialize_fields();
    let restored = HyphaPostParams::default();
    restored.deserialize_fields(&fields);
    assert_eq!(*restored.pair_pre_name.read().unwrap(), "Snare");
}
