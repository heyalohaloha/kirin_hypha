//! Non-RT, bounded Windows Analysis exchange. A named mutex protects BOTH readers and
//! writers. A generation check after an unsynchronised memcpy cannot prevent a Rust data race.
//! Busy ownership is skipped (zero wait). Abandoned ownership invalidates all ephemeral slots.
use super::AnalysisSlot;
use sha2::{Digest, Sha256};
use std::cell::UnsafeCell;
use std::collections::HashMap;
use std::ffi::c_void;
use std::io;
use std::marker::PhantomData;
use std::path::Path;
use std::ptr;
use std::rc::Rc;
use std::sync::{Arc, Mutex, OnceLock};
use windows_sys::Win32::Foundation::{
    CloseHandle, HANDLE, INVALID_HANDLE_VALUE, WAIT_ABANDONED, WAIT_OBJECT_0, WAIT_TIMEOUT,
};
use windows_sys::Win32::System::Memory::{
    CreateFileMappingW, MapViewOfFile, UnmapViewOfFile, FILE_MAP_ALL_ACCESS,
    MEMORY_MAPPED_VIEW_ADDRESS, PAGE_READWRITE,
};
use windows_sys::Win32::System::Threading::{CreateMutexW, ReleaseMutex, WaitForSingleObject};

const REQUEST_CAPACITY: usize = 2_048;
const READY_CAPACITY: usize = 2_048;
const SPECTRUM_CAPACITY: usize = 16_384;
const PERCEPTUAL_CAPACITY: usize = 4_096;
const ATTACK_CAPACITY: usize = 196_608;
const SLOTS: [AnalysisSlot; 5] = [
    AnalysisSlot::Request,
    AnalysisSlot::Ready,
    AnalysisSlot::Spectrum,
    AnalysisSlot::Perceptual,
    AnalysisSlot::Attack,
];

#[repr(C, align(64))]
struct SharedSlot<const CAPACITY: usize> {
    length: UnsafeCell<u32>,
    bytes: UnsafeCell<[u8; CAPACITY]>,
}

impl<const CAPACITY: usize> SharedSlot<CAPACITY> {
    // Slots are only borrowed through Claim::slot; their borrow cannot outlive mutex ownership.
    fn write(&self, bytes: &[u8]) -> io::Result<()> {
        if bytes.len() > CAPACITY {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                "Analysis payload exceeds shared slot capacity",
            ));
        }
        // SAFETY: the live Claim excludes all other readers and writers, including other
        // processes. If this thread dies mid-copy, the next claim invalidates every length.
        unsafe {
            *self.length.get() = 0;
            ptr::copy_nonoverlapping(bytes.as_ptr(), self.bytes.get().cast::<u8>(), bytes.len());
            *self.length.get() = bytes.len() as u32;
        }
        Ok(())
    }

    fn read(&self, maximum_bytes: u64) -> Option<Vec<u8>> {
        let limit = usize::try_from(maximum_bytes).ok()?.min(CAPACITY);
        // SAFETY: Claim::slot ties this reference to exclusive OS mutex ownership.
        let length = unsafe { *self.length.get() } as usize;
        if length == 0 || length > limit {
            return None;
        }
        let mut bytes = vec![0; length];
        unsafe {
            ptr::copy_nonoverlapping(self.bytes.get().cast::<u8>(), bytes.as_mut_ptr(), length);
        }
        Some(bytes)
    }
}

#[repr(C)]
struct SharedExchange {
    request: SharedSlot<REQUEST_CAPACITY>,
    ready: SharedSlot<READY_CAPACITY>,
    spectrum: SharedSlot<SPECTRUM_CAPACITY>,
    perceptual: SharedSlot<PERCEPTUAL_CAPACITY>,
    attack: SharedSlot<ATTACK_CAPACITY>,
}

struct Mapping {
    handle: HANDLE,
    mutex: HANDLE,
    view: *mut SharedExchange,
}

// SAFETY: mappings are process-wide handles; all mapped data access requires a thread-bound
// Claim on the same named OS mutex. Drop cannot run while a Claim borrows this Mapping.
unsafe impl Send for Mapping {}
unsafe impl Sync for Mapping {}

struct Claim<'a> {
    owner: &'a Mapping,
    // Windows mutexes are owned by a THREAD, not a process. Never move a live claim to another.
    _same_thread: PhantomData<Rc<()>>,
}

impl Claim<'_> {
    fn slot(&self, slot: AnalysisSlot) -> SlotRef<'_> {
        // SAFETY: this claim owns the mutex; the mapping is zero-initialised by Windows and
        // contains only integer bytes with no invalid bit patterns or process-local pointers.
        let exchange = unsafe { &*self.owner.view };
        match slot {
            AnalysisSlot::Request => SlotRef::Request(&exchange.request),
            AnalysisSlot::Ready => SlotRef::Ready(&exchange.ready),
            AnalysisSlot::Spectrum => SlotRef::Spectrum(&exchange.spectrum),
            AnalysisSlot::Perceptual => SlotRef::Perceptual(&exchange.perceptual),
            AnalysisSlot::Attack => SlotRef::Attack(&exchange.attack),
        }
    }
}

impl Drop for Claim<'_> {
    fn drop(&mut self) {
        unsafe {
            ReleaseMutex(self.owner.mutex);
        }
    }
}

