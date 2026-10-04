//! Non-RT Blind barrier. A local PRE/POST Blind and a VERSION BLIND take it exclusively, so only one
//! Blind runs per process and per project. Reference auditions never take it. Reference A capture,
//! the only shared holder, was removed in B-1190; the file names are kept so mixed builds still
//! exclude each other.
use super::*;

#[derive(Debug)]
pub(super) struct CaptureBarrier {
    paths: [PathBuf; 2],
    files: Vec<File>,
}
impl CaptureBarrier {
    pub(super) fn new(process: PathBuf, project: PathBuf) -> Self {
        Self {
            paths: [process, project],
            files: Vec::new(),
        }
    }
    pub(super) fn acquire(&mut self) -> io::Result<bool> {
        if !self.files.is_empty() {
            return Ok(true);
        }
        let mut held = Vec::new();
        for path in &self.paths {
            std::fs::create_dir_all(path.parent().unwrap())?;
            let f = OpenOptions::new()
                .read(true)
                .write(true)
                .create(true)
                .truncate(false)
                .open(path)?;
            match f.try_lock() {
                Ok(()) => held.push(f),
                Err(TryLockError::WouldBlock) => return Ok(false),
                Err(TryLockError::Error(e)) => return Err(e),
            }
        }
        self.files = held;
        Ok(true)
    }
    pub(super) fn release(&mut self) {
        self.files.clear();
    }
}

/// Held for the whole VERSION BLIND; dropping it releases the barrier.
#[derive(Debug)]
pub struct BlindCaptureExclusion(CaptureBarrier);
impl BlindCaptureExclusion {
    #[cfg(not(test))]
    pub fn for_current_project(root: &Path, project: &str) -> Self {
        Self(AuditionAdmission::for_current_project(root, project).capture_barrier)
    }
    pub fn acquire(&mut self) -> bool {
        self.0.acquire().unwrap_or(false)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn version_blind_and_local_blind_exclude_each_other_but_not_reference() {
        let tmp = tempfile::tempdir().unwrap();
        let paths = [tmp.path().join("a"), tmp.path().join("b")];
        let make = |project: &str| {
            AuditionAdmission::at_paths(
                paths.clone(),
                tmp.path().join("audition"),
                tmp.path(),
                project,
            )
        };
        let exclusion = |project: &str| BlindCaptureExclusion(make(project).capture_barrier);
        let mut reference = make("song");
        assert!(reference.try_acquire_for("Reference").unwrap());
        let mut version = exclusion("song");
        assert!(version.acquire()); // VERSION BLIND runs inside its Reference audition.
        assert!(version.acquire());
        assert!(!exclusion("song").acquire());
        reference.release();
        let mut local = make("song");
        assert!(!local.try_acquire_blind_for("PRE POST Blind").unwrap());
        drop(version);
        assert!(local.try_acquire_blind_for("PRE POST Blind").unwrap());
        assert!(!exclusion("song").acquire());
        assert!(!exclusion("other song").acquire()); // One Blind per process.
        local.release();
        assert!(exclusion("other song").acquire());
    }
}
