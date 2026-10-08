use super::*;

#[test]
fn fixed_prefix_reads_exclude_large_body_and_observe_atomic_replacements() {
    let directory = tempfile::tempdir().unwrap();
    let path = directory.path().join("attack/pre.bin");
    let mut bytes = vec![0x5a; 196_000];
    bytes[..8].copy_from_slice(b"KHATK001");
    bytes[8..10].copy_from_slice(&5_u16.to_le_bytes());
    bytes[12..28].copy_from_slice(uuid::Uuid::from_u128(17).as_bytes());
    write(directory.path(), &path, AnalysisSlot::Attack, &bytes).unwrap();
    for _ in 0..100 {
        let prefix = read_prefix(directory.path(), &path, AnalysisSlot::Attack, 28, 262_144)
            .expect("bounded request header");
        assert_eq!(prefix.len(), 28);
        assert_eq!(uuid::Uuid::from_slice(&prefix[12..]).unwrap().as_u128(), 17);
    }
    bytes[12..28].copy_from_slice(uuid::Uuid::from_u128(18).as_bytes());
    write(directory.path(), &path, AnalysisSlot::Attack, &bytes).unwrap();
    let prefix = read_prefix(directory.path(), &path, AnalysisSlot::Attack, 28, 262_144).unwrap();
    assert_eq!(uuid::Uuid::from_slice(&prefix[12..]).unwrap().as_u128(), 18);
    assert!(read_prefix(directory.path(), &path, AnalysisSlot::Attack, 28, 100).is_none());
    remove(directory.path(), &path, AnalysisSlot::Attack).unwrap();
    assert!(read_prefix(directory.path(), &path, AnalysisSlot::Attack, 28, 262_144).is_none());
}

#[test]
#[ignore]
fn foreign_attack_header_probe_copies_bounded_bytes_at_full_tick_rate() {
    let directory = tempfile::tempdir().unwrap();
    let path = directory.path().join("attack/pre.bin");
    let bytes = vec![0x5a; 196_000];
    write(directory.path(), &path, AnalysisSlot::Attack, &bytes).unwrap();
    let started = std::time::Instant::now();
    let mut prefix_bytes = 0;
    for _ in 0..3000 {
        let bytes =
            read_prefix(directory.path(), &path, AnalysisSlot::Attack, 28, 262_144).unwrap();
        prefix_bytes += bytes.len();
        std::hint::black_box(bytes);
    }
    let prefix_time = started.elapsed();
    let started = std::time::Instant::now();
    let mut full_bytes = 0;
    for _ in 0..3000 {
        let bytes = read(directory.path(), &path, AnalysisSlot::Attack, 262_144).unwrap();
        full_bytes += bytes.len();
        std::hint::black_box(bytes);
    }
    let full_time = started.elapsed();
    assert_eq!(prefix_bytes, 84_000);
    assert_eq!(full_bytes, 588_000_000);
    println!("3000 foreign-request probes: {prefix_bytes} copied bytes in {prefix_time:?}; prior full payload {full_bytes} copied bytes in {full_time:?}");
    assert!(
        prefix_time < full_time,
        "fixed header must reduce actual transport work"
    );
}
