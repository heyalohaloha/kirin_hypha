//! 
//!
//! - PluginDataFile::new が instance_id / paired_*_instance_id を保持する
//! - Some / None の serialize 挙動 (#[serde(skip_serializing_if = "Option::is_none")])
//! - JSON roundtrip で field が保たれる
//! - SCHEMA_VERSION が "1.2" である

use kirin_measure::{PluginDataFile, PluginDataRole as Role};

#[test]
fn cross_instance_pair_struct_roundtrip() {
    // PRE 側 plugin_data: paired_post_instance_id を書き込む
    let pre_data = PluginDataFile::new(
        "install_xx".to_string(),
        "project_test".to_string(),
        "pre_instance_aaaa".to_string(),
        Role::Pre,
        None,
        48_000,
        None,
        Some("post_instance_bbbb".to_string()),
    );
    assert_eq!(pre_data.instance_id, "pre_instance_aaaa");
    assert_eq!(
        pre_data.paired_post_instance_id.as_deref(),
        Some("post_instance_bbbb")
    );
    assert!(pre_data.paired_pre_instance_id.is_none());

    // POST 側 plugin_data: paired_pre_instance_id を書き込む
    let post_data = PluginDataFile::new(
        "install_xx".to_string(),
        "project_test".to_string(),
        "post_instance_bbbb".to_string(),
        Role::Post,
        None,
        48_000,
        Some("pre_instance_aaaa".to_string()),
        None,
    );
    assert_eq!(post_data.instance_id, "post_instance_bbbb");
    assert_eq!(
        post_data.paired_pre_instance_id.as_deref(),
        Some("pre_instance_aaaa")
    );
    assert!(post_data.paired_post_instance_id.is_none());

    // 双方向一致を確認
    assert_eq!(
        pre_data.instance_id,
        post_data.paired_pre_instance_id.as_ref().unwrap().as_str()
    );
    assert_eq!(
        post_data.instance_id,
        pre_data.paired_post_instance_id.as_ref().unwrap().as_str()
    );

    // JSON roundtrip で field が保たれること
    let pre_json = serde_json::to_string(&pre_data).unwrap();
    assert!(pre_json.contains("\"instance_id\":\"pre_instance_aaaa\""));
    assert!(pre_json.contains("\"paired_post_instance_id\":\"post_instance_bbbb\""));
    // None は skip_serializing_if で省略
    assert!(!pre_json.contains("paired_pre_instance_id"));

    let post_json = serde_json::to_string(&post_data).unwrap();
    assert!(post_json.contains("\"paired_pre_instance_id\":\"pre_instance_aaaa\""));
    assert!(!post_json.contains("paired_post_instance_id"));

    let pre_deserialized: PluginDataFile = serde_json::from_str(&pre_json).unwrap();
    assert_eq!(pre_deserialized.instance_id, "pre_instance_aaaa");
    assert_eq!(
        pre_deserialized.paired_post_instance_id.as_deref(),
        Some("post_instance_bbbb")
    );
}

#[test]
fn schema_version_v12() {
    assert_eq!(PluginDataFile::SCHEMA_VERSION, "1.2");
}

#[test]
fn both_paired_fields_none_serialize_omits_both() {
    let f = PluginDataFile::new(
        "install_xx".to_string(),
        "project_test".to_string(),
        "iid_solo".to_string(),
        Role::Pre,
        None,
        48_000,
        None,
        None,
    );
    let json = serde_json::to_string(&f).unwrap();
    assert!(!json.contains("paired_pre_instance_id"));
    assert!(!json.contains("paired_post_instance_id"));
    // instance_id は required なので必ず出る
    assert!(json.contains("\"instance_id\":\"iid_solo\""));
}
