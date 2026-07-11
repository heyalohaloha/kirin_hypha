//! Producer-owned PRE/POST TRACE axis contract.
//!
//! Host callback positions are instance-local diagnostics. DAWs may report different positions
//! for PRE and POST because of PDC, callback scheduling, or wrapper behavior, so those absolute
//! positions must never be used as a pair-wide origin. The final TRACE coordinate system is the
//! exact dropped WAV: sample 0 through its header-derived sample count.

use serde::{Deserialize, Serialize};

use crate::plugin_data::PluginDataFile;

pub const TRACE_ALIGNMENT_METHOD: &str = "paired_render_pass_wav_timeline_v1";
pub const TRACE_ALIGNMENT_STATUS: &str = "canonical_wav_clock";
pub const TRACE_TIME_AXIS: &str = "wav_samples_v1";
pub const TRACE_HOST_TIME_AXIS: &str = "host_audio_samples_v2";
pub const TRACE_CLOCK_BASIS: &str = "host_audio_callback_samples_v1";
pub const TRACE_WAV_REFERENCE_BASIS: &str = "dropped_wav_sample_index_v1";
pub const TRACE_CAPTURE_BASIS: &str = "paired_record_session_v1";
const TRACE_FRAME_INTERVAL_MS: u64 = 100;

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct TraceContentAlignment {
    pub status: String,
    pub confidence: f64,
    pub compared_frame_count: u64,
    pub method: String,
}

pub(crate) fn canonical_wav_alignment(
    left: &PluginDataFile,
    right: &PluginDataFile,
) -> Option<TraceContentAlignment> {
    if !has_canonical_wav_reference(left)
        || !has_canonical_wav_reference(right)
        || left.trace_wav_reference != right.trace_wav_reference
        || left.expected_wav != right.expected_wav
        || left.frames.is_empty()
        || left.frames.len() != right.frames.len()
        || !has_dense_frame_grid(left)
        || !has_dense_frame_grid(right)
    {
        return None;
    }
    Some(TraceContentAlignment {
        status: TRACE_ALIGNMENT_STATUS.to_string(),
        confidence: 1.0,
        compared_frame_count: left.frames.len() as u64,
        method: TRACE_ALIGNMENT_METHOD.to_string(),
    })
}

pub(crate) fn has_canonical_wav_reference(data: &PluginDataFile) -> bool {
    if data.trace_time_axis.as_deref() != Some(TRACE_TIME_AXIS) {
        return false;
    }
    let (Some(reference), Some(expected), Some(clock), Some(session_id)) = (
        data.trace_wav_reference.as_ref(),
        data.expected_wav.as_ref(),
        data.trace_clock.as_ref(),
        data.record_session_id.as_deref(),
    ) else {
        return false;
    };
    let Some(expected_hash) = expected.wav_hash.as_deref().map(str::trim) else {
        return false;
    };
    let valid_host_diagnostic = clock.basis == TRACE_CLOCK_BASIS
        && clock.end_position_samples > clock.origin_position_samples
        && clock.sample_rate == data.sample_rate
        && clock.sample_rate > 0
        && !clock.sources.is_empty()
        && clock.sources.iter().all(|source| !source.trim().is_empty());
    expected.is_complete()
        && valid_host_diagnostic
        && reference.basis == TRACE_WAV_REFERENCE_BASIS
        && reference.capture_basis == TRACE_CAPTURE_BASIS
        && reference.start_sample == 0
        && reference.end_sample == expected.expected_duration_samples
        && reference.end_sample > reference.start_sample
        && reference.sample_rate == expected.expected_sample_rate
        && reference.sample_rate == data.sample_rate
        && !session_id.trim().is_empty()
        && reference.record_session_id == session_id
        && reference.bounce_id == expected.bounce_id
        && !expected_hash.is_empty()
        && reference.wav_hash == expected_hash
}

