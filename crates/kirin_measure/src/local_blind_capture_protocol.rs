//! Name-independent, exact-pair control handshake for one Local Blind capture.
//!
//! This module carries bounded metadata on non-RT threads. It does not move PCM, grant an
//! Analysis lease, start an audition, or infer a PRE from a human name or host context.

use std::path::{Path, PathBuf};

use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};
use uuid::Uuid;

use crate::analysis_exchange_transport::{self, AnalysisSlot};
use crate::pair_claim_index::StablePairClaimObservation;

const REQUEST_SCHEMA: &str = "kirin_hypha_local_blind_capture_request_v1";
const ARMED_SCHEMA: &str = "kirin_hypha_local_blind_capture_armed_v1";
const REQUEST_MAX_BYTES: u64 = 4_096;
const ARMED_MAX_BYTES: u64 = 2_048;
const MAX_LEASE_MS: i64 = 15_000;
const MAX_CAPTURE_SECONDS: i64 = 4;

#[derive(Clone, Copy)]
struct CaptureTarget<'a> {
    kirin_root: &'a Path,
    instance_dir: &'a Path,
    pre_project_hash: &'a str,
    pre_instance_id: &'a str,
    sample_rate: u32,
    channels: u8,
}

#[derive(Clone, Debug, Deserialize, Eq, PartialEq, Serialize)]
pub struct LocalBlindPairAuthority {
    pub pair_generation: u64,
    pub pair_owner_id: String,
    pub pair_claimed_at_bits: u64,
    pub host_process_id: u32,
    pub post_project_hash: String,
    pub post_instance_id: String,
    pub pre_project_hash: String,
    pub pre_instance_id: String,
}

impl LocalBlindPairAuthority {
    fn valid_shape(&self) -> bool {
        self.pair_generation != 0
            && self.host_process_id != 0
            && canonical_uuid(&self.pair_owner_id)
            && valid_component(&self.post_project_hash)
            && valid_component(&self.post_instance_id)
            && valid_component(&self.pre_project_hash)
            && valid_component(&self.pre_instance_id)
            && f64::from_bits(self.pair_claimed_at_bits).is_finite()
            && f64::from_bits(self.pair_claimed_at_bits) > 0.0
    }

    fn matches_current_claim(&self, kirin_root: &Path) -> bool {
        match crate::pair_claim_index::try_observe_pair_claim(
            kirin_root,
            self.host_process_id,
            &self.pre_instance_id,
        ) {
            Ok(StablePairClaimObservation::Stable {
                claim: Some(claim),
                owned: true,
            }) => {
                claim.pre_instance_id == self.pre_instance_id
                    && claim.project_hash == self.post_project_hash
                    && claim.post_instance_id == self.post_instance_id
                    && claim.pair_owner_id == self.pair_owner_id
                    && claim.host_process_id == self.host_process_id
                    && claim.pair_claimed_at_bits == self.pair_claimed_at_bits
            }
            _ => false,
        }
    }
}

#[derive(Clone, Debug, Deserialize, Eq, PartialEq, Serialize)]
pub struct LocalBlindCaptureRequest {
    schema: String,
    pub request_id: String,
    pub authority: LocalBlindPairAuthority,
    pub capture_generation: u64,
    pub clock_generation: u64,
    pub sample_rate: u32,
    pub channels: u8,
    pub pre_start: i64,
    pub post_start: i64,
    pub frames: i64,
    pub issued_at_unix_ms: i64,
    pub expires_at_unix_ms: i64,
}

impl LocalBlindCaptureRequest {
    #[allow(clippy::too_many_arguments)]
    pub fn new(
        authority: LocalBlindPairAuthority,
        capture_generation: u64,
        clock_generation: u64,
        sample_rate: u32,
        channels: u8,
        pre_start: i64,
        post_start: i64,
        frames: i64,
        issued_at_unix_ms: i64,
        lease_ms: i64,
    ) -> Option<Self> {
        let request = Self {
            schema: REQUEST_SCHEMA.to_string(),
            request_id: Uuid::new_v4().to_string(),
            authority,
            capture_generation,
            clock_generation,
            sample_rate,
            channels,
            pre_start,
            post_start,
            frames,
            issued_at_unix_ms,
            expires_at_unix_ms: issued_at_unix_ms.checked_add(lease_ms)?,
        };
        request.valid_shape_at(issued_at_unix_ms).then_some(request)
    }