impl Mapping {
    fn open(instance_dir: &Path) -> io::Result<Self> {
        let name = mapping_name(instance_dir);
        let wide = name.encode_utf16().chain(Some(0)).collect::<Vec<_>>();
        let mutex_wide = format!("{name}-access")
            .encode_utf16()
            .chain(Some(0))
            .collect::<Vec<_>>();
        let size = std::mem::size_of::<SharedExchange>();
        let size32 = u32::try_from(size).map_err(io::Error::other)?;
        let mutex = unsafe { CreateMutexW(ptr::null(), 0, mutex_wide.as_ptr()) };
        if mutex.is_null() {
            return Err(io::Error::last_os_error());
        }
        let handle = unsafe {
            CreateFileMappingW(
                INVALID_HANDLE_VALUE,
                ptr::null(),
                PAGE_READWRITE,
                0,
                size32,
                wide.as_ptr(),
            )
        };
        if handle.is_null() {
            let error = io::Error::last_os_error();
            unsafe {
                CloseHandle(mutex);
            }
            return Err(error);
        }
        let mapped = unsafe { MapViewOfFile(handle, FILE_MAP_ALL_ACCESS, 0, 0, size) };
        if mapped.Value.is_null() {
            let error = io::Error::last_os_error();
            unsafe {
                CloseHandle(handle);
                CloseHandle(mutex);
            }
            return Err(error);
        }
        Ok(Self {
            handle,
            mutex,
            view: mapped.Value.cast(),
        })
    }

    fn try_claim(&self) -> io::Result<Claim<'_>> {
        // No blocking wait, timer, retry loop, or spin. This API is never called on Audio RT.
        let outcome = unsafe { WaitForSingleObject(self.mutex, 0) };
        if outcome == WAIT_TIMEOUT {
            return Err(io::Error::new(
                io::ErrorKind::WouldBlock,
                "Analysis exchange is in use",
            ));
        }
        if outcome != WAIT_OBJECT_0 && outcome != WAIT_ABANDONED {
            return Err(io::Error::last_os_error());
        }
        let claim = Claim {
            owner: self,
            _same_thread: PhantomData,
        };
        if outcome == WAIT_ABANDONED {
            // Ownership was granted, but the previous owner may have died mid-copy. Never
            // accept even an apparently valid length. Requests/readiness will be republished.
            for slot in SLOTS {
                claim.slot(slot).write(&[])?;
            }
        }
        Ok(claim)
    }

    fn write(&self, slot: AnalysisSlot, bytes: &[u8]) -> io::Result<()> {
        self.try_claim()?.slot(slot).write(bytes)
    }
    fn read(&self, slot: AnalysisSlot, maximum: u64) -> Option<Vec<u8>> {
        self.try_claim().ok()?.slot(slot).read(maximum)
    }
}

impl Drop for Mapping {
    fn drop(&mut self) {
        unsafe {
            UnmapViewOfFile(MEMORY_MAPPED_VIEW_ADDRESS {
                Value: self.view.cast::<c_void>(),
            });
            CloseHandle(self.handle);
            CloseHandle(self.mutex);
        }
    }
}

enum SlotRef<'a> {
    Request(&'a SharedSlot<REQUEST_CAPACITY>),
    Ready(&'a SharedSlot<READY_CAPACITY>),
    Spectrum(&'a SharedSlot<SPECTRUM_CAPACITY>),
    Perceptual(&'a SharedSlot<PERCEPTUAL_CAPACITY>),
    Attack(&'a SharedSlot<ATTACK_CAPACITY>),
}
impl SlotRef<'_> {
    fn write(&self, bytes: &[u8]) -> io::Result<()> {
        match self {
            Self::Request(s) => s.write(bytes),
            Self::Ready(s) => s.write(bytes),
            Self::Spectrum(s) => s.write(bytes),
            Self::Perceptual(s) => s.write(bytes),
            Self::Attack(s) => s.write(bytes),
        }
    }
    fn read(&self, maximum: u64) -> Option<Vec<u8>> {
        match self {
            Self::Request(s) => s.read(maximum),
            Self::Ready(s) => s.read(maximum),
            Self::Spectrum(s) => s.read(maximum),
            Self::Perceptual(s) => s.read(maximum),
            Self::Attack(s) => s.read(maximum),
        }
    }
}

fn mapping_name(instance_dir: &Path) -> String {
    let normalized = instance_dir
        .to_string_lossy()
        .replace('\\', "/")
        .to_ascii_lowercase();
    // v2 has unsynchronised readers. Never let an older binary join this mutex-protected layout.
    format!(
        "Local\\KirinHyphaAnalysis-v3-{}",
        hex::encode(Sha256::digest(normalized.as_bytes()))
    )
}

fn mapping(instance_dir: &Path) -> io::Result<Arc<Mapping>> {
    static MAPPINGS: OnceLock<Mutex<HashMap<String, Arc<Mapping>>>> = OnceLock::new();
    let name = mapping_name(instance_dir);
    let registry = MAPPINGS.get_or_init(|| Mutex::new(HashMap::new()));
    let mut registry = registry.lock().unwrap_or_else(|p| p.into_inner());
    if let Some(mapping) = registry.get(&name) {
        return Ok(Arc::clone(mapping));
    }
    let mapping = Arc::new(Mapping::open(instance_dir)?);
    registry.insert(name, Arc::clone(&mapping));
    Ok(mapping)
}

pub(super) fn write(instance_dir: &Path, slot: AnalysisSlot, bytes: &[u8]) -> io::Result<()> {
    mapping(instance_dir)?.write(slot, bytes)
}
pub(super) fn read(instance_dir: &Path, slot: AnalysisSlot, maximum_bytes: u64) -> Option<Vec<u8>> {
    mapping(instance_dir).ok()?.read(slot, maximum_bytes)
}
pub(super) fn clear(instance_dir: &Path, slot: AnalysisSlot) -> io::Result<()> {
    mapping(instance_dir)?.write(slot, &[])
}

#[cfg(test)]
#[path = "analysis_exchange_windows_tests.rs"]
mod tests;
