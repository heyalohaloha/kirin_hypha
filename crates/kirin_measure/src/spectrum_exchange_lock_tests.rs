use super::*;
use crate::atomic_file::AtomicWritePause;
use std::sync::Arc;
use std::thread;
use std::time::{Duration, Instant};

fn push_spectrum_audio(runtime: &SpectrumRuntime) {
    let block_frames = 256_usize;
    let total_frames = 6_400_usize;
    let mut position = 0_i64;
    while position < total_frames as i64 {
        let frames = block_frames.min(total_frames - position as usize);
        let mut interleaved = Vec::with_capacity(frames * 2);
        for offset in 0..frames {
            let index = position as usize + offset;
            let sample = (std::f32::consts::TAU * 997.0 * index as f32 / 48_000.0).sin() * 0.25;
            interleaved.extend_from_slice(&[sample, sample]);
        }
        assert!(runtime.push_block_from_audio(&interleaved, 2, Some(position)));
        position += frames as i64;
        thread::sleep(Duration::from_millis(1));
    }
    let deadline = Instant::now() + Duration::from_secs(2);
    while Instant::now() < deadline && runtime.try_history().is_none() {
        thread::sleep(Duration::from_millis(2));
    }
    assert!(runtime.try_history().is_some());
}

#[test]
fn filesystem_stalls_never_hold_post_or_pre_session_locks() {
    let temp = tempfile::tempdir().unwrap();
    let pre_dir = temp.path().join("pre");
    let pre_json = pre_dir.join("pre.json");
    crate::atomic_file::write_bytes_atomic(&pre_json, b"{}").unwrap();
    let target = SpectrumTarget::from_pre_json("pre".to_string(), &pre_json).unwrap();

    let post_runtime = SpectrumRuntime::new(48_000, 2);
    let post = SpectrumCoordinator::new(48_000, Arc::clone(&post_runtime));
    post.set_post_visible(true);
    let request_pause = AtomicWritePause::install(request_path(&pre_dir));
    let post_for_tick = Arc::clone(&post);
    let target_for_tick = target.clone();
    let post_tick = thread::spawn(move || post_for_tick.post_tick("post", Some(target_for_tick)));
    request_pause.wait_until_entered();
    assert!(post.post_session.try_lock().is_ok());
    thread::sleep(REQUEST_RENEW_INTERVAL + Duration::from_millis(25));
    let rescue_started = Instant::now();
    assert!(post.post_tick("post", Some(target.clone())));
    assert!(rescue_started.elapsed() < Duration::from_millis(250));
    assert!(validated_request(&pre_dir, "pre", 48_000, unix_ms_now()).is_some());
    let close_started = Instant::now();
    post.set_post_visible(false);
    assert!(close_started.elapsed() < Duration::from_millis(250));
    request_pause.release();
    assert!(!post_tick.join().unwrap());
    drop(request_pause);
    post.shutdown();

    let post_for_pre_runtime = SpectrumRuntime::new(48_000, 2);
    let post_for_pre = SpectrumCoordinator::new(48_000, Arc::clone(&post_for_pre_runtime));
    post_for_pre.set_post_visible(true);
    assert!(post_for_pre.post_tick("post-for-pre", Some(target.clone())));

    let pre_runtime = SpectrumRuntime::new(48_000, 2);
    let pre = SpectrumCoordinator::new(48_000, Arc::clone(&pre_runtime));
    assert!(pre.pre_tick("pre", &pre_dir));
    push_spectrum_audio(&pre_runtime);
    // The full workspace runs this test beside CPU-heavy engine checks. Renew immediately before
    // injecting the filesystem stall so scheduler delay cannot expire the intentionally short
    // production request lease and turn this into a request-lifetime test.
    assert!(post_for_pre.post_tick("post-for-pre", Some(target.clone())));
    let snapshot_pause = AtomicWritePause::install(snapshot_path(&pre_dir));
    let pre_for_tick = Arc::clone(&pre);
    let pre_dir_for_tick = pre_dir.clone();
    let pre_tick = thread::spawn(move || {
        let deadline = Instant::now() + Duration::from_secs(5);
        while Instant::now() < deadline {
            let active = pre_for_tick.pre_tick("pre", &pre_dir_for_tick);
            if active && read_snapshot(&pre_dir_for_tick).is_some() {
                return true;
            }
            thread::sleep(Duration::from_millis(2));
        }
        false
    });
    snapshot_pause.wait_until_entered();
    assert!(pre.pre_session.try_lock().is_ok());
    thread::sleep(REQUEST_RENEW_INTERVAL + Duration::from_millis(25));
    assert!(post_for_pre.post_tick("post-for-pre", Some(target)));
    let rescue_started = Instant::now();
    assert!(pre.pre_tick("pre", &pre_dir));
    assert!(rescue_started.elapsed() < Duration::from_millis(250));
    assert!(read_snapshot(&pre_dir).is_some());
    snapshot_pause.release();
    assert!(pre_tick.join().unwrap());
    drop(snapshot_pause);

    pre.shutdown();
    post_for_pre.shutdown();
    pre_runtime.shutdown_and_join();
    post_for_pre_runtime.shutdown_and_join();
    post_runtime.shutdown_and_join();
}