    fn valid_for_target(&self, target: CaptureTarget<'_>, now_unix_ms: i64) -> bool {
        self.valid_shape_at(now_unix_ms)
            && self.authority.pre_project_hash == target.pre_project_hash
            && self.authority.pre_instance_id == target.pre_instance_id
            && self.sample_rate == target.sample_rate
            && self.channels == target.channels
            && target.instance_dir
                == target
                    .kirin_root
                    .join(target.pre_project_hash)
                    .join(target.pre_instance_id)
                    .as_path()
            && self.authority.matches_current_claim(target.kirin_root)
    }

    fn valid_shape_at(&self, now_unix_ms: i64) -> bool {
        let max_frames = i64::from(self.sample_rate).checked_mul(MAX_CAPTURE_SECONDS);
        self.schema == REQUEST_SCHEMA
            && canonical_uuid(&self.request_id)
            && self.authority.valid_shape()
            && self.capture_generation != 0
            && self.clock_generation != 0
            && (8_000..=768_000).contains(&self.sample_rate)
            && matches!(self.channels, 1 | 2)
            && self.frames > 0
            && max_frames.is_some_and(|limit| self.frames <= limit)
            && self.pre_start.checked_add(self.frames).is_some()
            && self.post_start.checked_add(self.frames).is_some()
            && self.issued_at_unix_ms > 0
            && self.issued_at_unix_ms <= now_unix_ms
            && self.expires_at_unix_ms >= now_unix_ms
            && self.expires_at_unix_ms > self.issued_at_unix_ms
            && self.expires_at_unix_ms - self.issued_at_unix_ms <= MAX_LEASE_MS
    }

    fn digest(&self) -> Option<String> {
        let bytes = serde_json::to_vec(self).ok()?;
        Some(hex::encode(Sha256::digest(bytes)))
    }
}

#[derive(Clone, Debug, Deserialize, Eq, PartialEq, Serialize)]
pub struct LocalBlindCaptureArmed {
    schema: String,
    pub request_id: String,
    pub request_sha256: String,
    pub pre_project_hash: String,
    pub pre_instance_id: String,
    pub capture_generation: u64,
    pub clock_generation: u64,
    pub expires_at_unix_ms: i64,
}

impl LocalBlindCaptureArmed {
    pub fn for_request(request: &LocalBlindCaptureRequest, now_unix_ms: i64) -> Option<Self> {
        if !request.valid_shape_at(now_unix_ms) {
            return None;
        }
        let request_sha256 = request.digest()?;
        Some(Self {
            schema: ARMED_SCHEMA.to_string(),
            request_id: request.request_id.clone(),
            request_sha256,
            pre_project_hash: request.authority.pre_project_hash.clone(),
            pre_instance_id: request.authority.pre_instance_id.clone(),
            capture_generation: request.capture_generation,
            clock_generation: request.clock_generation,
            expires_at_unix_ms: request.expires_at_unix_ms,
        })
    }

    pub fn matches_request(&self, request: &LocalBlindCaptureRequest, now_unix_ms: i64) -> bool {
        self.schema == ARMED_SCHEMA
            && self.request_id == request.request_id
            && self.request_sha256.len() == 64
            && self.request_sha256 == request.digest().unwrap_or_default()
            && self.pre_project_hash == request.authority.pre_project_hash
            && self.pre_instance_id == request.authority.pre_instance_id
            && self.capture_generation == request.capture_generation
            && self.clock_generation == request.clock_generation
            && self.expires_at_unix_ms == request.expires_at_unix_ms
            && request.valid_shape_at(now_unix_ms)
    }
}

pub fn publish_local_blind_capture_request(
    kirin_root: &Path,
    instance_dir: &Path,
    request: &LocalBlindCaptureRequest,
) -> std::io::Result<()> {
    let target = CaptureTarget {
        kirin_root,
        instance_dir,
        pre_project_hash: &request.authority.pre_project_hash,
        pre_instance_id: &request.authority.pre_instance_id,
        sample_rate: request.sample_rate,
        channels: request.channels,
    };
    if !request.valid_for_target(target, request.issued_at_unix_ms) {
        return Err(std::io::Error::new(
            std::io::ErrorKind::InvalidInput,
            "Local Blind request has no current exact pair authority",
        ));
    }
    let bytes = serde_json::to_vec(request).map_err(std::io::Error::other)?;
    if bytes.len() > REQUEST_MAX_BYTES as usize {
        return Err(std::io::Error::new(
            std::io::ErrorKind::InvalidInput,
            "Local Blind request exceeds its transport bound",
        ));
    }
    analysis_exchange_transport::write(
        instance_dir,
        &request_path(instance_dir),
        AnalysisSlot::LocalBlindRequest,
        &bytes,
    )
}

