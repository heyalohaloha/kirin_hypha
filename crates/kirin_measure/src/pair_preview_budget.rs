//! Finite discovery I/O. Every operation is charged before execution; no engine or UI reference.
use std::fs::{self, File, OpenOptions, TryLockError};
use std::io::{self, Read};
use std::path::Path;
use std::time::{Duration, Instant};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Stop {
    Limit,
    Cancelled,
    Uncertain,
}
#[derive(Clone, Copy, Debug, Default)]
pub struct Statistics {
    pub entries: usize,
    pub json_files: usize,
    pub bytes: usize,
    pub operations: usize,
    pub batches: usize,
    pub max_batch_entries: usize,
    pub max_batch_operations: usize,
    pub max_batch_bytes: usize,
    pub elapsed_us: u64,
}

pub struct Budget<'a> {
    pub stats: Statistics,
    cancel: &'a dyn Fn() -> bool,
    started: Instant,
    batch: Instant,
    yielded: Duration,
    batch_entries: usize,
    batch_ops: usize,
    batch_bytes: usize,
}
impl<'a> Budget<'a> {
    pub fn new(cancel: &'a dyn Fn() -> bool) -> Self {
        let now = Instant::now();
        Self {
            stats: Statistics::default(),
            cancel,
            started: now,
            batch: now,
            yielded: Duration::ZERO,
            batch_entries: 0,
            batch_ops: 0,
            batch_bytes: 0,
        }
    }
    pub fn check(&self) -> Result<(), Stop> {
        if (self.cancel)() {
            return Err(Stop::Cancelled);
        }
        let elapsed = self.started.elapsed();
        if elapsed >= Duration::from_millis(250)
            || elapsed.saturating_sub(self.yielded) >= Duration::from_millis(30)
        {
            return Err(Stop::Limit);
        }
        Ok(())
    }
    fn boundary(&mut self, bytes: usize) -> Result<(), Stop> {
        self.check()?;
        if self.batch_entries >= 32
            || self.batch_ops >= 8
            || self.batch_bytes + bytes > 65536
            || self.batch.elapsed() >= Duration::from_millis(2)
        {
            let before = Instant::now();
            std::thread::yield_now();
            self.yielded += before.elapsed();
            self.batch = Instant::now();
            self.batch_entries = 0;
            self.batch_ops = 0;
            self.batch_bytes = 0;
            self.stats.batches += 1;
            self.check()?;
        }
        Ok(())
    }
    pub fn operation<T>(&mut self, f: impl FnOnce() -> io::Result<T>) -> Result<T, Stop> {
        self.boundary(0)?;
        self.batch_ops += 1;
        self.stats.operations += 1;
        self.stats.max_batch_operations = self.stats.max_batch_operations.max(self.batch_ops);
        let result = f().map_err(|_| Stop::Uncertain);
        self.check()?;
        result
    }
    pub fn entry(&mut self) -> Result<(), Stop> {
        if self.stats.entries >= 512 {
            return Err(Stop::Limit);
        }
        self.boundary(0)?;
        self.stats.entries += 1;
        self.batch_entries += 1;
        self.stats.max_batch_entries = self.stats.max_batch_entries.max(self.batch_entries);
        Ok(())
    }
    pub fn json(&mut self, path: &Path) -> Result<Option<Vec<u8>>, Stop> {
        if self.stats.json_files >= 64 {
            return Err(Stop::Limit);
        }
        // Count attempted JSON files too; repeated missing files cannot evade the work bound.
        self.stats.json_files += 1;
        let Some(mut file) = self.open_existing(path, false)? else {
            return Ok(None);
        };
        let metadata = self.operation(|| file.metadata())?;
        if !metadata.is_file() || metadata.len() > 65536 {
            return Err(Stop::Limit);
        }
        let length = metadata.len() as usize;
        if self.stats.bytes + length > 1048576 {
            return Err(Stop::Limit);
        }
        let mut bytes = vec![0; length];
        for chunk in bytes.chunks_mut(8192) {
            self.boundary(chunk.len())?;
            self.batch_ops += 1;
            self.stats.operations += 1;
            self.stats.max_batch_operations = self.stats.max_batch_operations.max(self.batch_ops);
            // Reserve requested bytes even on a short/error read. Counters never understate I/O.
            self.stats.bytes += chunk.len();
            self.batch_bytes += chunk.len();
            self.stats.max_batch_bytes = self.stats.max_batch_bytes.max(self.batch_bytes);
            file.read_exact(chunk).map_err(|_| Stop::Uncertain)?;
            self.check()?;
        }
        let after = self.operation(|| file.metadata())?;
        if metadata.len() != after.len() || metadata.modified().ok() != after.modified().ok() {
            return Err(Stop::Uncertain);
        }
        Ok(Some(bytes))
    }
    pub fn open_existing(&mut self, path: &Path, writable: bool) -> Result<Option<File>, Stop> {
        let before = self.operation(|| match fs::symlink_metadata(path) {
            Ok(value) => Ok(Some(value)),
            Err(error) if error.kind() == io::ErrorKind::NotFound => Ok(None),
            Err(error) => Err(error),
        })?;
        let Some(before) = before else {
            return Ok(None);
        };
        if !before.is_file() {
            return Err(Stop::Uncertain);
        }
        self.operation(|| {
            let mut options = OpenOptions::new();
            options.read(true).write(writable);
            #[cfg(unix)]
            {
                use std::os::unix::fs::OpenOptionsExt;
                options.custom_flags(libc::O_NOFOLLOW | libc::O_NONBLOCK);
            }
            options.open(path).map(Some)
        })
    }
    pub fn locked(&mut self, path: &Path) -> Result<bool, Stop> {
        let Some(file) = self.open_existing(path, true)? else {
            return Ok(false);
        };
        let result = self.operation(|| Ok(file.try_lock()))?;
        match result {
            Err(TryLockError::WouldBlock) => Ok(true),
            Ok(()) => {
                self.operation(|| file.unlock())?;
                Ok(false)
            }
            Err(TryLockError::Error(_)) => Err(Stop::Uncertain),
        }
    }
    pub fn finish(mut self) -> Statistics {
        self.stats.elapsed_us = self.started.elapsed().as_micros().min(u64::MAX as u128) as u64;
        self.stats
    }
}
