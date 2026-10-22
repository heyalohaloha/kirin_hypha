use super::*;

#[test]
fn pair_fixture_leases_are_removed_on_return_and_unwind() {
    for unwind in [false, true] {
        let fixture = Fixture::new();
        let root = fixture._temp.path().to_path_buf();
        let mut paths = Vec::new();
        for coordinator in [&fixture.pre, &fixture.post] {
            assert!(coordinator.ensure_analysis_lease("fixture").unwrap());
            paths.push(
                coordinator
                    .analysis_lease
                    .lock()
                    .unwrap()
                    .held_path_for_test()
                    .unwrap(),
            );
        }
        let contained = paths.iter().all(|path| path.starts_with(&root));
        assert!(
            paths.iter().all(|path| path.is_file()),
            "leases were actually acquired"
        );
        let result = std::panic::catch_unwind(std::panic::AssertUnwindSafe(move || {
            let _owned = fixture;
            assert!(!unwind, "fixture assertion failure");
        }));
        let survived = paths.iter().any(|path| path.exists());
        // The negative control must clean only these exact, now-unlocked fixture-owned leases.
        for path in &paths {
            match std::fs::remove_file(path) {
                Ok(()) => {}
                Err(error) if error.kind() == std::io::ErrorKind::NotFound => {}
                Err(error) => panic!("could not remove fixture lease: {error}"),
            }
        }
        assert_eq!(result.is_err(), unwind);
        assert!(!root.exists());
        assert!(contained, "all leases must belong to the fixture TempDir");
        assert!(
            !survived,
            "the fixture must remove its leases after joining and unlocking"
        );
    }
}
