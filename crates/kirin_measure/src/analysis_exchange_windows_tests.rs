use super::*;
use std::sync::{
    atomic::{AtomicBool, Ordering},
    mpsc,
};
use std::thread;
use std::time::{Duration, Instant};

#[test]
fn named_mapping_round_trips_all_slots_without_files() {
    let temp = tempfile::tempdir().unwrap();
    for slot in SLOTS {
        write(temp.path(), slot, b"complete").unwrap();
        assert_eq!(read(temp.path(), slot, 32), Some(b"complete".to_vec()));
        assert_eq!(read(temp.path(), slot, 7), None);
        clear(temp.path(), slot).unwrap();
        assert_eq!(read(temp.path(), slot, u64::MAX), None);
    }
    assert_eq!(std::fs::read_dir(temp.path()).unwrap().count(), 0);
}

#[test]
fn capacity_and_distinct_layout_are_explicit() {
    let temp = tempfile::tempdir().unwrap();
    for (slot, cap) in [
        (AnalysisSlot::Request, REQUEST_CAPACITY),
        (AnalysisSlot::Ready, READY_CAPACITY),
        (AnalysisSlot::Spectrum, SPECTRUM_CAPACITY),
        (AnalysisSlot::Perceptual, PERCEPTUAL_CAPACITY),
        (AnalysisSlot::Attack, ATTACK_CAPACITY),
    ] {
        let exact = vec![0x57; cap];
        write(temp.path(), slot, &exact).unwrap();
        assert_eq!(
            write(temp.path(), slot, &vec![0; cap + 1])
                .unwrap_err()
                .kind(),
            io::ErrorKind::InvalidInput
        );
        assert_eq!(read(temp.path(), slot, u64::MAX), Some(exact));
    }
    let name = mapping_name(temp.path());
    assert!(name.starts_with("Local\\KirinHyphaAnalysis-v3-"));
    assert_ne!(name, name.replace("-v3-", "-v2-"));
    // Fixed bounds: one copy per slot, not two megabyte-sized queues per reader.
    assert!(std::mem::size_of::<SharedExchange>() < 224 * 1024);
}

#[test]
fn distinct_handles_share_then_last_close_discards_ephemeral_data() {
    let temp = tempfile::tempdir().unwrap();
    {
        let first = Mapping::open(temp.path()).unwrap();
        let second = Mapping::open(temp.path()).unwrap();
        first.write(AnalysisSlot::Perceptual, b"joined").unwrap();
        assert_eq!(
            second.read(AnalysisSlot::Perceptual, 32),
            Some(b"joined".to_vec())
        );
    }
    let fresh = Mapping::open(temp.path()).unwrap();
    assert_eq!(fresh.read(AnalysisSlot::Perceptual, 32), None);
}

#[test]
fn busy_owner_skips_both_reader_and_writer_without_waiting() {
    let temp = tempfile::tempdir().unwrap();
    let shared = Arc::new(Mapping::open(temp.path()).unwrap());
    shared.write(AnalysisSlot::Request, b"stable").unwrap();
    let worker_mapping = Arc::clone(&shared);
    let (ready, wait_ready) = mpsc::channel();
    let (release, wait_release) = mpsc::channel();
    let worker = thread::spawn(move || {
        let _claim = worker_mapping.try_claim().unwrap();
        ready.send(()).unwrap();
        wait_release.recv_timeout(Duration::from_secs(5)).unwrap();
    });
    wait_ready.recv_timeout(Duration::from_secs(5)).unwrap();
    let started = Instant::now();
    assert_eq!(shared.read(AnalysisSlot::Request, 32), None);
    assert_eq!(
        shared
            .write(AnalysisSlot::Request, b"not published")
            .unwrap_err()
            .kind(),
        io::ErrorKind::WouldBlock
    );
    assert!(started.elapsed() < Duration::from_millis(500));
    release.send(()).unwrap();
    worker.join().unwrap();
    assert_eq!(
        shared.read(AnalysisSlot::Request, 32),
        Some(b"stable".to_vec())
    );
}

