use super::*;

#[test]
fn two_owners_hold_distinct_stable_kernel_slots_and_third_waits() {
    let temp = tempfile::tempdir().unwrap();
    let paths = [
        temp.path().join("analysis.0.lease"),
        temp.path().join("analysis.1.lease"),
    ];
    let make_lease = || AnalysisLease::at_paths(paths.clone());
    let mut first = make_lease();
    let mut second = make_lease();
    let mut third = make_lease();
    assert!(first.try_acquire_for("Mix").unwrap());
    assert!(second.try_acquire_for("Vocal").unwrap());
    assert_eq!(first.held_slot(), Some(0));
    assert_eq!(second.held_slot(), Some(1));
    assert!(!third.try_acquire_for("Music").unwrap());
    assert_eq!(
        third.observed_owner_names(),
        ["Mix".to_string(), "Vocal".to_string()]
    );
    first.release();
    assert!(third.try_acquire_for("Music").unwrap());
    assert_eq!(third.held_slot(), Some(0));
}

#[test]
fn held_owner_rename_is_published_without_releasing_its_slot() {
    let temp = tempfile::tempdir().unwrap();
    let paths = [
        temp.path().join("analysis.0.lease"),
        temp.path().join("analysis.1.lease"),
    ];
    let mut owner = AnalysisLease::at_paths(paths.clone());
    let mut observer = AnalysisLease::at_paths(paths);
    assert!(owner.try_acquire_for("Mix").unwrap());
    assert!(owner.try_acquire_for("2Mix").unwrap());
    assert!(observer.try_acquire_for("Vocal").unwrap());
    let mut waiting = AnalysisLease::at_paths([
        temp.path().join("analysis.0.lease"),
        temp.path().join("analysis.1.lease"),
    ]);
    assert!(!waiting.try_acquire_for("Music").unwrap());
    assert_eq!(
        waiting.observed_owner_names(),
        ["2Mix".to_string(), "Vocal".to_string()]
    );
}

#[test]
fn unknown_kernel_owner_falls_back_without_inventing_a_name() {
    let temp = tempfile::tempdir().unwrap();
    let path = temp.path().join("analysis.lease");
    let external = OpenOptions::new()
        .read(true)
        .write(true)
        .create(true)
        .truncate(false)
        .open(&path)
        .unwrap();
    external.try_lock().unwrap();

    let mut observer = AnalysisLease::at_path(path);
    assert!(!observer.try_acquire_for("Music").unwrap());
    assert_eq!(
        observer.observed_owner_names(),
        [String::new(), String::new()]
    );
}

#[test]
fn invalid_parent_fails_without_claiming() {
    let temp = tempfile::tempdir().unwrap();
    let blocker = temp.path().join("file");
    std::fs::write(&blocker, b"not a directory").unwrap();
    let mut lease = AnalysisLease::at_path(blocker.join("analysis.lease"));
    assert!(lease.try_acquire().is_err());
}

#[test]
fn one_broken_slot_does_not_hide_an_available_valid_slot() {
    let temp = tempfile::tempdir().unwrap();
    let blocker = temp.path().join("file");
    std::fs::write(&blocker, b"not a directory").unwrap();
    let valid = temp.path().join("analysis.1.lease");
    let mut lease = AnalysisLease::at_paths([blocker.join("analysis.0.lease"), valid]);
    assert!(lease.try_acquire().unwrap());
    assert_eq!(lease.held_slot(), Some(1));
}

#[test]
fn only_one_audition_owns_one_of_two_analysis_slots() {
    let temp = tempfile::tempdir().unwrap();
    let paths = [
        temp.path().join("analysis.0.lease"),
        temp.path().join("analysis.1.lease"),
    ];
    let process_path = temp.path().join("process.audition.lease");
    let make_admission =
        || AuditionAdmission::at_paths(paths.clone(), process_path.clone(), temp.path(), "project");
    let mut first = make_admission();
    let mut second = make_admission();
    assert!(first.try_acquire_for("Reference").unwrap());
    assert!(first.held());
    assert!(!second.try_acquire_for("PRE POST Blind").unwrap());
    assert!(!second.held());

    let mut other_analysis = AnalysisLease::at_paths(paths.clone());
    assert!(other_analysis.try_acquire_for("Drum").unwrap());
    first.release();
    assert!(!first.held());
    assert!(second.try_acquire_for("PRE POST Blind").unwrap());
}

#[test]
fn one_process_cannot_audition_two_projects() {
    let temp = tempfile::tempdir().unwrap();
    let paths = [
        temp.path().join("analysis.0"),
        temp.path().join("analysis.1"),
    ];
    let process = temp.path().join("process.audition");
    let mut first =
        AuditionAdmission::at_paths(paths.clone(), process.clone(), temp.path(), "first");
    let mut second = AuditionAdmission::at_paths(paths, process, temp.path(), "second");
    assert!(first.try_acquire_for("Reference").unwrap());
    assert!(!second.try_acquire_for("PRE POST Blind").unwrap());
}

#[test]
fn one_unreadable_slot_and_one_occupied_slot_report_in_use() {
    let temp = tempfile::tempdir().unwrap();
    let bad = temp.path().join("directory");
    std::fs::create_dir(&bad).unwrap();
    let good = temp.path().join("occupied.lease");
    let mut holder = AnalysisLease::at_paths([good.clone()]);
    assert!(holder.try_acquire_for("analysis").unwrap());
    let mut contender = AnalysisLease::at_paths([bad.clone(), good]);
    assert!(!contender.try_acquire().unwrap());
    assert_eq!(contender.observed_owner_names()[1], "analysis");
    let mut unavailable = AnalysisLease::at_paths([bad]);
    assert!(unavailable.try_acquire().is_err());
}
