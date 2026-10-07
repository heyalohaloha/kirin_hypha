//! A test's own folder for every place Kirin OS storage resolves from: HOME, TMPDIR, and the Kirin
//! OS test sandbox that storage reads first (kirin_measure `TEST_STORAGE_ROOT_ENV`). All sit under the
//! same root, laid out as storage expects (`home`, `tmp`), so a path a test builds from `home` or
//! `tmp` is where the code under test writes.

use std::path::Path;

/// Points HOME at `home`, TMPDIR at `tmp` when given, and the Kirin OS sandbox at their folder.
pub fn isolate(home: &Path, tmp: Option<&Path>) {
    std::env::set_var("HOME", home);
    let root = home.parent().expect("home sits in the test's own folder");
    std::env::set_var(kirin_measure::TEST_STORAGE_ROOT_ENV, root);
    if let Some(tmp) = tmp {
        std::env::set_var("TMPDIR", tmp);
    }
}

/// Removes every place storage resolves from, as on a host without HOME, APPDATA or LOCALAPPDATA.
#[allow(dead_code)] // pairing_candidates does not use it
pub fn unresolvable() {
    for name in [
        "HOME",
        "APPDATA",
        "LOCALAPPDATA",
        kirin_measure::TEST_STORAGE_ROOT_ENV,
    ] {
        std::env::remove_var(name);
    }
}
