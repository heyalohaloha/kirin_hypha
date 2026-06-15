use anyhow::{bail, Context, Result};
use std::path::{Component, Path, PathBuf};
use std::process::Command;

pub const UNSIGNED_SUFFIX: &str = "UNSIGNED-DO-NOT-UPLOAD";

pub fn verify_package_mode(dist_dir: &Path, allow_unsigned: bool) -> Result<()> {
    if allow_unsigned {
        return require_temp_dist(dist_dir);
    }

    let dirty = git_dirty_entries()?;
    if !dirty.is_empty() {
        bail!(
            "release-package requires a clean source worktree before producing an uploadable zip.\n\
             Commit/stash these entries first:\n{}",
            dirty.join("\n")
        );
    }
    Ok(())
}

pub fn git_dirty_for_manifest() -> String {
    match git_dirty_entries() {
        Ok(entries) if entries.is_empty() => "false".to_string(),
        Ok(entries) => format!("true: {}", entries.join("; ")),
        Err(err) => format!("unknown: {err}"),
    }
}

fn require_temp_dist(dist_dir: &Path) -> Result<()> {
    let normalized = normalized_absolute(dist_dir)?;
    if normalized.starts_with(normalized_absolute(&std::env::temp_dir())?)
        || normalized.starts_with(Path::new("/tmp"))
        || normalized.starts_with(Path::new("/private/tmp"))
    {
        return Ok(());
    }
    bail!(
        "--allow-unsigned is for local smoke tests only and must write under /tmp.\n\
         Use e.g. --dist-dir /tmp/kirin-hypha-release-smoke\n\
         Resolved dist dir: {}",
        normalized.display()
    );
}

fn git_dirty_entries() -> Result<Vec<String>> {
    let out = Command::new("git")
        .args(["status", "--porcelain=1", "--untracked-files=all"])
        .output()
        .context("spawn git status for release cleanliness check")?;
    if !out.status.success() {
        bail!(
            "git status failed: {}",
            String::from_utf8_lossy(&out.stderr).trim()
        );
    }

    Ok(String::from_utf8_lossy(&out.stdout)
        .lines()
        .filter_map(|line| {
            let path = line.get(3..).unwrap_or(line).trim();
            if path == "juce_shell/JUCE" {
                None
            } else {
                Some(line.to_string())
            }
        })
        .collect())
}

fn normalized_absolute(path: &Path) -> Result<PathBuf> {
    let absolute = if path.is_absolute() {
        path.to_path_buf()
    } else {
        std::env::current_dir()
            .context("resolve current directory for release dist path")?
            .join(path)
    };
    let mut normalized = PathBuf::new();
    for component in absolute.components() {
        match component {
            Component::Prefix(prefix) => normalized.push(prefix.as_os_str()),
            Component::RootDir => normalized.push(Path::new("/")),
            Component::CurDir => {}
            Component::ParentDir => {
                if !normalized.pop() {
                    bail!("dist path escapes above filesystem root: {}", path.display());
                }
            }
            Component::Normal(part) => normalized.push(part),
        }
    }
    Ok(normalized)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn unsigned_suffix_is_loud() {
        assert_eq!(UNSIGNED_SUFFIX, "UNSIGNED-DO-NOT-UPLOAD");
    }

    #[test]
    fn temp_dist_allows_unsigned_smoke() {
        let dist = std::env::temp_dir().join("kirin-hypha-release-smoke");
        verify_package_mode(&dist, true).unwrap();
    }

    #[test]
    fn repo_dist_rejects_unsigned_smoke() {
        let err = verify_package_mode(Path::new("dist"), true).unwrap_err();
        assert!(err.to_string().contains("--allow-unsigned"));
    }

    #[test]
    fn temp_parent_escape_rejects_unsigned_smoke() {
        let err = verify_package_mode(Path::new("/tmp/../Users/kirin-hypha-release-smoke"), true)
            .unwrap_err();
        assert!(err.to_string().contains("--allow-unsigned"));
    }
}