fn has_dense_frame_grid(data: &PluginDataFile) -> bool {
    data.frames.iter().enumerate().all(|(index, frame)| {
        frame.t_ms == (index as u64 + 1).saturating_mul(TRACE_FRAME_INTERVAL_MS)
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::plugin_data::{
        Frame, PluginDataFile, Role, TraceClock, TraceDiagnostics, TraceWavReference,
    };
    use crate::record_expected::ExpectedWavMetadata;

    fn data(role: Role) -> PluginDataFile {
        let mut data = PluginDataFile::new(
            "installation".to_string(),
            "project".to_string(),
            format!("{role:?}"),
            role,
            None,
            96_000,
            None,
            None,
            None,
            None,
        );
        data.record_session_id = Some("record-session".to_string());
        data.expected_wav = Some(ExpectedWavMetadata {
            expected_duration_samples: 9_600,
            expected_sample_rate: 96_000,
            wav_path: "/tmp/drop.wav".to_string(),
            bounce_id: "bounce-id".to_string(),
            created_at_ms: 1,
            wav_file_size: Some(19_244),
            wav_mtime_ms: 1,
            wav_hash: Some("wav-hash".to_string()),
            consumed_at_ms: None,
            consumed_by_session_id: None,
        });
        data.trace_time_axis = Some(TRACE_TIME_AXIS.to_string());
        data.trace_clock = Some(TraceClock {
            basis: TRACE_CLOCK_BASIS.to_string(),
            origin_position_samples: 6_473_347,
            end_position_samples: 6_482_947,
            sample_rate: 96_000,
            sources: vec!["project_timeline".to_string()],
        });
        data.trace_wav_reference = Some(TraceWavReference {
            basis: TRACE_WAV_REFERENCE_BASIS.to_string(),
            capture_basis: TRACE_CAPTURE_BASIS.to_string(),
            start_sample: 0,
            end_sample: 9_600,
            sample_rate: 96_000,
            record_session_id: "record-session".to_string(),
            bounce_id: "bounce-id".to_string(),
            wav_hash: "wav-hash".to_string(),
        });
        data.frames.push(Frame {
            t_ms: 100,
            n_prime: None,
            sharpness: None,
            lufs_m: -20.0,
            true_peak: -1.0,
            crest: 10.0,
            psr: None,
        });
        data.trace_diagnostics = Some(TraceDiagnostics {
            raw_trace_count: 1,
            expected_frame_count: 1,
            measured_frame_count: 1,
            missing_slots: 0,
            explicit_silence_frame_count: 0,
        });
        data
    }

    #[test]
    fn pair_contract_accepts_studio_one_instance_local_host_offsets() {
        let pre = data(Role::Pre);
        let mut post = data(Role::Post);
        post.trace_clock.as_mut().unwrap().origin_position_samples -= 7_676;
        post.trace_clock.as_mut().unwrap().end_position_samples -= 7_676;

        assert!(canonical_wav_alignment(&pre, &post).is_some());
    }

    #[test]
    fn pair_contract_requires_one_shared_wav_identity() {
        let pre = data(Role::Pre);
        let mut post = data(Role::Post);
        post.trace_wav_reference.as_mut().unwrap().wav_hash = "other-hash".to_string();

        assert!(canonical_wav_alignment(&pre, &post).is_none());
    }

    #[test]
    fn pair_contract_rejects_invalid_instance_host_diagnostics() {
        let pre = data(Role::Pre);
        let mut post = data(Role::Post);
        let clock = post.trace_clock.as_mut().unwrap();
        clock.end_position_samples = clock.origin_position_samples;

        assert!(canonical_wav_alignment(&pre, &post).is_none());
    }

    #[test]
    fn pair_contract_rejects_shifted_or_non_dense_wav_grids() {
        let pre = data(Role::Pre);
        let mut post = data(Role::Post);
        post.frames[0].t_ms = 200;

        assert!(canonical_wav_alignment(&pre, &post).is_none());
    }
}
