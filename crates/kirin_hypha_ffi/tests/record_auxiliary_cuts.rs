//! Real paired Keep admission, Measure/IO drain and canonical TRACE output.
//! Simulated clock facts; this is independent of actual DAW/format acceptance.
use kirin_hypha_ffi::{channel_abi::ChannelLayout, KirinHyphaEngine, KIRIN_KEEP_PHASE_ARMED};
use kirin_measure::{
    AuxiliaryClockSamples, AuxiliaryClockSource, CaptureClockSource, PresentationLatencySamples,
    PresentationLatencySource, RecordTakeBlock,
};
use std::{
    path::Path,
    thread::sleep,
    time::{Duration, Instant},
};

fn push(engine: &KirinHyphaEngine, samples: &[f32], position: i64, index: i64, cuts: bool) {
    let recording = engine.is_recording();
    engine.note_record_block(RecordTakeBlock {
        generation: 0,
        recording,
        rendered: recording,
        playing: true,
        offline: false,
        position_valid: true,
        position_samples: position,
        num_frames: 512,
        clock_start_samples: 0,
        clock_end_samples: None,
    });
    engine.note_capture_window_with_clocks(
        true,
        position,
        512,
        CaptureClockSource::ProjectTimeline,
        PresentationLatencySamples {
            source: PresentationLatencySource::AudioUnitV2,
            input: Some(0),
            output: Some(0),
        },
        AuxiliaryClockSamples {
            source: AuxiliaryClockSource::AudioUnitRender,
            samples: Some(position + if cuts { (index % 2) * 512 } else { 0 }),
        },
        false,
    );
    engine.push_samples(samples, 2);
}
fn collect(path: &Path, results: &mut Vec<serde_json::Value>) {
    for entry in std::fs::read_dir(path).unwrap() {
        let p = entry.unwrap().path();
        if p.is_dir() {
            collect(&p, results)
        } else if p.extension().is_some_and(|x| x == "json") {
            if let Ok(v) = serde_json::from_slice::<serde_json::Value>(&std::fs::read(p).unwrap()) {
                if v["schema_version"] == "1.3" && v["frames"].is_array() {
                    results.push(v)
                }
            }
        }
    }
}
#[test]
#[ignore = "slow: real PRE/POST Keep and TRACE output under repeated auxiliary clock cuts"]
fn continuous_keep_keeps_every_trace_slot_across_auxiliary_cuts() {
    let cuts = true;
    let sandbox = tempfile::tempdir().unwrap();
    std::env::set_var(kirin_measure::TEST_STORAGE_ROOT_ENV, sandbox.path());
    let paths = kirin_measure::PlatformPaths::default_current()
        .unwrap()
        .storage;
    std::fs::create_dir_all(&paths.kirin_os_root).unwrap();
    std::fs::write(paths.primary_path(),br#"{"schema_version":"1.0","installation_id":"aux-output-fixture","hardware_id":"hw","hardware_components":{"iop":"a","sn":"b","bd":"c"},"machine_signature":"sig","license":"os","created_at":"2026-06-09T00:00:00Z","last_verified_at":"2026-06-09T00:00:00Z"}"#).unwrap();
    let block: Vec<f32> = (0..512)
        .flat_map(|i| {
            let x = (0.2 * (std::f64::consts::TAU * 1000.0 * i as f64 / 48000.0).sin()) as f32;
            [x, x]
        })
        .collect();
    let pre = KirinHyphaEngine::new(48000, ChannelLayout::stereo());
    pre.set_identity(
        "probe-pre".into(),
        "probe-pre-project".into(),
        "".into(),
        "mix".into(),
    );
    pre.enable_pre_writes();
    pre.set_signal_state(1);
    let post = KirinHyphaEngine::new(48000, ChannelLayout::stereo());
    post.set_identity(
        "probe-post".into(),
        "probe-post-project".into(),
        "".into(),
        "mix".into(),
    );
    post.enable_post_writes();
    post.set_signal_state(1);
    post.set_pair_target("mix".into());
    let mut position = 0;
    let start = Instant::now();
    for i in 0..150_i64 {
        push(&pre, &block, position, i, false);
        push(&post, &block, position, i, false);
        position += 512;
        let due = start + Duration::from_secs_f64((i + 1) as f64 * 512.0 / 48000.0);
        if due > Instant::now() {
            sleep(due - Instant::now())
        }
    }
    assert!(post.keep(), "unique private PRE accepted Keep");
    let deadline = Instant::now() + Duration::from_secs(8);
    while post.keep_phase() != KIRIN_KEEP_PHASE_ARMED || !post.is_recording() {
        assert!(Instant::now() < deadline, "ARMED deadline");
        pre.push_samples(&[], 2);
        post.push_samples(&[], 2);
        sleep(Duration::from_millis(20));
    }
    let start = Instant::now();
    for i in 0..1125_i64 {
        push(&pre, &block, position, i, cuts);
        push(&post, &block, position, i, cuts);
        position += 512;
        let due = start + Duration::from_secs_f64((i + 1) as f64 * 512.0 / 48000.0);
        if due > Instant::now() {
            sleep(due - Instant::now())
        }
    }
    post.stop();
    sleep(Duration::from_secs(3));
    drop(post);
    drop(pre);
    let mut output = Vec::new();
    collect(&paths.plugin_data_dir(), &mut output);
    let expected_slots = 1125 * 512 / 4800;
    for role in ["PRE", "POST"] {
        let members: Vec<_> = output.iter().filter(|v| v["role"] == role).collect();
        assert!(!members.is_empty(), "actual {role} TRACE publication");
        for member in members {
            let typed: kirin_measure::plugin_data::PluginDataFile =
                serde_json::from_value(member.clone()).unwrap();
            assert!(kirin_measure::plugin_data::verify_checksum(&typed));
            let frames = member["frames"].as_array().unwrap();
            let quality = &member["record_quality"];
            eprintln!("{role}: {} slots, quality={quality}", frames.len());
            assert_eq!(
                frames.len(),
                expected_slots,
                "all factual {role} slots survive drain"
            );
            assert_eq!(quality["expected_frame_count"], expected_slots);
            assert_eq!(quality["measured_frame_count"], expected_slots);
            assert_eq!(quality["missing_trace_slots"], 0);
            assert_eq!(quality["trace_slots_complete"], true);
            assert_eq!(quality["expected_wav_ready"], false);
            // The existing contract accepts a complete producer-owned render-clock take.
            // WAV association stays explicit and is independent of sample-count readiness.
            assert_eq!(quality["sample_count_ready"], true);
            assert_eq!(quality["complete"], true);
            assert_eq!(member["bounce_take"]["source"], "render_clock_native");
            assert_eq!(member["bounce_take"]["duration_samples"], 1125 * 512);
            for pair in frames.windows(2) {
                assert_eq!(
                    pair[1]["t_ms"].as_u64().unwrap() - pair[0]["t_ms"].as_u64().unwrap(),
                    100
                );
            }
        }
    }
}
