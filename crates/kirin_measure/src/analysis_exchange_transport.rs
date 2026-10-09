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
    LocalBlindRequest,
    LocalBlindArmed,
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

/// Inspect only a fixed header before deciding whether a payload belongs to this request.
/// Foreign snapshots never need their large body copied or decoded by the PRE cleanup path.
pub(super) fn read_prefix(
    instance_dir: &Path,
    fallback_path: &Path,
    slot: AnalysisSlot,
    prefix_bytes: usize,
    maximum_bytes: u64,
) -> Option<Vec<u8>> {
    #[cfg(windows)]
    {
        let _ = fallback_path;
        windows::read_prefix(instance_dir, slot, prefix_bytes, maximum_bytes)
    }
    #[cfg(not(windows))]
    {
        use std::io::Read;
        let _ = (instance_dir, slot);
        let file = std::fs::File::open(fallback_path).ok()?;
        let length = file.metadata().ok()?.len();
        if length == 0 || length > maximum_bytes {
            return None;
        }
        let limit = length.min(prefix_bytes as u64);
        let mut bytes = Vec::with_capacity(limit as usize);
        file.take(limit).read_to_end(&mut bytes).ok()?;
        (bytes.len() as u64 == limit).then_some(bytes)
    }
}

#[cfg(windows)]
#[path = "analysis_exchange_windows.rs"]
mod windows;

#[cfg(test)]
#[path = "analysis_exchange_prefix_tests.rs"]
mod prefix_tests;
