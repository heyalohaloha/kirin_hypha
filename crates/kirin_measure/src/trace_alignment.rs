//! Producer-owned PRE/POST TRACE axis contract.
//!
//! Every metric frame is formed on the DAW host transport/render sample position. Optional
//! presentation-latency callbacks are diagnostic and never rebase either lane. Pair finalization
//! reads PRE and POST only at equal host positions, then publishes that producer-owned grid on
//! dropped-WAV sample 0..N. Metric values never participate in the clock decision.

use serde::{Deserialize, Serialize};

use crate::plugin_data::PluginDataFile;

pub const TRACE_ALIGNMENT_METHOD: &str = "producer_wav_sample_axis_v8";
pub const TRACE_ALIGNMENT_STATUS: &str = "canonical_wav_clock";
pub const TRACE_ALIGNMENT_START_BWF: &str = "bwf_time_reference";
pub const TRACE_ALIGNMENT_START_RENDER_RANGE: &str = "producer_render_range";
pub const TRACE_TIME_AXIS: &str = "wav_samples_v1";
pub const TRACE_HOST_TIME_AXIS: &str = "host_content_samples_v3";
pub const TRACE_CLOCK_BASIS: &str = "input_presentation_content_samples_v1";
pub const TRACE_WAV_REFERENCE_BASIS: &str = "dropped_wav_sample_index_v1";
pub const TRACE_CAPTURE_BASIS: &str = "paired_record_session_v1";
const TRACE_FRAME_INTERVAL_MS: u64 = 100;

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct TraceContentAlignment {
    pub status: String,
    pub confidence: f64,
    pub compared_frame_count: u64,
    pub method: String,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub start_basis: Option<String>,
    /// Legacy v2 diagnostic. Current artifacts never publish a curve-derived correction.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub pre_to_post_offset_samples: Option<i64>,
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
        || !has_shared_native_slots(left, right)
        || !has_wav_bounded_slots(left)
        || !has_wav_bounded_slots(right)
    {
        return None;
    }
    let start_basis = left.trace_pair_wav_start_basis.as_deref()?;
    if ![
        TRACE_ALIGNMENT_START_BWF,
        TRACE_ALIGNMENT_START_RENDER_RANGE,
    ]
    .contains(&start_basis)
        || right.trace_pair_wav_start_basis.as_deref() != Some(start_basis)
    {
        return None;
    }
    // A BWF time reference plus the identical native slots is the final sample-axis proof. The
    // per-instance host clock remains diagnostic and can legitimately be absent on AU when its
    // first callback is a partial 100 ms block. Without `bext`, the render-range fallback still
    // requires matching host-clock provenance.
    if start_basis == TRACE_ALIGNMENT_START_RENDER_RANGE
        && !crate::trace_content_clock::has_compatible_host_clocks(left, right)
    {
        return None;
    }
    Some(TraceContentAlignment {
        status: TRACE_ALIGNMENT_STATUS.to_string(),
        confidence: 1.0,
        compared_frame_count: left.frames.len() as u64,
        method: TRACE_ALIGNMENT_METHOD.to_string(),
        start_basis: Some(start_basis.to_string()),
        pre_to_post_offset_samples: None,
    })
}

fn has_shared_native_slots(left: &PluginDataFile, right: &PluginDataFile) -> bool {
    if left.sample_rate == 0
        || left.sample_rate != right.sample_rate
        || left.trace_slot_positions.len() != left.frames.len()
        || left.trace_slot_positions != right.trace_slot_positions
    {
        return false;
    }
    let slot_samples = (left.sample_rate as i64 / 10).max(1);
    left.trace_slot_positions
        .windows(2)
        .all(|pair| pair[1].saturating_sub(pair[0]) == slot_samples)
}

fn has_wav_bounded_slots(data: &PluginDataFile) -> bool {
    let (Some(reference), Some(first), Some(last)) = (
        data.trace_wav_reference.as_ref(),
        data.trace_slot_positions.first().copied(),
        data.trace_slot_positions.last().copied(),
    ) else {
        return false;
    };
    let slot_samples = (data.sample_rate as i64 / 10).max(1);
    let Ok(frame_count) = i64::try_from(data.trace_slot_positions.len()) else {
        return false;
    };
    let Some(duration_samples) = reference.end_sample.checked_sub(reference.start_sample) else {
        return false;
    };
    let Ok(reference_duration) = i64::try_from(duration_samples) else {
        return false;
    };
    let Some(measured_span) = frame_count.checked_mul(slot_samples) else {
        return false;
    };
    let Some(unmeasured_tail) = reference_duration.checked_sub(measured_span) else {
        return false;
    };
    first == slot_samples && last == measured_span && unmeasured_tail < slot_samples
}

