//! Non-RT ownership of a persisted instance ID across plug-in modules and host processes.
//!
//! A copied state is a new processor when its restored ID already has a live owner. Lock files
//! are deliberately not unlinked: replacing a locked inode would split the ownership namespace.
use std::fs::{self, File, OpenOptions, TryLockError};
use std::io;
use std::path::Path;
use uuid::Uuid;

#[derive(Debug)]
pub(crate) struct InstanceIdentityLease {
    _file: File,
}

impl InstanceIdentityLease {
    pub(crate) fn acquire(root: &Path, role: &str, restored: &str) -> io::Result<(String, Self)> {
        if !matches!(role, "pre" | "post")
            || (!restored.is_empty() && !kirin_measure::is_path_safe_component(restored))
        {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                "invalid instance identity",
            ));
        }
        let directory = root.join(".instance_identity");
        fs::create_dir_all(&directory)?;
        if !fs::symlink_metadata(&directory)?.file_type().is_dir() {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                "invalid identity directory",
            ));
        }
        #[cfg(unix)]
        {
            use std::os::unix::fs::PermissionsExt;
            fs::set_permissions(&directory, fs::Permissions::from_mode(0o700))?;
        }
        let mut id = if restored.is_empty() {
            Uuid::new_v4().to_string()
        } else {
            restored.into()
        };
        for _ in 0..16 {
            let path = directory.join(format!("{role}_{id}.lease"));
            match fs::symlink_metadata(&path) {
                Ok(meta) if !meta.file_type().is_file() => {
                    return Err(io::Error::new(
                        io::ErrorKind::InvalidInput,
                        "invalid identity claim",
                    ))
                }
                Err(error) if error.kind() != io::ErrorKind::NotFound => return Err(error),
                _ => {}
            }
            let mut options = OpenOptions::new();
            options.read(true).write(true).create(true).truncate(false);
            #[cfg(unix)]
            {
                use std::os::unix::fs::OpenOptionsExt;
                options.mode(0o600);
            }
            let file = options.open(&path)?;
            if !file.metadata()?.is_file() || !fs::symlink_metadata(&path)?.file_type().is_file() {
                return Err(io::Error::new(
                    io::ErrorKind::InvalidInput,
                    "invalid identity claim",
                ));
            }
            match file.try_lock() {
                Ok(()) => return Ok((id, Self { _file: file })),
                Err(TryLockError::WouldBlock) => id = Uuid::new_v4().to_string(),
                Err(TryLockError::Error(error)) => return Err(error),
            }
        }
        Err(io::Error::other(
            "instance identity collision budget exhausted",
        ))
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::{BufRead, Read, Write};
    use std::process::{Command, Stdio};
    use std::sync::{Arc, Barrier};

    #[test]
    fn live_copy_rekeys_but_reopen_keeps_the_saved_identity() {
        let root = tempfile::tempdir().unwrap();
        let saved = "11111111-1111-4111-8111-111111111111";
        let (first, owner) = InstanceIdentityLease::acquire(root.path(), "pre", saved).unwrap();
        let (copied, copy) = InstanceIdentityLease::acquire(root.path(), "pre", saved).unwrap();
        assert_eq!(first, saved);
        assert_ne!(copied, saved);
        assert_ne!(
            copied,
            InstanceIdentityLease::acquire(root.path(), "post", saved)
                .unwrap()
                .0
        );
        drop(owner);
        let (reopened, _owner) = InstanceIdentityLease::acquire(root.path(), "pre", saved).unwrap();
        assert_eq!(reopened, saved);
        drop(copy);
        assert_eq!(
            InstanceIdentityLease::acquire(root.path(), "pre", &copied)
                .unwrap()
                .0,
            copied
        );
    }

    #[test]
    fn concurrent_copies_have_distinct_live_claims() {
        let root = tempfile::tempdir().unwrap();
        let ready = Arc::new(Barrier::new(8));
        let workers: Vec<_> = (0..8)
            .map(|_| {
                let path = root.path().to_owned();
                let ready = Arc::clone(&ready);
                std::thread::spawn(move || {
                    let (id, _owner) =
                        InstanceIdentityLease::acquire(&path, "post", "saved-post").unwrap();
                    ready.wait();
                    id
                })
            })
            .collect();
        let mut ids: Vec<_> = workers
            .into_iter()
            .map(|worker| worker.join().unwrap())
            .collect();
        ids.sort();
        ids.dedup();
        assert_eq!(ids.len(), 8);
    }

    #[test]
    fn io_failure_is_not_a_collision_or_permission_to_publish() {
        let root = tempfile::tempdir().unwrap();
        fs::write(root.path().join(".instance_identity"), b"not a directory").unwrap();
        assert!(InstanceIdentityLease::acquire(root.path(), "pre", "original").is_err());
        assert!(InstanceIdentityLease::acquire(root.path(), "pre", "../escape").is_err());
    }

    #[test]
    fn full_length_safe_identity_and_empty_identity_are_supported() {
        let root = tempfile::tempdir().unwrap();
        let saved = "x".repeat(64);
        assert_eq!(
            InstanceIdentityLease::acquire(root.path(), "pre", &saved)
                .unwrap()
                .0,
            saved
        );
        let (id, _lease) = InstanceIdentityLease::acquire(root.path(), "post", "").unwrap();
        assert!(Uuid::parse_str(&id).is_ok());
    }

    #[test]
    fn child_claim_process() {
        let Some(root) = std::env::var_os("KIRIN_IDENTITY_LEASE_CHILD_ROOT") else {
            return;
        };
        let (id, _lease) =
            InstanceIdentityLease::acquire(Path::new(&root), "pre", "cross-process").unwrap();
        assert_eq!(id, "cross-process");
        println!("LEASE_CHILD_READY");
        std::io::stdout().flush().unwrap();
        let mut byte = [0];
        let _ = std::io::stdin().read_exact(&mut byte);
    }

    #[test]
    fn cross_process_collision_and_abnormal_exit_use_kernel_ownership() {
        let root = tempfile::tempdir().unwrap();
        let mut child = Command::new(std::env::current_exe().unwrap())
            .args([
                "--exact",
                "identity_ffi::instance_identity_lease::tests::child_claim_process",
                "--nocapture",
            ])
            .env("KIRIN_IDENTITY_LEASE_CHILD_ROOT", root.path())
            .stdin(Stdio::piped())
            .stdout(Stdio::piped())
            .spawn()
            .unwrap();
        let mut reader = io::BufReader::new(child.stdout.take().unwrap());
        let mut line = String::new();
        loop {
            line.clear();
            assert_ne!(
                reader.read_line(&mut line).unwrap(),
                0,
                "child did not claim identity"
            );
            if line.contains("LEASE_CHILD_READY") {
                break;
            }
        }
        let (copied, _copy) =
            InstanceIdentityLease::acquire(root.path(), "pre", "cross-process").unwrap();
        assert_ne!(copied, "cross-process");
        child.kill().unwrap();
        child.wait().unwrap();
        assert_eq!(
            InstanceIdentityLease::acquire(root.path(), "pre", "cross-process")
                .unwrap()
                .0,
            "cross-process"
        );
    }
}
