//! Platform path callers must go through `default_platform()`.

use std::fs;
use std::path::{Path, PathBuf};

fn workspace_root() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .parent()
        .and_then(Path::parent)
        .expect("workspace root")
        .to_path_buf()
}

fn collect_rs_files(dir: &Path, out: &mut Vec<PathBuf>) {
    let Ok(entries) = fs::read_dir(dir) else {
        return;
    };
    for entry in entries.flatten() {
        let path = entry.path();
        if path.is_dir() {
            collect_rs_files(&path, out);
        } else if path.extension().and_then(|s| s.to_str()) == Some("rs") {
            out.push(path);
        }
    }
}

fn collect_crate_src_rs_files(root: &Path) -> Vec<PathBuf> {
    let mut out = Vec::new();
    let crates_dir = root.join("crates");
    let entries = fs::read_dir(&crates_dir).expect("read crates dir");
    for entry in entries.flatten() {
        let src = entry.path().join("src");
        if src.is_dir() {
            collect_rs_files(&src, &mut out);
        }
    }
    out
}

#[test]
fn storage_default_macos_is_wrapper_only() {
    let root = workspace_root();
    let mut files = Vec::new();
    collect_rs_files(&root.join("crates"), &mut files);

    let allowed = root.join("crates/kirin_measure/src/storage.rs");
    let forbidden = ["StoragePaths::default_", "macos()"].concat();
    let mut offenders = Vec::new();

    for path in files {
        if path == allowed {
            continue;
        }
        let src = fs::read_to_string(&path).unwrap_or_else(|e| panic!("read {path:?}: {e}"));
        if src.contains(&forbidden) {
            offenders.push(
                path.strip_prefix(&root)
                    .unwrap_or(&path)
                    .display()
                    .to_string(),
            );
        }
    }

    assert!(
        offenders.is_empty(),
        "storage callers must use StoragePaths::default_platform(); offenders: {offenders:?}"
    );
}

#[test]
fn kirin_tmp_root_is_platform_helper_only_in_production_sources() {
    let root = workspace_root();
    let allowed = root.join("crates/kirin_measure/src/storage.rs");
    let forbidden = ["std::env::temp_dir()", ".join(\"", "kirin", "\")"].concat();
    let mut offenders = Vec::new();

    for path in collect_crate_src_rs_files(&root) {
        if path == allowed {
            continue;
        }
        let src = fs::read_to_string(&path).unwrap_or_else(|e| panic!("read {path:?}: {e}"));
        if src.contains(&forbidden) {
            offenders.push(
                path.strip_prefix(&root)
                    .unwrap_or(&path)
                    .display()
                    .to_string(),
            );
        }
    }

    assert!(
        offenders.is_empty(),
        "production watch root callers must use PlatformPaths::current_kirin_tmp_root(); offenders: {offenders:?}"
    );
}
