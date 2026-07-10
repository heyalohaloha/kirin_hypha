//! Shared atomic claim primitive for disk-backed cross-process ownership.
//!
//! Claim users write complete JSON to a same-directory temp file, then hard-link
//! that temp inode to the final claim path. `AlreadyExists` from `hard_link`
//! means another process won the claim; no final path is visible until it is
//! complete.

use std::fs::{self, OpenOptions};
use std::io::{self, Write};
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicU64, Ordering};

static TEMP_SEQ: AtomicU64 = AtomicU64::new(0);

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) enum ClaimOutcome {
    Created,
    AlreadyClaimed,
}

pub(crate) fn build_claim_temp(
    dir: &Path,
    temp_name_prefix: &str,
    bytes: &[u8],
) -> io::Result<PathBuf> {
    let seq = TEMP_SEQ.fetch_add(1, Ordering::Relaxed);
    let temp = dir.join(format!(
        ".{temp_name_prefix}.tmp.{}.{}",
        std::process::id(),
        seq
    ));
    let res = (|| -> io::Result<()> {
        let mut f = OpenOptions::new()
            .write(true)
            .create_new(true)
            .open(&temp)?;
        f.write_all(bytes)?;
        f.sync_all()?;
        Ok(())
    })();
    match res {
        Ok(()) => Ok(temp),
        Err(e) => {
            let _ = fs::remove_file(&temp);
            Err(e)
        }
    }
}

pub(crate) fn link_claim(temp: &Path, final_path: &Path) -> io::Result<ClaimOutcome> {
    let outcome = match fs::hard_link(temp, final_path) {
        Ok(()) => Ok(ClaimOutcome::Created),
        Err(e) if e.kind() == io::ErrorKind::AlreadyExists => Ok(ClaimOutcome::AlreadyClaimed),
        Err(e) => Err(e),
    };
    let _ = fs::remove_file(temp);
    outcome
}
