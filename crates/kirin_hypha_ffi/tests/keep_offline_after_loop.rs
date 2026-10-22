//! Paired Record output after realtime LOOP passes followed by an offline export and Drop.
#[allow(dead_code)]
mod keep_loop_host;
use keep_loop_host::{Host, StorageRoot, BLOCK, QUIET, RATE};
use kirin_hypha_ffi::{channel_abi::ChannelLayout, KirinHyphaEngine};
use kirin_measure::plugin_data::{PluginDataFile, Status};
use kirin_measure::record_drop_commit::*;
use kirin_measure::record_expected::ExpectedWavMetadata;
use kirin_measure::{
    AuxiliaryClockSamples, AuxiliaryClockSource, CaptureClockSource, MeterHistoryResolution,
    PresentationLatencySamples, PresentationLatencySource, RecordTakeBlock,
};
use std::{
    path::Path,
    thread::sleep,
    time::{Duration, Instant},
};

const FRAMES: u64 = 1_440_000;
const WATCHDOG: Duration = Duration::from_secs(600);

fn heartbeat(pre: &KirinHyphaEngine, post: &KirinHyphaEngine) {
    for engine in [pre, post] {
        engine.set_signal_state(1);
        engine.push_samples(&[], 2);
    }
}

fn offline_block(engine: &KirinHyphaEngine, position: u64, samples: &[f32]) {
    let frames = (samples.len() / 2) as u64;
    engine.set_signal_state(1);
    engine.note_record_block(RecordTakeBlock {
        generation: 0,
        recording: true,
        rendered: true,
        playing: true,
        offline: true,
        position_valid: true,
        position_samples: position as i64,
        num_frames: frames,
        clock_start_samples: 0,
        clock_end_samples: None,
    });
    engine.note_capture_window_with_clocks(
        true,
        position as i64,
        frames,
        CaptureClockSource::ProjectTimeline,
        PresentationLatencySamples {
            source: PresentationLatencySource::Vst3,
            input: Some(0),
            output: Some(0),
        },
        AuxiliaryClockSamples {
            source: AuxiliaryClockSource::Vst3Continuous,
            samples: Some(position as i64),
        },
        false,
    );
    engine.push_samples(samples, 2);
}

fn drop_metadata(base: &Path, signal: &kirin_measure::record_signal::RecordSignal) {
    let now = kirin_measure::record_writer::now_epoch_ms();
    let metadata = ExpectedWavMetadata {
        expected_duration_samples: FRAMES,
        expected_sample_rate: 48_000,
        wav_time_reference_samples: None,
        wav_path: base.join("fixture.wav").to_string_lossy().into_owned(),
        bounce_id: "offline-export".into(),
        created_at_ms: now,
        wav_file_size: Some(FRAMES * 8 + 44),
        wav_mtime_ms: now,
        wav_hash: Some("offline-export-fixture".into()),
        consumed_at_ms: None,
        consumed_by_session_id: None,
    };
    let id = "offline-export-drop";
    let commit = DropRecordCommit {
        schema_version: DROP_COMMIT_SCHEMA.into(),
        drop_commit_id: id.into(),
        project_hash: "offline-project".into(),
        record_session_id: signal.session_id.clone(),
        capture_generation_id: signal.capture_generation_id.clone(),
        generation_started_at_ms: signal.generation_started_at_ms,
        created_at_ms: now,
        metadata: metadata.clone(),
    };
    let transaction = DropRecordTransaction {
        schema_version: DROP_TRANSACTION_SCHEMA.into(),
        drop_commit_id: id.into(),
        project_hash: "offline-project".into(),
        capture_generation_id: signal.capture_generation_id.clone(),
        generation_started_at_ms: signal.generation_started_at_ms,
        created_at_ms: now,
        bounce_id: metadata.bounce_id.clone(),
        wav_hash: metadata.wav_hash.clone().unwrap(),
        record_session_ids: vec![signal.session_id.clone()],
    };
    let generation = DropRecordGenerationTransaction {
        schema_version: DROP_GENERATION_TRANSACTION_SCHEMA.into(),
        drop_commit_id: id.into(),
        capture_generation_id: signal.capture_generation_id.clone(),
        generation_started_at_ms: signal.generation_started_at_ms,
        created_at_ms: now,
        bounce_id: metadata.bounce_id.clone(),
        wav_hash: metadata.wav_hash.clone().unwrap(),
        projects: vec![DropRecordGenerationProject {
            project_hash: "offline-project".into(),
            record_session_ids: vec![signal.session_id.clone()],
        }],
    };
    for (path, value) in [
        (
            drop_commit_path(base, "offline-project", &signal.session_id),
            serde_json::to_vec(&commit).unwrap(),
        ),
        (
            drop_transaction_path(base, "offline-project", id),
            serde_json::to_vec(&transaction).unwrap(),
        ),
        (
            drop_generation_transaction_path(base, id),
            serde_json::to_vec(&generation).unwrap(),
        ),
    ] {
        kirin_measure::atomic_file::write_bytes_atomic(&path, &value).unwrap();
    }
}