#[allow(clippy::too_many_arguments)]
pub fn read_validated_local_blind_capture_request(
    kirin_root: &Path,
    instance_dir: &Path,
    pre_project_hash: &str,
    pre_instance_id: &str,
    sample_rate: u32,
    channels: u8,
    now_unix_ms: i64,
) -> Option<LocalBlindCaptureRequest> {
    let request: LocalBlindCaptureRequest =
        serde_json::from_slice(&analysis_exchange_transport::read(
            instance_dir,
            &request_path(instance_dir),
            AnalysisSlot::LocalBlindRequest,
            REQUEST_MAX_BYTES,
        )?)
        .ok()?;
    let target = CaptureTarget {
        kirin_root,
        instance_dir,
        pre_project_hash,
        pre_instance_id,
        sample_rate,
        channels,
    };
    request
        .valid_for_target(target, now_unix_ms)
        .then_some(request)
}

/// Publish only after the PRE non-RT owner has installed the matching capture object for its
/// Audio Thread. This function validates pair authority and the echo, but cannot prove that local
/// C++ publication step on its own.
pub fn publish_local_blind_capture_armed(
    kirin_root: &Path,
    instance_dir: &Path,
    request: &LocalBlindCaptureRequest,
    now_unix_ms: i64,
) -> std::io::Result<()> {
    let target = CaptureTarget {
        kirin_root,
        instance_dir,
        pre_project_hash: &request.authority.pre_project_hash,
        pre_instance_id: &request.authority.pre_instance_id,
        sample_rate: request.sample_rate,
        channels: request.channels,
    };
    if !request.valid_for_target(target, now_unix_ms) {
        return Err(std::io::Error::new(
            std::io::ErrorKind::InvalidInput,
            "Local Blind request lost exact pair authority",
        ));
    }
    let armed = LocalBlindCaptureArmed::for_request(request, now_unix_ms).ok_or_else(|| {
        std::io::Error::new(
            std::io::ErrorKind::InvalidInput,
            "Local Blind request expired",
        )
    })?;
    let bytes = serde_json::to_vec(&armed).map_err(std::io::Error::other)?;
    if bytes.len() > ARMED_MAX_BYTES as usize {
        return Err(std::io::Error::new(
            std::io::ErrorKind::InvalidInput,
            "Local Blind armed response exceeds its transport bound",
        ));
    }
    analysis_exchange_transport::write(
        instance_dir,
        &armed_path(instance_dir),
        AnalysisSlot::LocalBlindArmed,
        &bytes,
    )
}

pub fn read_matching_local_blind_capture_armed(
    kirin_root: &Path,
    instance_dir: &Path,
    request: &LocalBlindCaptureRequest,
    now_unix_ms: i64,
) -> Option<LocalBlindCaptureArmed> {
    let target = CaptureTarget {
        kirin_root,
        instance_dir,
        pre_project_hash: &request.authority.pre_project_hash,
        pre_instance_id: &request.authority.pre_instance_id,
        sample_rate: request.sample_rate,
        channels: request.channels,
    };
    request
        .valid_for_target(target, now_unix_ms)
        .then_some(())?;
    let armed: LocalBlindCaptureArmed = serde_json::from_slice(&analysis_exchange_transport::read(
        instance_dir,
        &armed_path(instance_dir),
        AnalysisSlot::LocalBlindArmed,
        ARMED_MAX_BYTES,
    )?)
    .ok()?;
    armed.matches_request(request, now_unix_ms).then_some(armed)
}

fn request_path(instance_dir: &Path) -> PathBuf {
    instance_dir.join("local_blind").join("request.json")
}

fn armed_path(instance_dir: &Path) -> PathBuf {
    instance_dir.join("local_blind").join("armed.json")
}

fn canonical_uuid(value: &str) -> bool {
    Uuid::parse_str(value).is_ok_and(|parsed| parsed.to_string() == value)
}

fn valid_component(value: &str) -> bool {
    crate::is_path_safe_component(value)
}

#[cfg(test)]
#[path = "local_blind_capture_protocol_tests.rs"]
mod tests;
