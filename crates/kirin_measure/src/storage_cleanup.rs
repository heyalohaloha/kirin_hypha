//! One-time cleanup for the legacy Phase 1 storage layout.

use super::{PlatformPaths, StoragePaths};
use std::fs;

/// `~/Library/Application Support/Kirin OS/.cleanup_v1_done` flag filename.
pub const CLEANUP_V1_DONE_FILENAME: &str = ".cleanup_v1_done";

/// Remove the legacy `default/MIX` and `default/preset` trees once.
///
/// Failures are reported in the return value and logs. They do not stop startup.
pub fn cleanup_legacy_v1(paths: &StoragePaths) -> CleanupReport {
    let flag = paths.kirin_os_root.join(CLEANUP_V1_DONE_FILENAME);
    if flag.exists() {
        return CleanupReport {
            ran: false,
            removed: 0,
            errors: 0,
        };
    }

    let mut removed = 0usize;
    let mut errors = 0usize;

    let plugin_data = paths.plugin_data_dir();
    let legacy_mix = plugin_data.join("default").join("MIX");
    let legacy_preset = plugin_data.join("default").join("preset");
    let legacy_tmp = PlatformPaths::current_kirin_tmp_root()
        .join("default")
        .join("MIX");

    for target in [&legacy_mix, &legacy_preset, &legacy_tmp] {
        if !target.exists() {
            continue;
        }
        match fs::remove_dir_all(target) {
            Ok(()) => {
                log::info!("[cleanup_v1] removed: {}", target.display());
                removed += 1;
            }
            Err(error) => {
                log::warn!(
                    "[cleanup_v1] failed to remove {}: {}",
                    target.display(),
                    error
                );
                errors += 1;
            }
        }
    }

    let legacy_default = plugin_data.join("default");
    if legacy_default.exists() {
        let _ = fs::remove_dir(&legacy_default);
    }
    let legacy_tmp_default = PlatformPaths::current_kirin_tmp_root().join("default");
    if legacy_tmp_default.exists() {
        let _ = fs::remove_dir(&legacy_tmp_default);
    }

    if let Some(parent) = flag.parent() {
        let _ = fs::create_dir_all(parent);
    }
    if let Err(error) = fs::write(&flag, b"cleanup v1 completed\n") {
        log::warn!(
            "[cleanup_v1] failed to write flag {}: {}",
            flag.display(),
            error
        );
        errors += 1;
    }

    CleanupReport {
        ran: true,
        removed,
        errors,
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct CleanupReport {
    pub ran: bool,
    pub removed: usize,
    pub errors: usize,
}
