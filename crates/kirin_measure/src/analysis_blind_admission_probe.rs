//! Executable BL-04 admission probe, test-only: two real Analysis slots, one Blind reservation.
//! The borrow proves that the underlying slot cannot be released while its reservation lives.
//! This does NOT yet prove DAW participant scope, Record arbitration or RT output completion.

use super::*;

struct Reservation<'lease> {
    _analysis: &'lease mut AnalysisLease,
    file: File,
}

impl<'lease> Reservation<'lease> {
    fn try_new(analysis: &'lease mut AnalysisLease) -> io::Result<Option<Self>> {
        if analysis.held_slot().is_none() {
            return Ok(None);
        }
        let path = analysis.paths[0].with_extension("blind-reservation");
        let mut options = OpenOptions::new();
        options.read(true).write(true).create(true).truncate(false);
        #[cfg(unix)]
        options.mode(0o600);
        let file = options.open(path)?;
        match file.try_lock() {
            Ok(()) => Ok(Some(Self {
                _analysis: analysis,
                file,
            })),
            Err(TryLockError::WouldBlock) => Ok(None),
            Err(TryLockError::Error(error)) => Err(error),
        }
    }
}

impl Drop for Reservation<'_> {
    fn drop(&mut self) {
        let _ = self.file.unlock();
        // Keep the inode stable, just as for the Analysis slots. No unlink on release.
    }
}

fn paths(root: &Path) -> [PathBuf; 2] {
    [root.join("analysis.0.lease"), root.join("analysis.1.lease")]
}

#[test]
fn blind_requires_one_of_the_existing_two_slots_and_never_creates_a_third() {
    let root = tempfile::tempdir().unwrap();
    let mut first = AnalysisLease::at_paths(paths(root.path()));
    let mut second = AnalysisLease::at_paths(paths(root.path()));
    let mut third = AnalysisLease::at_paths(paths(root.path()));
    assert!(Reservation::try_new(&mut first).unwrap().is_none());
    assert!(first.try_acquire().unwrap());
    assert!(second.try_acquire().unwrap());
    let reservation = Reservation::try_new(&mut first).unwrap().unwrap();
    assert!(Reservation::try_new(&mut second).unwrap().is_none());
    assert!(!third.try_acquire().unwrap());
    assert!(Reservation::try_new(&mut third).unwrap().is_none());
    drop(reservation);
    assert_eq!(first.held_slot(), Some(0));
    assert_eq!(second.held_slot(), Some(1));
    assert!(Reservation::try_new(&mut second).unwrap().is_some());
}

#[test]
fn failed_reservation_keeps_other_analysis_and_explicit_retry_is_required() {
    let root = tempfile::tempdir().unwrap();
    let mut first = AnalysisLease::at_paths(paths(root.path()));
    let mut second = AnalysisLease::at_paths(paths(root.path()));
    assert!(first.try_acquire().unwrap());
    assert!(second.try_acquire().unwrap());
    let reservation = Reservation::try_new(&mut first).unwrap().unwrap();
    assert!(Reservation::try_new(&mut second).unwrap().is_none());
    assert_eq!(second.held_slot(), Some(1));
    drop(reservation);
    first.release();
    let mut replacement = AnalysisLease::at_paths(paths(root.path()));
    assert!(replacement.try_acquire().unwrap());
    assert!(Reservation::try_new(&mut replacement).unwrap().is_some());
    assert_eq!(second.held_slot(), Some(1));
}

#[test]
fn simultaneous_starts_have_one_winner_while_both_keep_their_analysis_slot() {
    let root = tempfile::tempdir().unwrap();
    let barrier = std::sync::Arc::new(std::sync::Barrier::new(2));
    let winners = std::sync::atomic::AtomicUsize::new(0);
    let mut leases =
        std::array::from_fn::<_, 2, _>(|_| AnalysisLease::at_paths(paths(root.path())));
    for lease in &mut leases {
        assert!(lease.try_acquire().unwrap());
    }
    std::thread::scope(|scope| {
        for mut lease in leases {
            let barrier = barrier.clone();
            let winners = &winners;
            scope.spawn(move || {
                barrier.wait();
                let reservation = Reservation::try_new(&mut lease);
                if matches!(reservation, Ok(Some(_))) {
                    winners.fetch_add(1, Ordering::SeqCst);
                }
                // Winner cannot release before the other attempt finishes.
                barrier.wait();
                // An I/O error must fail the test, not strand the other thread at the barrier.
                let reservation = reservation.unwrap();
                assert_eq!(winners.load(Ordering::SeqCst), 1);
                drop(reservation);
                assert!(lease.held_slot().is_some());
            });
        }
    });
}

#[test]
fn filesystem_failure_is_not_permission_and_does_not_release_analysis() {
    let root = tempfile::tempdir().unwrap();
    let mut lease = AnalysisLease::at_paths(paths(root.path()));
    assert!(lease.try_acquire().unwrap());
    std::fs::create_dir(paths(root.path())[0].with_extension("blind-reservation")).unwrap();
    assert!(Reservation::try_new(&mut lease).is_err());
    assert_eq!(lease.held_slot(), Some(0));
}
