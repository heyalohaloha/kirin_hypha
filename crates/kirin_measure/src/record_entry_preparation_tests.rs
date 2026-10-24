use super::*;

#[test]
fn preparation_requires_the_exact_generation_session_owner_and_completed_input() {
    for role in [Role::Pre, Role::Post] {
        preparation_for_role(role);
    }
}

fn preparation_for_role(role: Role) {
    let temp = tempfile::tempdir().unwrap();
    let generation = crate::capture_generation::CaptureGeneration::new_single(
        "project".into(),
        "post".into(),
        "pre".into(),
        "daw".into(),
        std::process::id(),
    );
    let member = &generation.members[0];
    let instance = if role == Role::Pre { "pre" } else { "post" };
    crate::record_signal::write_pending_claiming_expected_and_clock_for_generation(
        temp.path(),
        "project",
        "post",
        "pre".into(),
        "daw".into(),
        None,
        &generation,
    )
    .unwrap();
    let signal = crate::record_signal::read_signal(temp.path(), "project", "post").unwrap();
    claim_record_entry(
        temp.path(),
        "project",
        &member.record_session_id,
        role,
        instance,
    )
    .unwrap();
    let read = || measure_preparation_samples(temp.path(), &generation, member, role);
    assert_eq!(read(), 0);
    let publish = |samples| {
        publish_measure_preparation(temp.path(), "project", role, instance, &signal, 2, samples)
            .unwrap()
    };
    publish(9_600);
    assert_eq!(read(), 9_600);
    for samples in [9_600, 4_800, 0] {
        publish(samples);
        assert_eq!(read(), 9_600);
    }
    publish(19_200);
    assert_eq!(read(), 19_200);
    let path = entry_lock_path(
        temp.path(),
        "project",
        &member.record_session_id,
        role,
        instance,
    );
    let original: RecordEntryLockFile = serde_json::from_slice(&fs::read(&path).unwrap()).unwrap();
    for wrong in 0..9 {
        let mut lock = original.clone();
        match wrong {
            0 => {
                lock.measure_preparation
                    .as_mut()
                    .unwrap()
                    .capture_generation_id = "old".into()
            }
            1 => {
                lock.measure_preparation
                    .as_mut()
                    .unwrap()
                    .generation_started_at_ms -= 1
            }
            2 => lock.host_process_id = std::process::id().saturating_add(1),
            3 => lock.record_session_id = "other".into(),
            4 => lock.instance_id = "other".into(),
            5 => lock.role = if role == Role::Pre { "post" } else { "pre" }.into(),
            6 => lock.project_hash = "other".into(),
            7 => lock.schema_version = "unknown".into(),
            _ => lock.measure_preparation.as_mut().unwrap().record_generation = 0,
        }
        crate::atomic_file::write_bytes_atomic(&path, &serde_json::to_vec(&lock).unwrap()).unwrap();
        assert_eq!(
            read(),
            0,
            "mismatched preparation evidence must be ignored: {wrong}"
        );
    }
}
