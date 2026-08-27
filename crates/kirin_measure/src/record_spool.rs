//! Non-audio Record spool.
//!
//! Audio still writes only to the preallocated SPSC lane. This worker owns the lane consumer and
//! appends raw f32 samples to an unlinked temporary file. The Measure worker reads that file at its
//! own pace, so expensive EBU/Phase-D work cannot fill the Audio -> capture lane during an offline
//! bounce. No filesystem operation is reachable from the Audio Thread.

use std::fs::{File, OpenOptions};
use std::io::{self, BufReader, Read, Seek, Write};
use std::path::PathBuf;
use std::sync::atomic::{AtomicBool, AtomicU64, Ordering};
use std::sync::Arc;
use std::thread::{self, JoinHandle};
use std::time::{Duration, Instant};

const DRAIN_SAMPLES: usize = 16_384;
const READER_BUFFER_BYTES: usize = DRAIN_SAMPLES * std::mem::size_of::<f32>();
static NEXT_SPOOL_ID: AtomicU64 = AtomicU64::new(1);

pub(crate) const fn memory_bytes_per_instance() -> usize {
    DRAIN_SAMPLES * std::mem::size_of::<f32>() + READER_BUFFER_BYTES
}

struct Shared {
    published_samples: AtomicU64,
    consumed_samples: AtomicU64,
    sealed: AtomicBool,
    closed: AtomicBool,
    failed: AtomicBool,
    draining: AtomicBool,
    /// POSIX unlinks the file immediately. Platforms which cannot unlink an open file retain the
    /// path until the last worker/reader/control owner disappears.
    retained_path: Option<PathBuf>,
}

impl Drop for Shared {
    fn drop(&mut self) {
        if let Some(path) = self.retained_path.take() {
            let _ = std::fs::remove_file(path);
        }
    }
}

pub(crate) struct RecordSpool {
    reader_seed: File,
    shared: Arc<Shared>,
    worker: Option<JoinHandle<()>>,
}

impl RecordSpool {
    pub(crate) fn start(mut source: rtrb::Consumer<f32>, generation: u64) -> io::Result<Self> {
        let root = std::env::temp_dir().join("kirin-hypha-record-spool");
        std::fs::create_dir_all(&root)?;
        let id = NEXT_SPOOL_ID.fetch_add(1, Ordering::Relaxed);
        let path = root.join(format!(
            "record-{}-{}-{}.f32le",
            std::process::id(),
            generation,
            id
        ));
        let mut writer = OpenOptions::new()
            .create_new(true)
            .write(true)
            .open(&path)?;
        let reader_seed = OpenOptions::new().read(true).open(&path)?;

        // POSIX keeps both open file descriptions alive after unlink and removes the spool
        // automatically on normal exit or crash. Windows retains the explicit path for Drop.
        #[cfg(unix)]
        let retained_path = {
            std::fs::remove_file(&path)?;
            None
        };
        #[cfg(not(unix))]
        let retained_path = Some(path);

        let shared = Arc::new(Shared {
            published_samples: AtomicU64::new(0),
            consumed_samples: AtomicU64::new(0),
            sealed: AtomicBool::new(false),
            closed: AtomicBool::new(false),
            failed: AtomicBool::new(false),
            draining: AtomicBool::new(false),
            retained_path,
        });
        let worker_shared = Arc::clone(&shared);
        let worker = match thread::Builder::new()
            .name(format!("hypha-record-spool-{generation}"))
            .spawn(move || {
                let mut bytes = Vec::with_capacity(DRAIN_SAMPLES * std::mem::size_of::<f32>());
                loop {
                    if worker_shared.failed.load(Ordering::Acquire) {
                        break;
                    }
                    worker_shared.draining.store(true, Ordering::Release);
                    bytes.clear();
                    for _ in 0..DRAIN_SAMPLES {
                        let Ok(sample) = source.pop() else { break };
                        bytes.extend_from_slice(&sample.to_le_bytes());
                    }
                    if !bytes.is_empty() {
                        if writer.write_all(&bytes).is_err() {
                            worker_shared.failed.store(true, Ordering::Release);
                            worker_shared.draining.store(false, Ordering::Release);
                            break;
                        }
                        worker_shared.published_samples.fetch_add(
                            (bytes.len() / std::mem::size_of::<f32>()) as u64,
                            Ordering::Release,
                        );
                        worker_shared.draining.store(false, Ordering::Release);
                        continue;
                    }
                    worker_shared.draining.store(false, Ordering::Release);
                    if worker_shared.sealed.load(Ordering::Acquire) && source.slots() == 0 {
                        break;
                    }
                    thread::sleep(Duration::from_millis(1));
                }
                worker_shared.closed.store(true, Ordering::Release);
                // Make the close ordering explicit for platforms which cannot unlink an open
                // file. If this worker owns the final Shared reference, its retained-path cleanup
                // must run only after the writer handle has closed.
                drop(writer);
                drop(worker_shared);
            }) {
            Ok(worker) => worker,
            Err(error) => {
                let failed_path = shared.retained_path.clone();
                drop(shared);
                drop(reader_seed);
                if let Some(path) = failed_path {
                    let _ = std::fs::remove_file(path);
                }
                return Err(error);
            }
        };
        Ok(Self {
            reader_seed,
            shared,
            worker: Some(worker),
        })
    }