fn closed(base: &Path, instance: &str, role: &str, session: &str) -> Option<PluginDataFile> {
    let path = base.join("offline-project").join(instance).join(role);
    let entries = std::fs::read_dir(path).ok()?;
    let mut found = None;
    for entry in entries {
        let path = entry.ok()?.path();
        if path.extension().is_none_or(|ext| ext != "json") {
            continue;
        }
        let Ok(bytes) = std::fs::read(path) else {
            continue;
        };
        let Ok(data) = serde_json::from_slice::<PluginDataFile>(&bytes) else {
            continue;
        };
        if data.status == Status::Closed && data.record_session_id.as_deref() == Some(session) {
            assert!(found.is_none(), "one canonical member per exact session");
            found = Some(data);
        }
    }
    found
}

#[test]
#[ignore = "slow: paired realtime LOOP followed by offline take and exact Drop"]
fn keep_offline_after_loop_retains_the_complete_selected_take() {
    let sandbox = tempfile::tempdir().unwrap();
    let _storage = StorageRoot::set(sandbox.path());
    let paths = kirin_measure::PlatformPaths::default_current()
        .unwrap()
        .storage;
    assert!(paths.kirin_os_root.starts_with(sandbox.path()));
    std::fs::create_dir_all(&paths.kirin_os_root).unwrap();
    std::fs::write(paths.primary_path(), br#"{"schema_version":"1.0","installation_id":"offline-take-fixture","hardware_id":"hw","hardware_components":{"iop":"a","sn":"b","bd":"c"},"machine_signature":"sig","license":"os","created_at":"2026-06-09T00:00:00Z","last_verified_at":"2026-06-09T00:00:00Z"}"#).unwrap();
    let base = paths.plugin_data_dir();
    let pre = KirinHyphaEngine::new(48_000, ChannelLayout::stereo());
    pre.set_identity(
        "offline-pre".into(),
        "offline-project".into(),
        "".into(),
        "mix".into(),
    );
    pre.enable_pre_writes();
    let post = KirinHyphaEngine::new(48_000, ChannelLayout::stereo());
    post.set_identity(
        "offline-post".into(),
        "offline-project".into(),
        "".into(),
        "output".into(),
    );
    post.enable_post_writes();
    post.set_pair_target("mix".into());
    let mut host = Host {
        position: 0,
        counter: 0,
        blocks: 0,
        start: Instant::now(),
        cycle: true,
        wrapped: false,
    };
    for _ in 0..150 {
        host.play(&pre, &post, QUIET);
    }
    let stalled = Instant::now() + WATCHDOG;
    while !post.keep() {
        assert!(Instant::now() < stalled, "Keep admission stopped");
        host.play(&pre, &post, QUIET);
    }
    host.arm(&pre, &post, QUIET);
    let signal =
        kirin_measure::record_signal::read_signal(&base, "offline-project", "offline-post")
            .unwrap();
    let mut wraps = 0;
    while wraps < 2 {
        if host.play(&pre, &post, QUIET) {
            wraps += 1;
        }
    }
    // Hold the producer while either worker is behind, instead of overflowing a slow worker's ring.
    let watchdog = Instant::now() + WATCHDOG;
    let mut position = 0;
    while position < FRAMES {
        let endpoints = [&pre, &post].map(keep_loop_host::latest_endpoint);
        if endpoints
            .iter()
            .any(|end| end.unwrap_or(0) + 24_000 < position as i64)
        {
            assert!(
                Instant::now() < watchdog,
                "offline Measure stopped: {endpoints:?} at {position}"
            );
            heartbeat(&pre, &post);
            sleep(Duration::from_millis(100));
            continue;
        }
        let frames = (BLOCK as u64).min(FRAMES - position);
        let samples = keep_loop_host::tone(QUIET, position as i64);
        for engine in [&pre, &post] {
            offline_block(engine, position, &samples[..frames as usize * 2]);
        }
        position += frames;
    }
    let stalled = Instant::now() + WATCHDOG;
    let idle_start = Instant::now();
    let mut idle_frames = 0_u64;
    while [&pre, &post].iter().any(|engine| {
        engine
            .poll_meter_history(MeterHistoryResolution::Hz10, 1)
            .and_then(|history| history.last()?.last_timeline_endpoint_samples)
            != Some(FRAMES as i64)
    }) {
        assert!(
            Instant::now() < stalled,
            "the offline audio stopped advancing through Measure: PRE {:?}, POST {:?}",
            keep_loop_host::latest_endpoint(&pre),
            keep_loop_host::latest_endpoint(&post)
        );
        heartbeat(&pre, &post);
        idle_frames += BLOCK as u64;
        let due = idle_start + Duration::from_secs_f64(idle_frames as f64 / RATE);
        if due > Instant::now() {
            sleep(due - Instant::now());
        }
    }
    drop_metadata(&base, &signal);
    post.stop();
    let stalled = Instant::now() + WATCHDOG;
    let records = loop {
        if let (Some(pre), Some(post)) = (
            closed(&base, "offline-pre", "pre", &signal.session_id),
            closed(&base, "offline-post", "post", &signal.session_id),
        ) {
            break [pre, post];
        }
        assert!(Instant::now() < stalled, "paired publication stopped");
        host.stopped(&pre, &post, 10);
    };
    for record in records {
        assert!(kirin_measure::plugin_data::verify_checksum(&record));
        assert!(record.trace_selected_capture_epoch.is_some());
        assert_eq!(record.trace_diagnostics.as_ref().unwrap().missing_slots, 0);
        assert!(
            record.record_quality.as_ref().unwrap().complete,
            "role={:?} reasons={:?} take={:?} frames={} first={:?} last={:?} diagnostics={:?} alignment={:?} passes={:?}",
            record.role, record.integrity_reasons, record.bounce_take, record.frames.len(),
            record.frames.first().map(|frame| frame.t_ms), record.frames.last().map(|frame| frame.t_ms),
            record.trace_diagnostics, record.trace_content_alignment, record.keep_passes
        );
        assert_eq!(record.frames.len(), 300);
        assert_eq!(record.frames.first().unwrap().t_ms, 100);
        assert_eq!(record.frames.last().unwrap().t_ms, 30_000);
        assert_eq!(
            record
                .keep_passes
                .iter()
                .filter(|pass| pass.in_trace)
                .count(),
            1
        );
        assert!(record
            .keep_passes
            .iter()
            .any(|pass| pass.trace_excluded_reason.as_deref() == Some("replaced_by_offline_take")));
    }
}
