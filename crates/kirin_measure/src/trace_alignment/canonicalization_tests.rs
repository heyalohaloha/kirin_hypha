use super::{
    canonical_frames_from_source, canonicalize_pair_trace_content_alignment,
    TRACE_CONTENT_ALIGNMENT_CANONICALIZED,
};
use crate::plugin_data::{BounceTake, Frame, PluginDataFile, Role, TraceDiagnostics};

fn frame(index: usize, lufs: f64, peak: f64) -> Frame {
    Frame {
        t_ms: (index as u64 + 1) * 100,
        n_prime: Some([0.0; 20]),
        sharpness: Some(0.0),
        lufs_m: lufs,
        true_peak: peak,
        crest: 10.0,
        psr: None,
    }
}

fn source(index: usize) -> (f64, f64) {
    let x = index as f64;
    let lufs_jitter = ((index * 37) % 29) as f64 - 14.0;
    let peak_jitter = ((index * 53) % 31) as f64 - 15.0;
    let lufs = -22.0 + (x * 0.37).sin() * 4.0 + lufs_jitter * 0.45;
    let peak = -8.0 + (x * 0.53).sin() * 3.0 + peak_jitter * 0.30;
    (lufs, peak)
}

fn pair_data(role: Role, instance_id: &str) -> PluginDataFile {
    let mut data = PluginDataFile::new(
        "installation".to_string(),
        "project".to_string(),
        instance_id.to_string(),
        role,
        None,
        96_000,
        None,
        None,
        None,
        None,
    );
    data.bounce_take = Some(BounceTake {
        source: "expected_wav_duration_native".to_string(),
        time_axis: "native_samples".to_string(),
        alignment_status: "sample_count_ready".to_string(),
        sample_rate: 96_000,
        wav_start_sample: 0,
        wav_end_sample: 1_440_000,
        duration_samples: 1_440_000,
        duration_frames_48k: 720_000,
        start_t_ms: 0,
        end_t_ms: 15_000,
        trace_sample_count: 160,
        frame_count: 150,
    });
    data.trace_diagnostics = Some(TraceDiagnostics {
        raw_trace_count: 160,
        expected_frame_count: 150,
        measured_frame_count: 150,
        missing_slots: 0,
        explicit_silence_frame_count: 0,
    });
    data
}

#[test]
fn captured_tail_produces_full_canonical_wav_axis() {
    let source: Vec<_> = (0_usize..151)
        .map(|index| {
            let (lufs, peak) = source(index.saturating_sub(1));
            frame(index, lufs, peak)
        })
        .collect();
    let canonical = canonical_frames_from_source(&source, -100, 15_000).unwrap();
    assert_eq!(canonical.len(), 150);
    assert_eq!(canonical.first().map(|frame| frame.t_ms), Some(100));
    assert_eq!(canonical.last().map(|frame| frame.t_ms), Some(15_000));
    assert_eq!(
        canonical.last().map(|frame| frame.lufs_m),
        Some(source[150].lufs_m)
    );
}

#[test]
fn pair_finalize_uses_tail_context_and_publishes_canonical_frames() {
    let mut pre = pair_data(Role::Pre, "pre");
    let mut post = pair_data(Role::Post, "post");
    pre.trace_context_frames = (0..160)
        .map(|index| {
            let (lufs, peak) = source(index);
            frame(index, lufs, peak)
        })
        .collect();
    post.trace_context_frames = vec![frame(0, -100.0, -100.0)];
    post.trace_context_frames.extend((0..159).map(|index| {
        let (lufs, peak) = source(index);
        frame(index + 1, lufs + 4.0, peak - 2.0)
    }));
    pre.frames = pre.trace_context_frames[..150].to_vec();
    post.frames = post.trace_context_frames[..150].to_vec();

    let alignment = canonicalize_pair_trace_content_alignment(&mut pre, &mut post);

    assert_eq!(alignment.status, TRACE_CONTENT_ALIGNMENT_CANONICALIZED);
    assert_eq!(alignment.post_offset_ms, 0);
    assert_eq!(alignment.source_post_offset_ms, -100);
    assert_eq!(pre.frames.len(), 150);
    assert_eq!(post.frames.len(), 150);
    assert_eq!(post.frames.first().map(|frame| frame.t_ms), Some(100));
    assert_eq!(post.frames.last().map(|frame| frame.t_ms), Some(15_000));
    assert_eq!(post.frames[0].lufs_m, pre.frames[0].lufs_m + 4.0);
    assert!(pre.trace_context_frames.is_empty());
    assert!(post.trace_context_frames.is_empty());
}

#[test]
fn pair_finalize_canonicalizes_when_pre_is_the_delayed_side() {
    let mut pre = pair_data(Role::Pre, "pre");
    let mut post = pair_data(Role::Post, "post");
    pre.trace_context_frames = vec![frame(0, -100.0, -100.0)];
    pre.trace_context_frames.extend((0..159).map(|index| {
        let (lufs, peak) = source(index);
        frame(index + 1, lufs, peak)
    }));
    post.trace_context_frames = (0..160)
        .map(|index| {
            let (lufs, peak) = source(index);
            frame(index, lufs + 4.0, peak - 2.0)
        })
        .collect();
    pre.frames = pre.trace_context_frames[..150].to_vec();
    post.frames = post.trace_context_frames[..150].to_vec();

    let alignment = canonicalize_pair_trace_content_alignment(&mut pre, &mut post);

    assert_eq!(alignment.status, TRACE_CONTENT_ALIGNMENT_CANONICALIZED);
    assert_eq!(alignment.post_offset_ms, 0);
    assert_eq!(alignment.source_post_offset_ms, 100);
    assert_eq!(pre.frames.len(), 150);
    assert_eq!(post.frames.len(), 150);
    assert_eq!(pre.frames.first().map(|frame| frame.t_ms), Some(100));
    assert_eq!(pre.frames.last().map(|frame| frame.t_ms), Some(15_000));
    assert_eq!(post.frames[0].lufs_m, pre.frames[0].lufs_m + 4.0);
    assert!(pre.trace_context_frames.is_empty());
    assert!(post.trace_context_frames.is_empty());
}
