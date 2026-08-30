use std::fs::{self, File, OpenOptions};
use std::io::Write;
use std::path::{Path, PathBuf};

use crate::output::{ArtifactFile, Artifacts};

#[derive(Debug)]
pub(crate) struct WrittenPaths {
    pub(crate) receipt: PathBuf,
}

pub(crate) fn publish_artifacts_create_new(
    target: &Path,
    artifacts: &Artifacts,
) -> Result<WrittenPaths, String> {
    if target.exists() {
        return Err(format!("output already exists: {}", target.display()));
    }
    let parent = target.parent().ok_or("output has no parent directory")?;
    if !parent.is_dir() {
        return Err(format!(
            "output parent is not a directory: {}",
            parent.display()
        ));
    }
    let name = target
        .file_name()
        .and_then(|name| name.to_str())
        .ok_or("output directory name is not valid UTF-8")?;
    let staging = create_staging(parent, name)?;
    let transaction = write_staging(&staging, artifacts)
        .and_then(|_| sync_directory(&staging))
        .and_then(|_| {
            if target.exists() {
                Err(format!(
                    "output appeared during publish: {}",
                    target.display()
                ))
            } else {
                fs::rename(&staging, target)
                    .map_err(|error| format!("cannot publish {}: {error}", target.display()))
            }
        })
        .and_then(|_| sync_directory(parent));
    if let Err(error) = transaction {
        let _ = fs::remove_dir_all(&staging);
        return Err(error);
    }
    Ok(WrittenPaths {
        receipt: target.join("attack_drum_development_receipt_v1.json"),
    })
}

fn create_staging(parent: &Path, name: &str) -> Result<PathBuf, String> {
    for attempt in 0..100_u8 {
        let path = parent.join(format!(".{name}.staging-{}-{attempt}", std::process::id()));
        match fs::create_dir(&path) {
            Ok(()) => return Ok(path),
            Err(error) if error.kind() == std::io::ErrorKind::AlreadyExists => continue,
            Err(error) => return Err(format!("cannot create staging directory: {error}")),
        }
    }
    Err("cannot allocate unique staging directory".to_string())
}

fn write_staging(directory: &Path, artifacts: &Artifacts) -> Result<(), String> {
    for ArtifactFile { name, bytes } in &artifacts.files {
        if Path::new(name).components().count() != 1 {
            return Err(format!("invalid artifact name: {name}"));
        }
        let path = directory.join(name);
        let mut file = OpenOptions::new()
            .create_new(true)
            .write(true)
            .open(&path)
            .map_err(|error| format!("cannot create {}: {error}", path.display()))?;
        file.write_all(bytes)
            .and_then(|_| file.sync_all())
            .map_err(|error| format!("cannot persist {}: {error}", path.display()))?;
    }
    Ok(())
}

fn sync_directory(path: &Path) -> Result<(), String> {
    File::open(path)
        .and_then(|directory| directory.sync_all())
        .map_err(|error| format!("cannot sync directory {}: {error}", path.display()))
}

#[cfg(test)]
mod tests {
    use super::*;
    use tempfile::tempdir;

    fn artifacts() -> Artifacts {
        Artifacts {
            files: vec![ArtifactFile {
                name: "attack_drum_development_receipt_v1.json".into(),
                bytes: b"receipt".to_vec(),
            }],
        }
    }

    #[test]
    fn publish_is_directory_atomic_and_refuses_existing_target() {
        let parent = tempdir().unwrap();
        let target = parent.path().join("final");
        publish_artifacts_create_new(&target, &artifacts()).unwrap();
        assert_eq!(
            fs::read(target.join("attack_drum_development_receipt_v1.json")).unwrap(),
            b"receipt"
        );
        assert!(publish_artifacts_create_new(&target, &artifacts())
            .unwrap_err()
            .contains("already exists"));
    }

    #[test]
    fn failed_staging_leaves_no_final_or_partial_directory() {
        let parent = tempdir().unwrap();
        let target = parent.path().join("final");
        let artifacts = Artifacts {
            files: vec![ArtifactFile {
                name: "../escape".into(),
                bytes: Vec::new(),
            }],
        };
        assert!(publish_artifacts_create_new(&target, &artifacts).is_err());
        assert!(!target.exists());
        assert_eq!(fs::read_dir(parent.path()).unwrap().count(), 0);
    }
}
