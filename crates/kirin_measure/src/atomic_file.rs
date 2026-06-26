//! Small, shared atomic file writer for watch-state JSON.
//!
//! The writer uses a sibling temp file with a process-local unique suffix. Fixed
//! names such as `pre.json.tmp` can collide when a DAW briefly hosts duplicate
//! instances with the same restored id, causing one rename to consume another
//! writer's temp file.

use std::ffi::OsString;
use std::fs::{self, OpenOptions};
use std::io::{self, Write};
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicU64, Ordering};

static TMP_COUNTER: AtomicU64 = AtomicU64::new(0);

pub fn write_bytes_atomic(path: &Path, bytes: &[u8]) -> io::Result<()> {
    if let Some(parent) = path.parent() {
        fs::create_dir_all(parent)?;
    }

    let mut last_exists: Option<io::Error> = None;
    for _ in 0..16 {
        let tmp = unique_tmp_path(path)?;
        match OpenOptions::new().write(true).create_new(true).open(&tmp) {
            Ok(mut file) => {
                let result = (|| {
                    file.write_all(bytes)?;
                    file.flush()?;
                    drop(file);
                    fs::rename(&tmp, path)
                })();
                if result.is_err() {
                    let _ = fs::remove_file(&tmp);
                }
                return result;
            }
            Err(e) if e.kind() == io::ErrorKind::AlreadyExists => {
                last_exists = Some(e);
            }
            Err(e) => return Err(e),
        }
    }

    Err(last_exists.unwrap_or_else(|| {
        io::Error::new(
            io::ErrorKind::AlreadyExists,
            "could not allocate unique atomic temp path",
        )
    }))
}

pub fn remove_temp_siblings(path: &Path) -> io::Result<usize> {
    let Some(parent) = path.parent() else {
        return Ok(0);
    };
    let Some(file_name) = path.file_name() else {
        return Ok(0);
    };

    let final_name = file_name.to_string_lossy();
    let legacy_name = format!("{final_name}.tmp");
    let unique_prefix = format!(".{final_name}.tmp.");
    let entries = match fs::read_dir(parent) {
        Ok(entries) => entries,
        Err(e) if e.kind() == io::ErrorKind::NotFound => return Ok(0),
        Err(e) => return Err(e),
    };

    let mut removed = 0usize;
    for entry in entries {
        let entry = entry?;
        let name = entry.file_name();
        let name = name.to_string_lossy();
        if name == legacy_name || name.starts_with(&unique_prefix) {
            match fs::remove_file(entry.path()) {
                Ok(()) => removed += 1,
                Err(e) if e.kind() == io::ErrorKind::NotFound => {}
                Err(e) => return Err(e),
            }
        }
    }
    Ok(removed)
}

fn unique_tmp_path(path: &Path) -> io::Result<PathBuf> {
    let file_name = path.file_name().ok_or_else(|| {
        io::Error::new(
            io::ErrorKind::InvalidInput,
            "atomic write target has no file name",
        )
    })?;
    let n = TMP_COUNTER.fetch_add(1, Ordering::Relaxed);
    let mut tmp_name = OsString::from(".");
    tmp_name.push(file_name);
    tmp_name.push(format!(".tmp.{}.{}", std::process::id(), n));
    Ok(path.with_file_name(tmp_name))
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::{Arc, Barrier};
    use std::thread;

    fn isolated_dir(label: &str) -> PathBuf {
        let n = TMP_COUNTER.fetch_add(1, Ordering::Relaxed);
        let dir = std::env::temp_dir().join(format!(
            "kirin_atomic_file_{}_{}_{}",
            std::process::id(),
            n,
            label
        ));
        let _ = fs::remove_dir_all(&dir);
        fs::create_dir_all(&dir).unwrap();
        dir
    }

    fn tmp_entries(dir: &Path) -> Vec<String> {
        fs::read_dir(dir)
            .unwrap()
            .flatten()
            .filter_map(|entry| entry.file_name().into_string().ok())
            .filter(|name| name.contains(".tmp."))
            .collect()
    }

    #[test]
    fn atomic_write_creates_parent_and_leaves_no_tmp_after_success() {
        let root = isolated_dir("parent");
        let path = root.join("a").join("b").join("pre.json");

        write_bytes_atomic(&path, br#"{"ok":true}"#).unwrap();

        assert_eq!(fs::read_to_string(&path).unwrap(), r#"{"ok":true}"#);
        assert!(tmp_entries(path.parent().unwrap()).is_empty());
    }

    #[test]
    fn concurrent_writers_do_not_share_one_tmp_name() {
        let root = isolated_dir("concurrent");
        let path = Arc::new(root.join("post.json"));
        let barrier = Arc::new(Barrier::new(12));
        let mut handles = Vec::new();

        for n in 0..12 {
            let path = Arc::clone(&path);
            let barrier = Arc::clone(&barrier);
            handles.push(thread::spawn(move || {
                let payload = format!(r#"{{"writer":{n}}}"#);
                barrier.wait();
                write_bytes_atomic(&path, payload.as_bytes())
            }));
        }

        for handle in handles {
            handle.join().unwrap().unwrap();
        }

        let final_text = fs::read_to_string(&*path).unwrap();
        let parsed: serde_json::Value = serde_json::from_str(&final_text).unwrap();
        let writer = parsed.get("writer").and_then(serde_json::Value::as_u64);
        assert!(writer.is_some_and(|n| n < 12));
        assert!(tmp_entries(path.parent().unwrap()).is_empty());
    }

    #[test]
    fn target_without_file_name_is_rejected() {
        let err = write_bytes_atomic(Path::new("/"), b"x").unwrap_err();
        assert_eq!(err.kind(), io::ErrorKind::InvalidInput);
    }

    #[test]
    fn cleanup_removes_legacy_and_unique_tmp_siblings_only() {
        let root = isolated_dir("cleanup");
        let final_path = root.join("pre.json");
        fs::write(&final_path, b"live").unwrap();
        fs::write(root.join("pre.json.tmp"), b"old").unwrap();
        fs::write(root.join(".pre.json.tmp.123.1"), b"new").unwrap();
        fs::write(root.join(".post.json.tmp.123.1"), b"other").unwrap();

        let removed = remove_temp_siblings(&final_path).unwrap();

        assert_eq!(removed, 2);
        assert!(final_path.exists());
        assert!(!root.join("pre.json.tmp").exists());
        assert!(!root.join(".pre.json.tmp.123.1").exists());
        assert!(root.join(".post.json.tmp.123.1").exists());
    }
}
