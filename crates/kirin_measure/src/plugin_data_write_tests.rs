use super::*;
use crate::plugin_data::{Frame, PsbSnapshot, Role};
use std::io;

fn data(role: Role) -> PluginDataFile {
    PluginDataFile::new(
        "synthetic-installation".into(),
        "synthetic-project".into(),
        "synthetic-instance".into(),
        role,
        None,
        48_000,
        None,
        None,
        None,
        None,
    )
}

fn assert_original_bytes(mut data: PluginDataFile) {
    let mut canonical = data.clone();
    canonical.checksum = compute_checksum(&canonical).unwrap();
    let expected = serde_json::to_vec(&canonical).unwrap();
    assert_eq!(
        serde_json::to_string(&canonical).unwrap().as_bytes(),
        expected
    );
    let encoded = encode_with_checksum(&mut data).unwrap();
    assert_eq!(encoded, expected);
    assert_eq!(data.checksum, canonical.checksum);
    assert!(super::super::verify_checksum(&data));
    assert_eq!(data.checksum.len(), 64);
}

#[test]
fn single_serialization_preserves_canonical_bytes_and_escaped_optional_text() {
    assert_original_bytes(data(Role::Pre));
    let mut record = data(Role::Post);
    record.chain_memo = "菌糸🙂 \"checksum\":\"\"} \\ \n\u{0}".into();
    record.bus = Some("MID / SIDE".into());
    record.pair_name = Some("前後 \"checksum\":\"quoted\"".into());
    record.pair_pre_name = Some(String::new());
    record.record_session_id = Some("synthetic-record".into());
    record.capture_generation_id = Some("synthetic-generation".into());
    record.commit_status = Some("pair_pending".into());
    record.integrity_reasons = vec!["嵌入 } \"checksum\":\"\"".into()];
    record.lufs_i = Some(-14.3);
    record.lra = Some(8.7);
    record.plr = Some(13.1);
    record.checksum = "previous checksum containing \"checksum\":\"\"}".into();
    assert_original_bytes(record);
}

#[test]
fn single_serialization_preserves_large_native_trace_and_psb_payloads() {
    let mut record = data(Role::Post);
    for index in 0..36_000 {
        record.frames.push(Frame {
            t_ms: index * 100,
            n_prime: Some(std::array::from_fn(|band| 0.01 * (band + 1) as f64)),
            n_prime_total: Some(7.1),
            sharpness: Some(1.4),
            lufs_m: -14.3,
            true_peak: -1.2,
            crest: 12.6,
            psr: (index >= 30).then_some(13.1),
        });
    }
    record.psb_snapshots = Some(
        (0..7_200)
            .map(|index| PsbSnapshot {
                t_ms: index * 500,
                psb: std::array::from_fn(|band| -45.0 + band as f64),
                interpolatable: true,
            })
            .collect(),
    );
    record.trace_slot_positions = (0..36_000).map(|index| index * 4_800).collect();
    assert_original_bytes(record);
}

#[test]
fn failed_serialization_and_unwinding_restore_the_previous_checksum() {
    let mut record = data(Role::Post);
    record.checksum = "confirmed old value".into();
    let error = encode_using(
        &mut record,
        |value| {
            assert!(value.checksum.is_empty());
            Err(serde_json::Error::io(io::Error::other(
                "injected serialization failure",
            )))
        },
        serde_json::to_vec,
    );
    assert!(error.is_err());
    assert_eq!(record.checksum, "confirmed old value");
    let panic = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        let _ = encode_using(
            &mut record,
            |_| panic!("injected serializer panic"),
            serde_json::to_vec,
        );
    }));
    assert!(panic.is_err());
    assert_eq!(record.checksum, "confirmed old value");
}

#[test]
fn unsupported_final_shape_uses_the_canonical_fallback() {
    let mut record = data(Role::Post);
    record.chain_memo = "nested \"checksum\":\"\"}".into();
    let bytes = encode_using(
        &mut record,
        |_| Ok(br#"{"checksum":"","future":"field after checksum"}"#.to_vec()),
        serde_json::to_vec,
    )
    .unwrap();
    assert_eq!(bytes, serde_json::to_vec(&record).unwrap());
    assert_eq!(record.checksum, compute_checksum(&record).unwrap());
    assert!(super::super::verify_checksum(&record));
}

#[test]
fn failed_fallback_serialization_restores_the_previous_checksum() {
    let mut record = data(Role::Post);
    record.checksum = "previous confirmed checksum".into();
    let encoded = encode_using(
        &mut record,
        |_| Ok(br#"{"checksum":"","future":"field after checksum"}"#.to_vec()),
        |value| {
            assert_eq!(value.checksum, compute_checksum(value).unwrap());
            Err(serde_json::Error::io(io::Error::other(
                "injected final serialization failure",
            )))
        },
    );
    assert!(encoded.is_err());
    assert_eq!(record.checksum, "previous confirmed checksum");
}

#[test]
fn failed_atomic_write_preserves_the_existing_target_and_checksum_contract() {
    let temporary = tempfile::tempdir().unwrap();
    let destination = temporary.path().join("confirmed.json");
    fs::write(&destination, b"previous confirmed bytes").unwrap();
    let invalid_tmp = temporary.path().join("blocked.tmp");
    fs::create_dir(&invalid_tmp).unwrap();
    let paths = super::super::WriterPaths::build(
        temporary.path(),
        "synthetic-project",
        "synthetic-instance",
        Role::Post,
        "2026-10-08T00:00:00Z",
    );
    let mut writer = PluginDataWriter {
        paths,
        data: data(Role::Post),
    };
    assert!(writer
        .write_atomic_with_tmp(destination.clone(), invalid_tmp)
        .is_err());
    assert_eq!(fs::read(destination).unwrap(), b"previous confirmed bytes");
    assert!(super::super::verify_checksum(writer.data()));
}

#[test]
fn failed_atomic_rename_keeps_the_destination_and_complete_temporary_bytes() {
    let temporary = tempfile::tempdir().unwrap();
    let destination = temporary.path().join("destination-directory");
    fs::create_dir(&destination).unwrap();
    fs::write(destination.join("canary"), b"untouched").unwrap();
    let tmp = temporary.path().join("complete.tmp");
    let paths = super::super::WriterPaths::build(
        temporary.path(),
        "synthetic-project",
        "synthetic-instance",
        Role::Post,
        "2026-10-08T00:00:00Z",
    );
    let mut writer = PluginDataWriter {
        paths,
        data: data(Role::Post),
    };
    assert!(writer
        .write_atomic_with_tmp(destination.clone(), tmp.clone())
        .is_err());
    assert_eq!(fs::read(destination.join("canary")).unwrap(), b"untouched");
    assert_eq!(
        fs::read(tmp).unwrap(),
        serde_json::to_vec(writer.data()).unwrap()
    );
    assert!(super::super::verify_checksum(writer.data()));
}
