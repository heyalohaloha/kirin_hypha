use std::collections::HashSet;
use std::fs;
use std::io::Write;
use std::path::{Path, PathBuf};

use sha2::{Digest, Sha256};
use tempfile::NamedTempFile;

pub(crate) fn sha256_bytes(bytes: &[u8]) -> String {
    hex::encode(Sha256::digest(bytes))
}

pub(crate) fn write_new_atomic(path: &Path, bytes: &[u8]) -> Result<(), String> {
    write_bundle_create_new(&[(path, bytes)])
}

pub(crate) fn write_bundle_create_new(outputs: &[(&Path, &[u8])]) -> Result<(), String> {
    if outputs.is_empty() {
        return Err("output bundle is empty".to_string());
    }
    let mut targets = HashSet::new();
    let mut temporary = Vec::with_capacity(outputs.len());
    for &(path, bytes) in outputs {
        let target = canonical_target(path)?;
        if !targets.insert(target.clone()) {
            return Err(format!("duplicate output target: {}", path.display()));
        }
        if target.exists() {
            return Err(format!("output already exists: {}", path.display()));
        }
        let parent = target.parent().ok_or("output has no parent directory")?;
        let mut file = NamedTempFile::new_in(parent)
            .map_err(|error| format!("cannot create output temporary file: {error}"))?;
        file.write_all(bytes)
            .and_then(|()| file.as_file().sync_all())
            .map_err(|error| format!("cannot write output temporary file: {error}"))?;
        temporary.push((target, file));
    }

    let mut persisted = Vec::<PathBuf>::new();
    for (target, file) in temporary {
        if let Err(error) = file.persist_noclobber(&target) {
            for path in &persisted {
                let _ = fs::remove_file(path);
            }
            return Err(format!(
                "cannot atomically create {}: {}",
                target.display(),
                error.error
            ));
        }
        persisted.push(target);
    }
    Ok(())
}

fn canonical_target(path: &Path) -> Result<PathBuf, String> {
    let name = path
        .file_name()
        .ok_or_else(|| format!("output path has no filename: {}", path.display()))?;
    let parent = path
        .parent()
        .filter(|parent| !parent.as_os_str().is_empty())
        .unwrap_or_else(|| Path::new("."));
    let parent = fs::canonicalize(parent)
        .map_err(|error| format!("output parent {}: {error}", parent.display()))?;
    if !parent.is_dir() {
        return Err(format!(
            "output parent is not a directory: {}",
            parent.display()
        ));
    }
    Ok(parent.join(name))
}

#[cfg(test)]
mod tests {
    use super::*;
    use tempfile::tempdir;

    #[test]
    fn outputs_are_create_new_and_bundle_targets_are_distinct() {
        let directory = tempdir().unwrap();
        let path = directory.path().join("result.json");
        write_new_atomic(&path, b"first").unwrap();
        assert_eq!(fs::read(&path).unwrap(), b"first");
        assert!(write_new_atomic(&path, b"second")
            .unwrap_err()
            .contains("already exists"));
        assert!(write_bundle_create_new(&[(&path, b"a"), (&path, b"b")]).is_err());
    }
}
