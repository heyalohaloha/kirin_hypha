//! Producer-owned PRE/POST TRACE clock contract.
//!
//! TRACE is baked from the host's native sample position inside each Hypha instance. Offline
//! compensation pre-roll may process the same project range more than once; the producer resets
//! measurement state at each host discontinuity and keeps the last measurement for each native
//! sample slot. Pair publication only describes that already-canonical result. No content
//! correlation or consumer-side time shift exists in this path.

use serde::{Deserialize, Serialize};

use crate::plugin_data::PluginDataFile;

pub const TRACE_ALIGNMENT_METHOD: &str = "host_audio_sample_timeline_v2";
pub const TRACE_ALIGNMENT_STATUS: &str = "canonical_host_clock";
pub const TRACE_TIME_AXIS: &str = "host_audio_samples_v2";
pub const TRACE_CLOCK_BASIS: &str = "host_audio_callback_samples_v1";
const TRACE_FRAME_INTERVAL_MS: u64 = 100;

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct TraceContentAlignment {
    pub status: String,
    pub confidence: f64,
    pub compared_frame_count: u64,
    pub method: String,
}

pub(crate) fn canonical_host_alignment(
    left: &PluginDataFile,
    right: &PluginDataFile,
) -> Option<TraceContentAlignment> {
    if left.trace_time_axis.as_deref() != Some(TRACE_TIME_AXIS)
        || right.trace_time_axis.as_deref() != Some(TRACE_TIME_AXIS)
        || !matching_absolute_clock(left, right)
        || left.frames.is_empty()
        || right.frames.is_empty()
        || left.frames.len() != right.frames.len()
        || left.frames.iter().zip(&right.frames).enumerate().any(
            |(index, (left_frame, right_frame))| {
                let expected_t_ms = (index as u64 + 1).saturating_mul(TRACE_FRAME_INTERVAL_MS);
                left_frame.t_ms != expected_t_ms || right_frame.t_ms != expected_t_ms
            },
        )
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

fn matching_absolute_clock(left: &PluginDataFile, right: &PluginDataFile) -> bool {
    let (Some(left_clock), Some(right_clock)) = (&left.trace_clock, &right.trace_clock) else {
        return false;
    };
    left_clock.basis == TRACE_CLOCK_BASIS
        && right_clock.basis == TRACE_CLOCK_BASIS
        && left_clock.origin_position_samples == right_clock.origin_position_samples
        && left_clock.end_position_samples == right_clock.end_position_samples
        && left_clock.end_position_samples > left_clock.origin_position_samples
        && left_clock.sample_rate > 0
        && left_clock.sample_rate == right_clock.sample_rate
        && !left_clock.sources.is_empty()
        && !right_clock.sources.is_empty()
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::plugin_data::{PluginDataFile, Role, TraceClock};

    fn data(role: Role) -> PluginDataFile {
        let mut data = PluginDataFile::new(
            "installation".to_string(),
            "project".to_string(),
            format!("{role:?}"),
            role,
            None,
            48_000,
            None,
            None,
            None,
            None,
        );
        data.trace_time_axis = Some(TRACE_TIME_AXIS.to_string());
        data.trace_clock = Some(TraceClock {
            basis: TRACE_CLOCK_BASIS.to_string(),
            origin_position_samples: 96_000,
            end_position_samples: 100_800,
            sample_rate: 48_000,
            sources: vec!["project_timeline".to_string()],
        });
        data.frames.push(crate::plugin_data::Frame {
            t_ms: 100,
            n_prime: None,
            sharpness: None,
            lufs_m: -20.0,
            true_peak: -1.0,
            crest: 10.0,
            psr: None,
        });
        data
    }

    #[test]
    fn pair_contract_requires_both_producer_clocks() {
        let pre = data(Role::Pre);
        let mut post = data(Role::Post);
        assert!(canonical_host_alignment(&pre, &post).is_some());
        post.trace_time_axis = None;
        assert!(canonical_host_alignment(&pre, &post).is_none());
    }

    #[test]
    fn pair_contract_requires_one_shared_absolute_origin() {
        let pre = data(Role::Pre);
        let mut post = data(Role::Post);
        post.trace_clock.as_mut().unwrap().origin_position_samples += 512;
        assert!(canonical_host_alignment(&pre, &post).is_none());
    }

    #[test]
    fn pair_contract_rejects_invalid_absolute_ranges() {
        let pre = data(Role::Pre);
        let mut post = data(Role::Post);
        post.trace_clock.as_mut().unwrap().end_position_samples = 96_000;
        assert!(canonical_host_alignment(&pre, &post).is_none());
    }

    #[test]
    fn pair_contract_rejects_shifted_or_non_dense_grids() {
        let pre = data(Role::Pre);
        let mut post = data(Role::Post);
        post.frames[0].t_ms = 200;
        assert!(canonical_host_alignment(&pre, &post).is_none());
    }
}
