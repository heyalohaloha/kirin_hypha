//! Scope every storage resolver to a disposable test folder. Run environment-mutating tests
//! serially (the workspace Cargo configuration sets RUST_TEST_THREADS=1).

use std::ffi::OsString;
use std::path::Path;

const STORAGE_ENV: [&str; 5] = [
    "HOME",
    "TMPDIR",
    "APPDATA",
    "LOCALAPPDATA",
    kirin_measure::TEST_STORAGE_ROOT_ENV,
];

#[must_use = "keep the sandbox guard alive until all engines have been dropped"]
pub struct StorageSandbox {
    previous: Vec<(&'static str, Option<OsString>)>,
}

impl StorageSandbox {
    fn capture() -> Self {
        Self {
            previous: STORAGE_ENV
                .into_iter()
                .map(|name| (name, std::env::var_os(name)))
                .collect(),
        }
    }
}

impl Drop for StorageSandbox {
    fn drop(&mut self) {
        for (name, value) in &self.previous {
            match value {
                Some(value) => std::env::set_var(name, value),
                None => std::env::remove_var(name),
            }
        }
    }
}

/// Restore all original values, including absent/non-Unicode values, when the guard drops.
pub fn isolate(home: &Path, tmp: Option<&Path>) -> StorageSandbox {
    let guard = StorageSandbox::capture();
    std::env::set_var("HOME", home);
    let root = home.parent().expect("home sits in the test's own folder");
    std::env::set_var(kirin_measure::TEST_STORAGE_ROOT_ENV, root);
    if let Some(tmp) = tmp {
        std::env::set_var("TMPDIR", tmp);
    }
    guard
}

/// Temporarily model a host with no resolvable storage. Restore the enclosing sandbox on Drop.
#[allow(dead_code)] // pairing_candidates does not use it
pub fn unresolvable() -> StorageSandbox {
    let guard = StorageSandbox::capture();
    for name in STORAGE_ENV.into_iter().filter(|name| *name != "TMPDIR") {
        std::env::remove_var(name);
    }
    guard
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn restores_resolvers_after_nested_scope_and_unwind() {
        let original = STORAGE_ENV.map(std::env::var_os);
        let temp = std::env::temp_dir();
        let root = temp.join(format!("kirin-storage-guard-{}", std::process::id()));
        {
            let _sandbox = isolate(&root.join("home"), Some(&root.join("tmp")));
            let scoped = STORAGE_ENV.map(std::env::var_os);
            let result = std::panic::catch_unwind(|| {
                let _missing = unresolvable();
                assert!(std::env::var_os("HOME").is_none());
                panic!("exercise unwinding");
            });
            assert!(result.is_err());
            assert_eq!(STORAGE_ENV.map(std::env::var_os), scoped);
        }
        assert_eq!(STORAGE_ENV.map(std::env::var_os), original);
        assert_eq!(
            std::env::temp_dir(),
            temp,
            "next test must not nest under this test"
        );
    }
}