#[test]
fn concurrent_reader_cannot_overlap_a_payload_copy() {
    let temp = tempfile::tempdir().unwrap();
    let shared = Arc::new(Mapping::open(temp.path()).unwrap());
    let finished = Arc::new(AtomicBool::new(false));
    let worker_mapping = Arc::clone(&shared);
    let worker_finished = Arc::clone(&finished);
    let worker = thread::spawn(move || {
        let deadline = Instant::now() + Duration::from_secs(15);
        for value in 1..=200u8 {
            let bytes = vec![value; 4096];
            loop {
                match worker_mapping.write(AnalysisSlot::Spectrum, &bytes) {
                    Ok(()) => break,
                    Err(error) if error.kind() == io::ErrorKind::WouldBlock => {
                        assert!(Instant::now() < deadline);
                        thread::sleep(Duration::from_millis(1));
                    }
                    Err(error) => panic!("{error}"),
                }
            }
        }
        worker_finished.store(true, Ordering::Release);
    });
    let deadline = Instant::now() + Duration::from_secs(20);
    while !finished.load(Ordering::Acquire) {
        assert!(Instant::now() < deadline);
        if let Some(bytes) = shared.read(AnalysisSlot::Spectrum, 4096) {
            assert_eq!(bytes.len(), 4096);
            assert!(bytes.iter().all(|v| *v == bytes[0]));
        }
        thread::yield_now();
    }
    worker.join().unwrap();
    assert_eq!(
        shared.read(AnalysisSlot::Spectrum, 4096),
        Some(vec![200; 4096])
    );
}

const CRASH_PROBE: &str = "KIRIN_ANALYSIS_MUTEX_CRASH_TEST_PATH";

#[test]
fn abandoned_owner_child_probe() {
    let Some(path) = std::env::var_os(CRASH_PROBE) else {
        return;
    };
    let mapped = Mapping::open(Path::new(&path)).unwrap();
    let claim = mapped.try_claim().unwrap();
    claim
        .slot(AnalysisSlot::Perceptual)
        .write(b"uncommitted")
        .unwrap();
    // Simulate the owner process dying during a payload update. No destructor runs; only
    // Windows can release this mutex. This is a dedicated child test, never the host/DAW.
    unsafe {
        *(*mapped.view).perceptual.bytes.get().cast::<u8>() = 0xee;
    }
    std::process::exit(78);
}

#[test]
fn abandoned_process_invalidates_all_slots_and_accepts_a_fresh_publication() {
    let temp = tempfile::tempdir().unwrap();
    let mapped = Mapping::open(temp.path()).unwrap();
    for slot in SLOTS {
        mapped.write(slot, b"previous").unwrap();
    }
    let mut child = std::process::Command::new(std::env::current_exe().unwrap())
        .arg("analysis_exchange_transport::windows::tests::abandoned_owner_child_probe")
        .args(["--exact", "--nocapture"])
        .env(CRASH_PROBE, temp.path())
        .spawn()
        .unwrap();
    let deadline = Instant::now() + Duration::from_secs(15);
    let status = loop {
        if let Some(status) = child.try_wait().unwrap() {
            break status;
        }
        if Instant::now() >= deadline {
            let _ = child.kill();
            let _ = child.wait();
            panic!("dedicated crash probe timed out");
        }
        thread::sleep(Duration::from_millis(10));
    };
    assert_eq!(status.code(), Some(78));
    for slot in SLOTS {
        assert_eq!(mapped.read(slot, 128), None);
    }
    mapped.write(AnalysisSlot::Perceptual, b"fresh").unwrap();
    assert_eq!(
        mapped.read(AnalysisSlot::Perceptual, 128),
        Some(b"fresh".to_vec())
    );
}