    pub(crate) fn reader(&self) -> io::Result<RecordSpoolReader> {
        let mut file = self.reader_seed.try_clone()?;
        file.seek(io::SeekFrom::Start(0))?;
        self.shared.consumed_samples.store(0, Ordering::Release);
        Ok(RecordSpoolReader {
            file: BufReader::with_capacity(READER_BUFFER_BYTES, file),
            shared: Arc::clone(&self.shared),
            consumed_samples: 0,
            available_until: 0,
        })
    }

    pub(crate) fn seal(&self) {
        self.shared.sealed.store(true, Ordering::Release);
    }

    pub(crate) fn wait_closed(&self, timeout: Duration) -> bool {
        let deadline = Instant::now() + timeout;
        while !self.shared.closed.load(Ordering::Acquire) {
            if Instant::now() >= deadline {
                return false;
            }
            thread::sleep(Duration::from_millis(1));
        }
        !self.shared.failed.load(Ordering::Acquire)
    }

    pub(crate) fn fully_consumed(&self) -> bool {
        self.shared.closed.load(Ordering::Acquire)
            && !self.shared.failed.load(Ordering::Acquire)
            && self.caught_up()
    }

    pub(crate) fn caught_up(&self) -> bool {
        !self.shared.failed.load(Ordering::Acquire)
            && !self.shared.draining.load(Ordering::Acquire)
            && self.shared.consumed_samples.load(Ordering::Acquire)
                == self.shared.published_samples.load(Ordering::Acquire)
    }

    pub(crate) fn is_closed(&self) -> bool {
        self.shared.closed.load(Ordering::Acquire)
    }
}

impl Drop for RecordSpool {
    fn drop(&mut self) {
        self.seal();
        // A dropped control seed has no future reader. Ask a still-running worker to abandon any
        // remaining backlog; a worker already blocked inside local I/O remains safely detached.
        self.shared.failed.store(true, Ordering::Release);
        // Never let a stalled local filesystem hold the DAW's plug-in teardown thread. The worker
        // owns its consumer, writer and Shared lifetime, so dropping JoinHandle safely detaches it;
        // Shared removes a retained non-POSIX path after the last file owner is gone.
        detach_worker(&mut self.worker);
    }
}

#[inline]
fn detach_worker(worker: &mut Option<JoinHandle<()>>) {
    drop(worker.take());
}

pub(crate) struct RecordSpoolReader {
    file: BufReader<File>,
    shared: Arc<Shared>,
    consumed_samples: u64,
    available_until: u64,
}

impl RecordSpoolReader {
    fn slots(&self) -> usize {
        self.shared
            .consumed_samples
            .store(self.consumed_samples, Ordering::Release);
        if self.shared.failed.load(Ordering::Acquire) {
            return 0;
        }
        self.shared
            .published_samples
            .load(Ordering::Acquire)
            .saturating_sub(self.consumed_samples)
            .min(usize::MAX as u64) as usize
    }

    fn pop(&mut self) -> Result<f32, ()> {
        if self.consumed_samples >= self.available_until {
            self.available_until = self.shared.published_samples.load(Ordering::Acquire);
            if self.consumed_samples >= self.available_until {
                self.shared
                    .consumed_samples
                    .store(self.consumed_samples, Ordering::Release);
                return Err(());
            }
        }
        let mut bytes = [0_u8; std::mem::size_of::<f32>()];
        if self.file.read_exact(&mut bytes).is_err() {
            self.shared.failed.store(true, Ordering::Release);
            return Err(());
        }
        self.consumed_samples = self.consumed_samples.saturating_add(1);
        Ok(f32::from_le_bytes(bytes))
    }
}

impl Drop for RecordSpoolReader {
    fn drop(&mut self) {
        self.shared
            .consumed_samples
            .store(self.consumed_samples, Ordering::Release);
    }
}

pub(crate) enum MeasureSampleConsumer {
    Watch(rtrb::Consumer<f32>),
    Record(RecordSpoolReader),
}

impl MeasureSampleConsumer {
    pub(crate) fn watch(consumer: rtrb::Consumer<f32>) -> Self {
        Self::Watch(consumer)
    }

    pub(crate) fn record(reader: RecordSpoolReader) -> Self {
        Self::Record(reader)
    }

    pub(crate) fn slots(&self) -> usize {
        match self {
            Self::Watch(consumer) => consumer.slots(),
            Self::Record(reader) => reader.slots(),
        }
    }

    pub(crate) fn pop(&mut self) -> Result<f32, ()> {
        match self {
            Self::Watch(consumer) => consumer.pop().map_err(|_| ()),
            Self::Record(reader) => reader.pop(),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::detach_worker;
    use std::time::{Duration, Instant};

    #[test]
    fn detaching_a_stalled_worker_never_waits_for_its_completion() {
        let mut worker = Some(std::thread::spawn(|| {
            std::thread::sleep(Duration::from_millis(500));
        }));
        let started = Instant::now();
        detach_worker(&mut worker);
        assert!(worker.is_none());
        assert!(
            started.elapsed() < Duration::from_millis(200),
            "plug-in lifetime must not join a stalled spool worker"
        );
    }
}
