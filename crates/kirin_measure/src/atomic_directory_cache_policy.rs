//! Cache eligibility, not an assertion of crash durability or inode uniqueness.
use std::path::Path;

pub(super) fn permits_cache(directory: &Path) -> bool {
    #[cfg(test)]
    if let Some(value) = fixture::decision(directory) {
        return value;
    }
    platform_permits_cache(directory)
}

#[cfg(target_os = "macos")]
fn platform_permits_cache(directory: &Path) -> bool {
    use std::ffi::CString;
    use std::os::unix::ffi::OsStrExt;
    let Ok(path) = CString::new(directory.as_os_str().as_bytes()) else {
        return false;
    };
    let mut info = std::mem::MaybeUninit::<libc::statfs>::uninit();
    // Query this ancestor, not a previously queried leaf (mounts may differ).
    if unsafe { libc::statfs(path.as_ptr(), info.as_mut_ptr()) } != 0 {
        return false;
    }
    let info = unsafe { info.assume_init() };
    info.f_fstypename
        .iter()
        .take(5)
        .map(|c| *c as u8)
        .eq(b"apfs\0".iter().copied())
}
#[cfg(not(target_os = "macos"))]
fn platform_permits_cache(_directory: &Path) -> bool {
    false
}

#[cfg(test)]
pub(super) mod fixture {
    use std::cell::RefCell;
    use std::path::{Path, PathBuf};
    #[derive(Clone)]
    struct Policy {
        fallback: bool,
        exceptions: Vec<(PathBuf, bool)>,
    }
    thread_local! { static POLICY: RefCell<Option<Policy>> = const { RefCell::new(None) }; }
    pub(super) fn decision(path: &Path) -> Option<bool> {
        POLICY.with_borrow(|policy| {
            policy.as_ref().map(|p| {
                p.exceptions
                    .iter()
                    .find(|(name, _)| name == path)
                    .map_or(p.fallback, |(_, v)| *v)
            })
        })
    }
    pub(in super::super) struct Scope(Option<Policy>);
    impl Scope {
        pub(in super::super) fn new(fallback: bool, exceptions: Vec<(PathBuf, bool)>) -> Self {
            Self(POLICY.replace(Some(Policy {
                fallback,
                exceptions,
            })))
        }
    }
    impl Drop for Scope {
        fn drop(&mut self) {
            POLICY.set(self.0.take());
        }
    }
}
