use super::*;
use std::sync::{mpsc, Mutex, OnceLock};
use std::time::Duration;

struct Pause {
    path: PathBuf,
    phase: &'static str,
    reached: mpsc::SyncSender<()>,
    resume: mpsc::Receiver<()>,
}
static PAUSES: OnceLock<Mutex<Vec<Pause>>> = OnceLock::new();
const LIMIT: Duration = Duration::from_secs(5);
struct PauseGuard {
    path: PathBuf,
    phase: &'static str,
    reached: mpsc::Receiver<()>,
    resume: mpsc::SyncSender<()>,
}
impl PauseGuard {
    fn install(path: &Path, phase: &'static str) -> Self {
        let (tx, reached) = mpsc::sync_channel(1);
        let (resume, rx) = mpsc::sync_channel(1);
        PAUSES
            .get_or_init(Default::default)
            .lock()
            .unwrap()
            .push(Pause {
                path: path.to_owned(),
                phase,
                reached: tx,
                resume: rx,
            });
        Self {
            path: path.to_owned(),
            phase,
            reached,
            resume,
        }
    }
    fn reached(&self) {
        self.reached
            .recv_timeout(LIMIT)
            .expect("entry hook not reached");
    }
}
impl Drop for PauseGuard {
    fn drop(&mut self) {
        let _ = self.resume.try_send(());
        PAUSES
            .get_or_init(Default::default)
            .lock()
            .unwrap()
            .retain(|p| p.path != self.path || p.phase != self.phase);
    }
}
fn pause(path: &Path, phase: &'static str) {
    let hook = {
        let mut hooks = PAUSES.get_or_init(Default::default).lock().unwrap();
        hooks
            .iter()
            .position(|p| p.path == path && p.phase == phase)
            .map(|i| hooks.remove(i))
    };
    if let Some(hook) = hook {
        let _ = hook.reached.try_send(());
        hook.resume
            .recv_timeout(LIMIT)
            .expect("entry test release deadline");
    }
}
pub(super) fn after_progress_read(path: &Path) {
    pause(path, "progress");
}
pub(super) fn after_claim_lock(path: &Path) {
    pause(path, "claim");
}

fn expire(path: &Path) {
    let mut entry: RecordEntryLockFile = serde_json::from_slice(&fs::read(path).unwrap()).unwrap();
    entry.heartbeat_at_ms = now_epoch_ms() - RECORD_ENTRY_LOCK_STALE_MS - 10_000;
    crate::atomic_file::write_bytes_atomic(path, &serde_json::to_vec(&entry).unwrap()).unwrap();
}
fn signal(base: &Path) -> crate::record_signal::RecordSignal {
    let generation = crate::capture_generation::CaptureGeneration::new_single(
        "project".into(),
        "post".into(),
        "pre".into(),
        "daw".into(),
        std::process::id(),
    );
    crate::record_signal::write_pending_claiming_expected_and_clock_for_generation(
        base,
        "project",
        "post",
        "pre".into(),
        "daw".into(),
        None,
        &generation,
    )
    .unwrap();
    crate::record_signal::read_signal(base, "project", "post").unwrap()
}

#[test]
fn progress_read_cannot_roll_back_a_reclaimed_owner() {
    let temp = tempfile::tempdir().unwrap();
    let signal = signal(temp.path());
    let owner = claim_record_entry(
        temp.path(),
        "project",
        &signal.session_id,
        Role::Post,
        "post",
    )
    .unwrap();
    let path = entry_lock_path(
        temp.path(),
        "project",
        &signal.session_id,
        Role::Post,
        "post",
    );
    expire(&path);
    let guard = PauseGuard::install(&path, "progress");
    std::thread::scope(|scope| {
        let task = scope.spawn(|| {
            publish_measure_preparation(
                temp.path(),
                "project",
                Role::Post,
                "post",
                &signal,
                2,
                100,
                &owner,
            )
        });
        guard.reached();
        // This bypasses the binary-local table: only the OS sibling lock can refuse it.
        assert!(matches!(
            claim_record_entry_inner(
                temp.path(),
                "project",
                &signal.session_id,
                Role::Post,
                "post",
                false
            ),
            Err(RecordEntryLockError::AlreadyActive { .. })
        ));
        drop(guard);
        task.join().unwrap().unwrap();
    });
    let replacement = claim_record_entry(
        temp.path(),
        "project",
        &signal.session_id,
        Role::Post,
        "post",
    )
    .unwrap();
    assert_ne!(owner, replacement);
    let before = fs::read(&path).unwrap();
    publish_measure_preparation(
        temp.path(),
        "project",
        Role::Post,
        "post",
        &signal,
        2,
        200,
        &owner,
    )
    .unwrap();
    assert_eq!(
        fs::read(&path).unwrap(),
        before,
        "same-pid stale owner must not publish"
    );
    publish_measure_preparation(
        temp.path(),
        "project",
        Role::Post,
        "post",
        &signal,
        2,
        200,
        &replacement,
    )
    .unwrap();
    let entry: RecordEntryLockFile = serde_json::from_slice(&fs::read(&path).unwrap()).unwrap();
    assert_eq!(entry.owner_id, replacement);
    assert_eq!(
        serde_json::to_value(entry).unwrap()["measure_preparation"]["samples"],
        200
    );
}

