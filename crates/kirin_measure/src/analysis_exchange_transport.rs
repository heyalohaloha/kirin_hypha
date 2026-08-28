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
mod windows {
    use std::cell::UnsafeCell;
    use std::collections::HashMap;
    use std::ffi::c_void;
    use std::io;
    use std::path::Path;
    use std::ptr;
    use std::sync::atomic::{AtomicU32, Ordering};
    use std::sync::{Arc, Mutex, OnceLock};

    use sha2::{Digest, Sha256};
    use windows_sys::Win32::Foundation::{CloseHandle, HANDLE, INVALID_HANDLE_VALUE};
    use windows_sys::Win32::System::Memory::{
        CreateFileMappingW, MapViewOfFile, UnmapViewOfFile, FILE_MAP_ALL_ACCESS, PAGE_READWRITE,
    };

    use super::AnalysisSlot;

    const REQUEST_CAPACITY: usize = 2_048;
    const READY_CAPACITY: usize = 2_048;
    const SPECTRUM_CAPACITY: usize = 16_384;
    const PERCEPTUAL_CAPACITY: usize = 1_280;
    const MAX_READ_RETRIES: usize = 3;

    #[repr(C, align(64))]
    struct SharedSlot<const CAPACITY: usize> {
        writer: AtomicU32,
        generation: AtomicU32,
        lengths: [AtomicU32; 2],
        bytes: UnsafeCell<[[u8; CAPACITY]; 2]>,
    }

    unsafe impl<const CAPACITY: usize> Sync for SharedSlot<CAPACITY> {}

