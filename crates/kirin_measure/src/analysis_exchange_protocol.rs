use std::path::{Path, PathBuf};

use serde::{Deserialize, Serialize};
use uuid::Uuid;

use crate::spectrum::{AnalysisViewMode, SpectrumChannelMode};

pub(super) const REQUEST_SCHEMA: &str = "kirin_hypha_analysis_request_v4";
const READY_SCHEMA: &str = "kirin_hypha_analysis_ready_v1";
pub(super) const JSON_MAX_BYTES: u64 = 2_048;

#[derive(Debug, Serialize, Deserialize)]
pub(super) struct AnalysisRequest {
    pub(super) schema: String,
    pub(super) request_id: String,
    pub(super) requested_by_post_instance_id: String,
    pub(super) target_pre_instance_id: String,
    pub(super) sample_rate: u32,
    pub(super) analysis_mode: u8,
    pub(super) channel_mode: u8,
    pub(super) state_epoch_samples: Option<i64>,
    pub(super) expires_at_unix_ms: i64,
}

#[derive(Debug, Serialize, Deserialize)]
pub(super) struct AnalysisReady {
    schema: String,
    request_id: String,
    target_pre_instance_id: String,
    sample_rate: u32,
    observed_presentation_end_samples: i64,
    rearm_required: bool,
    expires_at_unix_ms: i64,
}

impl AnalysisReady {
    pub(super) fn new(
        request_id: Uuid,
        target_pre_instance_id: &str,
        sample_rate: u32,
        observed_presentation_end_samples: i64,
        rearm_required: bool,
        expires_at_unix_ms: i64,
    ) -> Self {
        Self {
            schema: READY_SCHEMA.to_string(),
            request_id: request_id.to_string(),
            target_pre_instance_id: target_pre_instance_id.to_string(),
            sample_rate,
            observed_presentation_end_samples,
            rearm_required,
            expires_at_unix_ms,
        }
    }

    pub(super) fn matches(
        &self,
        request_id: Uuid,
        target_pre_instance_id: &str,
        sample_rate: u32,
        now_unix_ms: i64,
    ) -> bool {
        self.schema == READY_SCHEMA
            && self.request_id == request_id.to_string()
            && self.target_pre_instance_id == target_pre_instance_id
            && self.sample_rate == sample_rate
            && self.expires_at_unix_ms >= now_unix_ms
    }

    pub(super) fn observed_end(&self) -> i64 {
        self.observed_presentation_end_samples
    }

    pub(super) fn rearm_required(&self) -> bool {
        self.rearm_required
    }
}

pub(super) fn validated_request(
    instance_dir: &Path,
    pre_instance_id: &str,
    sample_rate: u32,
    now_unix_ms: i64,
) -> Option<(Uuid, AnalysisViewMode, SpectrumChannelMode, Option<i64>)> {
    let request = read_request(instance_dir)?;
    let analysis_mode = AnalysisViewMode::try_from(request.analysis_mode).ok()?;
    let channel_mode = SpectrumChannelMode::try_from(request.channel_mode).ok()?;
    (request.schema == REQUEST_SCHEMA
        && request.target_pre_instance_id == pre_instance_id
        && request.sample_rate == sample_rate
        && request.expires_at_unix_ms >= now_unix_ms
        && !request.requested_by_post_instance_id.is_empty())
    .then_some(())?;
    let request_id = Uuid::parse_str(&request.request_id).ok()?;
    if analysis_mode == AnalysisViewMode::Spectrum && request.state_epoch_samples.is_some() {
        return None;
    }
    if let Some(epoch) = request.state_epoch_samples {
        let aperture = i64::from(sample_rate / crate::PERCEPTUAL_PRESENTATION_HZ);
        if analysis_mode != AnalysisViewMode::Perceptual || epoch.rem_euclid(aperture) != 0 {
            return None;
        }
    }
    Some((
        request_id,
        analysis_mode,
        channel_mode,
        request.state_epoch_samples,
    ))
}

pub(super) fn write_request(instance_dir: &Path, request: &AnalysisRequest) -> std::io::Result<()> {
    let bytes = serde_json::to_vec(request).map_err(std::io::Error::other)?;
    crate::atomic_file::write_bytes_atomic(&request_path(instance_dir), &bytes)
}

