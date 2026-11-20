//! Low-frequency coordination entries only. Preserve syscall errors; sync is best effort.
use std::{fs, io, path::Path};
pub(crate) fn remove_file(path: impl AsRef<Path>) -> io::Result<()> {
    let path = path.as_ref();
    fs::remove_file(path)?;
    super::sync_parent_directory(path);
    Ok(())
}
pub(crate) fn hard_link(from: impl AsRef<Path>, to: impl AsRef<Path>) -> io::Result<()> {
    let to = to.as_ref();
    fs::hard_link(from, to)?;
    super::sync_parent_directory(to);
    Ok(())
}
/// Both directory entries change; this does not sync the file content.
pub(crate) fn rename(from: impl AsRef<Path>, to: impl AsRef<Path>) -> io::Result<()> {
    let from = from.as_ref();
    let to = to.as_ref();
    fs::rename(from, to)?;
    super::sync_parent_directory(to);
    if from.parent() != to.parent() {
        super::sync_parent_directory(from);
    }
    Ok(())
}
#[cfg(test)]
#[path = "atomic_directory_mutation_tests.rs"]
mod tests;