    struct WriterClaim<'a>(&'a AtomicU32);

    impl Drop for WriterClaim<'_> {
        fn drop(&mut self) {
            self.0.store(0, Ordering::Release);
        }
    }

    impl<const CAPACITY: usize> SharedSlot<CAPACITY> {
        fn write(&self, bytes: &[u8]) -> io::Result<()> {
            if bytes.len() > CAPACITY {
                return Err(io::Error::new(
                    io::ErrorKind::InvalidInput,
                    "Analysis payload exceeds shared slot capacity",
                ));
            }
            if self
                .writer
                .compare_exchange(0, 1, Ordering::AcqRel, Ordering::Acquire)
                .is_err()
            {
                return Err(io::Error::new(
                    io::ErrorKind::WouldBlock,
                    "Analysis slot already has a writer",
                ));
            }
            let _claim = WriterClaim(&self.writer);
            let current = self.generation.load(Ordering::Acquire);
            let next = if current == u32::MAX { 2 } else { current + 1 };
            let bank = (next & 1) as usize;
            // SAFETY: the writer claim gives exclusive access to the inactive bank. Readers keep
            // using the current bank until `generation` publishes this complete replacement.
            unsafe {
                ptr::copy_nonoverlapping(
                    bytes.as_ptr(),
                    (*self.bytes.get())[bank].as_mut_ptr(),
                    bytes.len(),
                );
            }
            self.lengths[bank].store(bytes.len() as u32, Ordering::Relaxed);
            self.generation.store(next, Ordering::Release);
            Ok(())
        }

        fn read(&self, maximum_bytes: u64) -> Option<Vec<u8>> {
            let maximum_bytes = usize::try_from(maximum_bytes).ok()?.min(CAPACITY);
            for _ in 0..MAX_READ_RETRIES {
                let before = self.generation.load(Ordering::Acquire);
                if before == 0 {
                    return None;
                }
                let bank = (before & 1) as usize;
                let length = self.lengths[bank].load(Ordering::Acquire) as usize;
                if length == 0 || length > maximum_bytes {
                    return None;
                }
                let mut bytes = vec![0_u8; length];
                // SAFETY: the mapped slot is alive for this call. The generation validation below
                // discards a copy if another complete bank became current concurrently.
                unsafe {
                    ptr::copy_nonoverlapping(
                        (*self.bytes.get())[bank].as_ptr(),
                        bytes.as_mut_ptr(),
                        length,
                    );
                }
                let after = self.generation.load(Ordering::Acquire);
                if before == after {
                    return Some(bytes);
                }
            }
            None
        }

        fn clear(&self) -> io::Result<()> {
            self.write(&[])
        }
    }

    #[repr(C)]
    struct SharedExchange {
        request: SharedSlot<REQUEST_CAPACITY>,
        ready: SharedSlot<READY_CAPACITY>,
        spectrum: SharedSlot<SPECTRUM_CAPACITY>,
        perceptual: SharedSlot<PERCEPTUAL_CAPACITY>,
    }

    struct Mapping {
        handle: HANDLE,
        view: *mut SharedExchange,
    }

    unsafe impl Send for Mapping {}
    unsafe impl Sync for Mapping {}

    impl Mapping {
        fn open(instance_dir: &Path) -> io::Result<Self> {
            let name = mapping_name(instance_dir);
            let name_wide = name.encode_utf16().chain(Some(0)).collect::<Vec<_>>();
            let size = std::mem::size_of::<SharedExchange>();
            let handle = unsafe {
                CreateFileMappingW(
                    INVALID_HANDLE_VALUE,
                    ptr::null(),
                    PAGE_READWRITE,
                    0,
                    u32::try_from(size).map_err(io::Error::other)?,
                    name_wide.as_ptr(),
                )
            };
            if handle.is_null() {
                return Err(io::Error::last_os_error());
            }
            let mapped = unsafe { MapViewOfFile(handle, FILE_MAP_ALL_ACCESS, 0, 0, size) };
            if mapped.Value.is_null() {
                let error = io::Error::last_os_error();
                unsafe {
                    CloseHandle(handle);
                }
                return Err(error);
            }
            Ok(Self {
                handle,
                view: mapped.Value.cast(),
            })
        }

        fn slot(&self, slot: AnalysisSlot) -> SlotRef<'_> {
            // SAFETY: `view` maps exactly one `SharedExchange` and remains valid until Drop.
            let exchange = unsafe { &*self.view };
            match slot {
                AnalysisSlot::Request => SlotRef::Request(&exchange.request),
                AnalysisSlot::Ready => SlotRef::Ready(&exchange.ready),
                AnalysisSlot::Spectrum => SlotRef::Spectrum(&exchange.spectrum),
                AnalysisSlot::Perceptual => SlotRef::Perceptual(&exchange.perceptual),
            }
        }
    }

    impl Drop for Mapping {
        fn drop(&mut self) {
            unsafe {
                UnmapViewOfFile(
                    windows_sys::Win32::System::Memory::MEMORY_MAPPED_VIEW_ADDRESS {
                        Value: self.view.cast::<c_void>(),
                    },
                );
                CloseHandle(self.handle);
            }
        }
    }

    enum SlotRef<'a> {
        Request(&'a SharedSlot<REQUEST_CAPACITY>),
        Ready(&'a SharedSlot<READY_CAPACITY>),
        Spectrum(&'a SharedSlot<SPECTRUM_CAPACITY>),
        Perceptual(&'a SharedSlot<PERCEPTUAL_CAPACITY>),
    }

    impl SlotRef<'_> {
        fn write(&self, bytes: &[u8]) -> io::Result<()> {
            match self {
                Self::Request(slot) => slot.write(bytes),
                Self::Ready(slot) => slot.write(bytes),
                Self::Spectrum(slot) => slot.write(bytes),
                Self::Perceptual(slot) => slot.write(bytes),
            }
        }

        fn read(&self, maximum_bytes: u64) -> Option<Vec<u8>> {
            match self {
                Self::Request(slot) => slot.read(maximum_bytes),
                Self::Ready(slot) => slot.read(maximum_bytes),
                Self::Spectrum(slot) => slot.read(maximum_bytes),
                Self::Perceptual(slot) => slot.read(maximum_bytes),
            }
        }

        fn clear(&self) -> io::Result<()> {
            match self {
                Self::Request(slot) => slot.clear(),
                Self::Ready(slot) => slot.clear(),
                Self::Spectrum(slot) => slot.clear(),
                Self::Perceptual(slot) => slot.clear(),
            }
        }
    }

    fn mapping_name(instance_dir: &Path) -> String {
        let normalized = instance_dir
            .to_string_lossy()
            .replace('\\', "/")
            .to_ascii_lowercase();
        let digest = Sha256::digest(normalized.as_bytes());
        format!("Local\\KirinHyphaAnalysis-v1-{}", hex::encode(digest))
    }

    fn mapping(instance_dir: &Path) -> io::Result<Arc<Mapping>> {
        static MAPPINGS: OnceLock<Mutex<HashMap<String, Arc<Mapping>>>> = OnceLock::new();
        let name = mapping_name(instance_dir);
        let registry = MAPPINGS.get_or_init(|| Mutex::new(HashMap::new()));
        let mut registry = registry
            .lock()
            .unwrap_or_else(|poisoned| poisoned.into_inner());
        if let Some(mapping) = registry.get(&name) {
            return Ok(Arc::clone(mapping));
        }
        let mapping = Arc::new(Mapping::open(instance_dir)?);
        registry.insert(name, Arc::clone(&mapping));
        Ok(mapping)
    }

    pub(super) fn write(instance_dir: &Path, slot: AnalysisSlot, bytes: &[u8]) -> io::Result<()> {
        mapping(instance_dir)?.slot(slot).write(bytes)
    }

    pub(super) fn read(
        instance_dir: &Path,
        slot: AnalysisSlot,
        maximum_bytes: u64,
    ) -> Option<Vec<u8>> {
        mapping(instance_dir).ok()?.slot(slot).read(maximum_bytes)
    }

    pub(super) fn clear(instance_dir: &Path, slot: AnalysisSlot) -> io::Result<()> {
        mapping(instance_dir)?.slot(slot).clear()
    }

    #[cfg(test)]
    mod tests {
        use super::*;
        use std::sync::atomic::AtomicBool;
        use std::thread;

        #[test]
        fn named_mapping_round_trips_each_slot_without_files() {
            let temp = tempfile::tempdir().unwrap();
            for (slot, bytes) in [
                (AnalysisSlot::Request, b"request".as_slice()),
                (AnalysisSlot::Ready, b"ready".as_slice()),
                (AnalysisSlot::Spectrum, b"spectrum".as_slice()),
                (AnalysisSlot::Perceptual, b"perceptual".as_slice()),
            ] {
                write(temp.path(), slot, bytes).unwrap();
                assert_eq!(read(temp.path(), slot, 16), Some(bytes.to_vec()));
                clear(temp.path(), slot).unwrap();
                assert!(read(temp.path(), slot, 16).is_none());
            }
            assert!(!temp.path().join("spectrum").exists());
        }

        #[test]
        fn slot_rejects_oversized_payload_without_replacing_previous_value() {
            let temp = tempfile::tempdir().unwrap();
            write(temp.path(), AnalysisSlot::Request, b"valid").unwrap();
            let oversized = vec![0_u8; REQUEST_CAPACITY + 1];
            assert_eq!(
                write(temp.path(), AnalysisSlot::Request, &oversized)
                    .unwrap_err()
                    .kind(),
                io::ErrorKind::InvalidInput
            );
            assert_eq!(
                read(temp.path(), AnalysisSlot::Request, REQUEST_CAPACITY as u64),
                Some(b"valid".to_vec())
            );
        }

        #[test]
        fn contended_update_retains_the_last_complete_payload() {
            let mut banks = [[0_u8; 32]; 2];
            banks[1][..6].copy_from_slice(b"stable");
            let slot = SharedSlot::<32> {
                writer: AtomicU32::new(1),
                generation: AtomicU32::new(1),
                lengths: [AtomicU32::new(0), AtomicU32::new(6)],
                bytes: UnsafeCell::new(banks),
            };
            assert_eq!(slot.read(32), Some(b"stable".to_vec()));
            assert_eq!(
                slot.write(b"blocked").unwrap_err().kind(),
                io::ErrorKind::WouldBlock
            );
        }

        #[test]
        fn reader_never_accepts_a_partially_published_payload() {
            let slot = Arc::new(SharedSlot::<512> {
                writer: AtomicU32::new(0),
                generation: AtomicU32::new(0),
                lengths: [AtomicU32::new(0), AtomicU32::new(0)],
                bytes: UnsafeCell::new([[0; 512]; 2]),
            });
            let finished = Arc::new(AtomicBool::new(false));
            let writer_slot = Arc::clone(&slot);
            let writer_finished = Arc::clone(&finished);
            let writer = thread::spawn(move || {
                for value in 1_u8..=200 {
                    writer_slot.write(&vec![value; 512]).unwrap();
                }
                writer_finished.store(true, Ordering::Release);
            });
            while !finished.load(Ordering::Acquire) {
                if let Some(bytes) = slot.read(512) {
                    assert_eq!(bytes.len(), 512);
                    assert!(bytes.iter().all(|value| *value == bytes[0]));
                }
            }
            writer.join().unwrap();
        }
    }
}