pub(super) fn read_request(instance_dir: &Path) -> Option<AnalysisRequest> {
    serde_json::from_slice(&super::spectrum_exchange::codec::read_bounded(
        &request_path(instance_dir),
        JSON_MAX_BYTES,
    )?)
    .ok()
}

pub(super) fn write_ready(instance_dir: &Path, ready: &AnalysisReady) -> std::io::Result<()> {
    let bytes = serde_json::to_vec(ready).map_err(std::io::Error::other)?;
    crate::atomic_file::write_bytes_atomic(&ready_path(instance_dir), &bytes)
}

pub(super) fn read_ready(instance_dir: &Path) -> Option<AnalysisReady> {
    serde_json::from_slice(&super::spectrum_exchange::codec::read_bounded(
        &ready_path(instance_dir),
        JSON_MAX_BYTES,
    )?)
    .ok()
}

pub(super) fn remove_ready(instance_dir: &Path) {
    let _ = std::fs::remove_file(ready_path(instance_dir));
}

pub(super) fn request_path(instance_dir: &Path) -> PathBuf {
    instance_dir.join("spectrum").join("request.json")
}

fn ready_path(instance_dir: &Path) -> PathBuf {
    instance_dir.join("spectrum").join("ready.json")
}

pub(super) fn common_future_epoch(
    post_end: i64,
    pre_end: i64,
    aperture_samples: i64,
) -> Option<i64> {
    if aperture_samples <= 0 {
        return None;
    }
    let guarded = post_end
        .max(pre_end)
        .checked_add(aperture_samples.checked_mul(2)?)?;
    let remainder = guarded.rem_euclid(aperture_samples);
    if remainder == 0 {
        Some(guarded)
    } else {
        guarded.checked_add(aperture_samples - remainder)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn common_epoch_is_aligned_and_at_least_two_apertures_ahead() {
        assert_eq!(common_future_epoch(4_801, 9_601, 4_800), Some(24_000));
        assert_eq!(common_future_epoch(9_600, 4_800, 4_800), Some(19_200));
        assert_eq!(common_future_epoch(0, 0, 0), None);
        assert_eq!(common_future_epoch(i64::MAX, 0, 4_800), None);
    }

    #[test]
    fn request_rejects_missing_or_misaligned_state_contracts() {
        let temp = tempfile::tempdir().unwrap();
        let instance_dir = temp.path().join("pre");
        let request_id = Uuid::new_v4();
        let mut request = AnalysisRequest {
            schema: REQUEST_SCHEMA.to_string(),
            request_id: request_id.to_string(),
            requested_by_post_instance_id: "post".to_string(),
            target_pre_instance_id: "pre".to_string(),
            sample_rate: 48_000,
            analysis_mode: AnalysisViewMode::Perceptual as u8,
            channel_mode: SpectrumChannelMode::Lr as u8,
            state_epoch_samples: Some(9_600),
            expires_at_unix_ms: 10_000,
        };
        write_request(&instance_dir, &request).unwrap();
        assert_eq!(
            validated_request(&instance_dir, "pre", 48_000, 9_000),
            Some((
                request_id,
                AnalysisViewMode::Perceptual,
                SpectrumChannelMode::Lr,
                Some(9_600)
            ))
        );

        request.state_epoch_samples = Some(9_601);
        write_request(&instance_dir, &request).unwrap();
        assert!(validated_request(&instance_dir, "pre", 48_000, 9_000).is_none());
        request.analysis_mode = AnalysisViewMode::Spectrum as u8;
        request.state_epoch_samples = Some(9_600);
        write_request(&instance_dir, &request).unwrap();
        assert!(validated_request(&instance_dir, "pre", 48_000, 9_000).is_none());
    }

    #[test]
    fn readiness_is_bound_to_request_target_rate_and_expiry() {
        let request_id = Uuid::new_v4();
        let ready = AnalysisReady::new(request_id, "pre", 48_000, 4_800, false, 10_000);
        assert!(ready.matches(request_id, "pre", 48_000, 10_000));
        assert!(!ready.matches(Uuid::new_v4(), "pre", 48_000, 10_000));
        assert!(!ready.matches(request_id, "other", 48_000, 10_000));
        assert!(!ready.matches(request_id, "pre", 44_100, 10_000));
        assert!(!ready.matches(request_id, "pre", 48_000, 10_001));
    }
}