#[test]
fn independent_binary_reservations_are_excluded_by_os_lock() {
    let temp = tempfile::tempdir().unwrap();
    claim_record_entry(temp.path(), "ph", "session", Role::Post, "post").unwrap();
    let path = entry_lock_path(temp.path(), "ph", "session", Role::Post, "post");
    expire(&path);
    let guard = PauseGuard::install(&path, "claim");
    std::thread::scope(|scope| {
        let first = scope.spawn(|| {
            claim_record_entry_inner(temp.path(), "ph", "session", Role::Post, "post", false)
        });
        guard.reached();
        assert!(matches!(
            claim_record_entry_inner(temp.path(), "ph", "session", Role::Post, "post", false),
            Err(RecordEntryLockError::AlreadyActive { .. })
        ));
        drop(guard);
        let winner = first.join().unwrap().unwrap();
        let entry: RecordEntryLockFile = serde_json::from_slice(&fs::read(path).unwrap()).unwrap();
        assert_eq!(entry.owner_id, winner);
    });
}

#[test]
fn case_aliases_share_both_reservation_and_sibling_lock() {
    let temp = tempfile::tempdir().unwrap();
    let dir = entry_lock_dir(temp.path(), "ph", "session");
    fs::create_dir_all(dir).unwrap();
    let upper = entry_lock_path(temp.path(), "ph", "session", Role::Post, "AbC");
    let lower = entry_lock_path(temp.path(), "ph", "session", Role::Post, "abc");
    let reservation = acquisition::reserve(&upper).unwrap();
    assert!(matches!(
        acquisition::reserve(&lower),
        Err(RecordEntryLockError::AlreadyActive { .. })
    ));
    drop(reservation);
    let guard = PauseGuard::install(&upper, "claim");
    std::thread::scope(|scope| {
        let first = scope.spawn(|| {
            claim_record_entry_inner(temp.path(), "ph", "session", Role::Post, "AbC", false)
        });
        guard.reached();
        assert!(matches!(
            claim_record_entry_inner(temp.path(), "ph", "session", Role::Post, "abc", false),
            Err(RecordEntryLockError::AlreadyActive { .. })
        ));
        drop(guard);
        first.join().unwrap().unwrap();
    });
    // On a case-insensitive filesystem the released sibling still protects the same entry.
    // On a case-sensitive filesystem these are distinct entries, not an alias claim.
    if lower.exists() {
        assert!(matches!(
            claim_record_entry_inner(temp.path(), "ph", "session", Role::Post, "abc", false),
            Err(RecordEntryLockError::AlreadyActive { .. })
        ));
    }
}

#[test]
fn busy_progress_is_skipped_and_lock_inode_is_retained() {
    let temp = tempfile::tempdir().unwrap();
    let signal = signal(temp.path());
    let owner = claim_record_entry(
        temp.path(),
        "project",
        &signal.session_id,
        Role::Post,
        "post",
    )
    .unwrap();
    let path = entry_lock_path(
        temp.path(),
        "project",
        &signal.session_id,
        Role::Post,
        "post",
    );
    let before = fs::read(&path).unwrap();
    let guard = mutation::try_acquire(&path).unwrap();
    publish_measure_preparation(
        temp.path(),
        "project",
        Role::Post,
        "post",
        &signal,
        2,
        100,
        &owner,
    )
    .unwrap();
    assert_eq!(fs::read(&path).unwrap(), before);
    drop(guard);
    let sibling = path.with_file_name("post__post.json.lock");
    assert!(sibling.is_file());
    publish_measure_preparation(
        temp.path(),
        "project",
        Role::Post,
        "post",
        &signal,
        2,
        100,
        &owner,
    )
    .unwrap();
    assert_ne!(fs::read(&path).unwrap(), before);
    assert!(sibling.is_file());
}

#[test]
fn claim_tokens_do_not_depend_on_binary_local_pid_sequences() {
    let temp = tempfile::tempdir().unwrap();
    let a = claim_record_entry(temp.path(), "ph", "one", Role::Post, "post").unwrap();
    let b = claim_record_entry(temp.path(), "ph", "two", Role::Post, "post").unwrap();
    assert_eq!(uuid::Uuid::parse_str(&a).unwrap().get_version_num(), 4);
    assert_eq!(uuid::Uuid::parse_str(&b).unwrap().get_version_num(), 4);
    assert_ne!(a, b);
}
