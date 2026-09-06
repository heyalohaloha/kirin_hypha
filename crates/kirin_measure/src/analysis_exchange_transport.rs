//! Platform transport for the optional PRE/POST Analysis exchange.
//!
//! Watch and Record keep their existing file contracts. Analysis is a high-rate, ephemeral
//! exchange: Windows uses a pagefile-backed named mapping so filesystem filter latency cannot
//! expire a healthy request. Other platforms retain the atomic-file implementation.

use std::io;
use std::path::Path;

#[derive(Clone, Copy, Debug)]
pub(super) enum AnalysisSlot {
    Request,
    Ready,
    Spectrum,
    Perceptual,
    Attack,
}

pub(super) fn write(
    instance_dir: &Path,
    fallback_path: &Path,
    slot: AnalysisSlot,
    bytes: &[u8],
) -> io::Result<()> {
    #[cfg(windows)]
    {
        let _ = fallback_path;
        windows::write(instance_dir, slot, bytes)
    }
    #[cfg(not(windows))]
    {
        let _ = (instance_dir, slot);
        crate::atomic_file::write_bytes_atomic(fallback_path, bytes)
    }
}

pub(super) fn read(
    instance_dir: &Path,
    fallback_path: &Path,
    slot: AnalysisSlot,
    maximum_bytes: u64,
) -> Option<Vec<u8>> {
    #[cfg(windows)]
    {
        let _ = fallback_path;
        windows::read(instance_dir, slot, maximum_bytes)
    }
    #[cfg(not(windows))]
    {
        let _ = (instance_dir, slot);
        super::spectrum_exchange::codec::read_bounded(fallback_path, maximum_bytes)
    }
}

pub(super) fn remove(
    instance_dir: &Path,
    fallback_path: &Path,
    slot: AnalysisSlot,
) -> io::Result<()> {
    #[cfg(windows)]
    {
        let _ = fallback_path;
        windows::clear(instance_dir, slot)
    }
    #[cfg(not(windows))]
    {
        let _ = (instance_dir, slot);
        match std::fs::remove_file(fallback_path) {
            Ok(()) => Ok(()),
            Err(error) if error.kind() == io::ErrorKind::NotFound => Ok(()),
            Err(error) => Err(error),
        }
    }
}

#[cfg(windows)]
#[path = "analysis_exchange_windows.rs"]
mod windows;
