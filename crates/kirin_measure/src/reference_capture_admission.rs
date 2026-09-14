//! Non-RT Capture intent. Shared capture barriers exclude Blind without suspending PRE delta.
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
    pub(super) fn acquire(&mut self, shared: bool) -> io::Result<bool> {
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
            let result = if shared {
                f.try_lock_shared()
            } else {
                f.try_lock()
            };
            match result {
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

#[derive(Debug)]
pub struct BlindCaptureExclusion(CaptureBarrier);
impl BlindCaptureExclusion {
    #[cfg(not(test))]
    pub fn for_current_project(root: &Path, project: &str) -> Self {
        Self(AuditionAdmission::for_current_project(root, project).capture_barrier)
    }
    pub fn acquire(&mut self) -> bool {
        self.0.acquire(false).unwrap_or(false)
    }
}
#[derive(Debug)]
pub struct CaptureAdmission {
    owner: AuditionAdmission,
    pub(super) held: bool,
}
impl CaptureAdmission {
    #[cfg(not(test))]
    pub fn for_current_project(root: &Path, project: &str) -> Self {
        Self {
            owner: AuditionAdmission::for_current_project(root, project),
            held: false,
        }
    }
    pub(super) fn compatible(&self, audition: &AuditionAdmission) -> bool {
        self.held
            && self.owner.capture_barrier.paths == audition.capture_barrier.paths
            && self.owner.analysis.paths == audition.analysis.paths
    }
    pub fn try_acquire(&mut self, audition: Option<&mut AuditionAdmission>) -> io::Result<bool> {
        if self.held {
            return Ok(true);
        }
        if !self.owner.capture_barrier.acquire(true)? {
            return Ok(false);
        }
        if let Some(a) = audition.filter(|a| a.held) {
            if a.borrowed_analysis
                || self.owner.capture_barrier.paths != a.capture_barrier.paths
                || self.owner.analysis.paths != a.analysis.paths
            {
                self.owner.capture_barrier.release();
                return Ok(false);
            }
            std::mem::swap(&mut self.owner.analysis, &mut a.analysis);
            a.borrowed_analysis = true;
        } else {
            match self.owner.analysis.try_acquire_for("Capture A") {
                Ok(true) => {}
                other => {
                    self.owner.capture_barrier.release();
                    return other;
                }
            }
        }
        self.held = true;
        Ok(true)
    }
    pub fn try_acquire_shared(
        &mut self,
        owner: &reference_owner::ReferenceAnalysisOwner,
    ) -> io::Result<bool> {
        if self.held {
            return Ok(true);
        }
        if !owner.compatible(&self.owner.analysis.paths)
            || !self.owner.capture_barrier.acquire(true)?
        {
            return Ok(false);
        }
        let Some(grant) = owner.acquire() else {
            self.owner.capture_barrier.release();
            return Ok(false);
        };
        self.owner.shared_analysis = Some(grant);
        self.held = true;
        Ok(true)
    }
    pub fn release(&mut self, audition: Option<&mut AuditionAdmission>) {
        if !self.held {
            return;
        }
        if let Some(a) = audition.filter(|a| a.held && a.borrowed_analysis) {
            std::mem::swap(&mut self.owner.analysis, &mut a.analysis);
            a.borrowed_analysis = false;
        }
        self.owner.release();
        self.held = false;
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn capture_shares_slots_but_never_suspends_measurement_or_allows_blind() {
        let tmp = tempfile::tempdir().unwrap();
        let paths = [tmp.path().join("a"), tmp.path().join("b")];
        let make = || {
            AuditionAdmission::at_paths(
                paths.clone(),
                tmp.path().join("audition"),
                tmp.path(),
                "song",
            )
        };
        let mut cap = CaptureAdmission {
            owner: make(),
            held: false,
        };
        let mut cap2 = CaptureAdmission {
            owner: make(),
            held: false,
        };
        let mut cap3 = CaptureAdmission {
            owner: make(),
            held: false,
        };
        assert!(cap.try_acquire(None).unwrap());
        assert!(cap2.try_acquire(None).unwrap());
        assert!(!cap3.try_acquire(None).unwrap());
        let mut a = make();
        assert!(!a.try_acquire_blind_for("Blind").unwrap());
        assert!(a.try_acquire_during_capture(&cap, "Reference").unwrap());
        cap.release(Some(&mut a)); // Output keeps the same kernel slot.
        assert!(!cap3.try_acquire(None).unwrap());
        assert!(cap.try_acquire(Some(&mut a)).unwrap());
        a.release(); // A return does not terminate capture.
        assert!(!cap3.try_acquire(None).unwrap());
        cap.release(None);
        cap2.release(None);
        assert!(a.try_acquire_blind_for("Blind").unwrap());
        assert!(!cap.try_acquire(None).unwrap());
        a.release();
        assert!(cap.try_acquire(None).unwrap());
        assert!(cap2.try_acquire(None).unwrap());
        assert!(!a.try_acquire_for("Reference without borrow").unwrap());
        let mut foreign = AuditionAdmission::at_paths(
            paths.clone(),
            tmp.path().join("audition"),
            tmp.path(),
            "other song",
        );
        assert!(!foreign
            .try_acquire_during_capture(&cap, "Reference")
            .unwrap());
    }
}