pub(crate) fn has_canonical_wav_reference(data: &PluginDataFile) -> bool {
    if data.trace_time_axis.as_deref() != Some(TRACE_TIME_AXIS) {
        return false;
    }
    let (Some(reference), Some(expected), Some(session_id)) = (
        data.trace_wav_reference.as_ref(),
        data.expected_wav.as_ref(),
        data.record_session_id.as_deref(),
    ) else {
        return false;
    };
    let Some(expected_hash) = expected.wav_hash.as_deref().map(str::trim) else {
        return false;
    };
    expected.is_complete()
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
        Frame, HostPresentationLatencyObservation, PluginDataFile, Role, TraceClock,
        TraceDiagnostics, TraceWavReference,
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
            wav_time_reference_samples: None,
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
        data.host_presentation_latency_observations = vec![HostPresentationLatencyObservation {
            source: Some("vst3".to_string()),
            input_samples: Some(if role == Role::Pre { 0 } else { 256 }),
            output_samples: None,
        }];
        data.trace_clock = Some(TraceClock {
            basis: TRACE_CLOCK_BASIS.to_string(),
            origin_position_samples: 6_473_347,
            end_position_samples: 6_482_947,
            sample_rate: 96_000,
            sources: vec!["project_timeline".to_string()],
        });
        data.trace_slot_positions = vec![9_600];
        data.trace_pair_wav_start_basis = Some(TRACE_ALIGNMENT_START_BWF.to_string());
        data.expected_wav
            .as_mut()
            .unwrap()
            .wav_time_reference_samples = Some(6_473_347);
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
    fn pair_contract_requires_shared_slots_even_when_instance_diagnostics_differ() {
        let pre = data(Role::Pre);
        let mut post = data(Role::Post);
        post.trace_clock.as_mut().unwrap().origin_position_samples -= 7_676;
        post.trace_clock.as_mut().unwrap().end_position_samples -= 7_676;

        let alignment = canonical_wav_alignment(&pre, &post).expect("v3 alignment");
        assert_eq!(alignment.method, TRACE_ALIGNMENT_METHOD);
        assert_eq!(alignment.confidence, 1.0);
        assert_eq!(
            alignment.start_basis.as_deref(),
            Some(TRACE_ALIGNMENT_START_BWF)
        );
        assert!(alignment.pre_to_post_offset_samples.is_none());

        post.trace_slot_positions[0] -= 7_676;
        assert!(canonical_wav_alignment(&pre, &post).is_none());
    }

    #[test]
    fn pair_contract_requires_one_shared_wav_identity() {
        let pre = data(Role::Pre);
        let mut post = data(Role::Post);
        post.trace_wav_reference.as_mut().unwrap().wav_hash = "other-hash".to_string();

        assert!(canonical_wav_alignment(&pre, &post).is_none());
    }

    #[test]
    fn pair_contract_does_not_treat_optional_latency_callbacks_as_start_proof() {
        let pre = data(Role::Pre);
        let mut post = data(Role::Post);
        post.host_presentation_latency_observations.clear();
        assert!(canonical_wav_alignment(&pre, &post).is_some());

        post.host_presentation_latency_observations = vec![HostPresentationLatencyObservation {
            source: Some("vst3".to_string()),
            input_samples: None,
            output_samples: Some(256),
        }];
        assert!(canonical_wav_alignment(&pre, &post).is_some());
    }

    #[test]
    fn bwf_pair_contract_does_not_require_optional_au_host_clock() {
        let pre = data(Role::Pre);
        let mut post = data(Role::Post);
        post.trace_clock = None;

        assert!(canonical_wav_alignment(&pre, &post).is_some());

        let mut render_pre = pre;
        let mut render_post = post;
        for side in [&mut render_pre, &mut render_post] {
            side.expected_wav
                .as_mut()
                .unwrap()
                .wav_time_reference_samples = None;
            side.trace_pair_wav_start_basis = Some(TRACE_ALIGNMENT_START_RENDER_RANGE.to_string());
        }
        assert!(canonical_wav_alignment(&render_pre, &render_post).is_none());
    }

    #[test]
    fn public_wav_axis_does_not_reintroduce_the_absolute_bwf_offset() {
        let mut pre = data(Role::Pre);
        let mut post = data(Role::Post);
        for side in [&mut pre, &mut post] {
            side.expected_wav
                .as_mut()
                .unwrap()
                .wav_time_reference_samples = Some(6_473_347);
            side.trace_pair_wav_start_basis = Some(TRACE_ALIGNMENT_START_BWF.to_string());
        }

        let alignment = canonical_wav_alignment(&pre, &post).expect("BWF anchored alignment");
        assert_eq!(
            alignment.start_basis.as_deref(),
            Some(TRACE_ALIGNMENT_START_BWF)
        );

        for side in [&mut pre, &mut post] {
            side.expected_wav
                .as_mut()
                .unwrap()
                .wav_time_reference_samples = Some(6_473_348);
        }
        // The producer already used the BWF reference to select the common source frames. The
        // published grid remains 0-relative to the dropped WAV and must not be shifted again by
        // that absolute DAW position in a consumer.
        assert!(canonical_wav_alignment(&pre, &post).is_some());
    }

    #[test]
    fn pair_contract_accepts_exact_producer_render_range_for_a_non_bwf_wav() {
        let mut pre = data(Role::Pre);
        let mut post = data(Role::Post);
        for side in [&mut pre, &mut post] {
            side.expected_wav
                .as_mut()
                .unwrap()
                .wav_time_reference_samples = None;
            side.trace_pair_wav_start_basis = Some(TRACE_ALIGNMENT_START_RENDER_RANGE.to_string());
        }

        let alignment =
            canonical_wav_alignment(&pre, &post).expect("producer render-range alignment");
        assert_eq!(alignment.method, TRACE_ALIGNMENT_METHOD);
        assert_eq!(
            alignment.start_basis.as_deref(),
            Some(TRACE_ALIGNMENT_START_RENDER_RANGE)
        );
    }

    #[test]
    fn pair_contract_accepts_a_wav_with_an_unmeasured_sub_frame_tail() {
        let mut pre = data(Role::Pre);
        let mut post = data(Role::Post);
        for side in [&mut pre, &mut post] {
            side.expected_wav
                .as_mut()
                .unwrap()
                .expected_duration_samples = 10_000;
            side.expected_wav
                .as_mut()
                .unwrap()
                .wav_time_reference_samples = Some(6_473_347);
            side.trace_wav_reference.as_mut().unwrap().end_sample = 10_000;
            side.trace_pair_wav_start_basis = Some(TRACE_ALIGNMENT_START_BWF.to_string());
        }

        assert!(canonical_wav_alignment(&pre, &post).is_some());

        for side in [&mut pre, &mut post] {
            side.expected_wav
                .as_mut()
                .unwrap()
                .expected_duration_samples = 19_200;
            side.trace_wav_reference.as_mut().unwrap().end_sample = 19_200;
        }
        assert!(canonical_wav_alignment(&pre, &post).is_none());
    }

    #[test]
    fn bwf_pair_contract_treats_instance_clock_ranges_as_diagnostics() {
        let mut pre = data(Role::Pre);
        let mut post = data(Role::Post);
        let clock = post.trace_clock.as_mut().unwrap();
        clock.end_position_samples = clock.origin_position_samples;

        assert!(canonical_wav_alignment(&pre, &post).is_some());

        post.trace_clock.as_mut().unwrap().sources = vec!["different_clock".to_string()];
        assert!(canonical_wav_alignment(&pre, &post).is_some());

        pre.trace_clock = None;
        post.trace_clock = None;
        assert!(canonical_wav_alignment(&pre, &post).is_some());
    }

    #[test]
    fn pair_contract_rejects_shifted_or_non_dense_wav_grids() {
        let pre = data(Role::Pre);
        let mut post = data(Role::Post);
        post.frames[0].t_ms = 200;

        assert!(canonical_wav_alignment(&pre, &post).is_none());
    }

    #[test]
    fn legacy_curve_correlated_alignment_still_deserializes() {
        let alignment: TraceContentAlignment = serde_json::from_str(
            r#"{"status":"canonical_wav_clock","confidence":0.94,"compared_frame_count":150,"method":"producer_content_correlated_slots_v2","pre_to_post_offset_samples":9600}"#,
        )
        .unwrap();

        assert!(alignment.start_basis.is_none());
        assert_eq!(alignment.pre_to_post_offset_samples, Some(9_600));
    }
}
