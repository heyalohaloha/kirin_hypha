//! Immutable PRE PCM transfer for one exact Local Blind capture request.
//!
//! Capture happens in the plug-in's preallocated role-local slot. A non-RT PRE owner publishes
//! the completed interleaved PCM once, and the exact POST copies and validates it before writing
//! a consumed acknowledgement. This transport does not grant an Analysis lease or audition.

use std::fs;
use std::io::{self, Read};
use std::path::{Path, PathBuf};

use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};

use crate::local_blind_capture_protocol::{
    active_capture_result_was_armed, LocalBlindCaptureRequest,
};

const BLOB_MAGIC: &[u8; 16] = b"KIRINLBPCMv1\0\0\0\0";
const HEADER_LENGTH_BYTES: usize = 4;
const HEADER_MAX_BYTES: usize = 4_096;
const RECEIPT_SCHEMA: &str = "kirin_hypha_local_blind_pre_capture_v2";
const CONSUMED_SCHEMA: &str = "kirin_hypha_local_blind_pre_capture_consumed_v2";
const FAILURE_SCHEMA: &str = "kirin_hypha_local_blind_pre_capture_failure_v1";

#[derive(Clone, Debug, Deserialize, Eq, PartialEq, Serialize)]
#[serde(deny_unknown_fields)]
pub struct LocalBlindPreCaptureReceipt {
    schema: String,
    pub request_id: String,
    pub request_sha256: String,
    pub pair_generation: u64,
    pub capture_generation: u64,
    pub clock_generation: u64,
    pub sample_rate: u32,
    pub channels: u8,
    pub start: i64,
    pub frames: i64,
    pub sample_count: u64,
    pub pcm_sha256: String,
    pub expires_at_unix_ms: i64,
}

/// Terminal PRE failure for one already-armed request. The numeric values are the stable C ABI
/// discriminants for the non-RT owner and role-local capture lane; zero is never a failure.
#[derive(Clone, Debug, Deserialize, Eq, PartialEq, Serialize)]
#[serde(deny_unknown_fields)]
pub struct LocalBlindPreCaptureFailure {
    schema: String,
    pub request_id: String,
    pub request_sha256: String,
    pub pair_generation: u64,
    pub capture_generation: u64,
    pub clock_generation: u64,
    pub owner_failure: u8,
    pub capture_failure: u8,
    pub expires_at_unix_ms: i64,
}

impl LocalBlindPreCaptureFailure {
    fn new(
        request: &LocalBlindCaptureRequest,
        owner_failure: u8,
        capture_failure: u8,
    ) -> Option<Self> {
        valid_failure_codes(owner_failure, capture_failure).then(|| Self {
            schema: FAILURE_SCHEMA.to_string(),
            request_id: request.request_id.clone(),
            request_sha256: request.digest().unwrap_or_default(),
            pair_generation: request.authority.pair_generation,
            capture_generation: request.capture_generation,
            clock_generation: request.clock_generation,
            owner_failure,
            capture_failure,
            expires_at_unix_ms: request.expires_at_unix_ms,
        })
    }

    fn matches_request(&self, request: &LocalBlindCaptureRequest) -> bool {
        self.schema == FAILURE_SCHEMA
            && self.request_id == request.request_id
            && self.request_sha256 == request.digest().unwrap_or_default()
            && self.pair_generation == request.authority.pair_generation
            && self.capture_generation == request.capture_generation
            && self.clock_generation == request.clock_generation
            && valid_failure_codes(self.owner_failure, self.capture_failure)
            && self.expires_at_unix_ms == request.expires_at_unix_ms
    }
}

impl LocalBlindPreCaptureReceipt {
    pub fn matches_request(&self, request: &LocalBlindCaptureRequest) -> bool {
        let expected_samples = expected_sample_count(request);
        self.schema == RECEIPT_SCHEMA
            && self.request_id == request.request_id
            && self.request_sha256.len() == 64
            && self.request_sha256 == request.digest().unwrap_or_default()
            && self.pair_generation == request.authority.pair_generation
            && self.capture_generation == request.capture_generation
            && self.clock_generation == request.clock_generation
            && self.sample_rate == request.sample_rate
            && self.channels == request.channels
            && self.start == request.native_start
            && self.frames == request.frames
            && expected_samples == Some(self.sample_count)
            && canonical_sha256(&self.pcm_sha256)
            && self.expires_at_unix_ms == request.expires_at_unix_ms
    }
}

#[derive(Clone, Debug, Deserialize, Eq, PartialEq, Serialize)]
#[serde(deny_unknown_fields)]
struct ConsumedReceipt {
    schema: String,
    request_id: String,
    request_sha256: String,
    capture_generation: u64,
    clock_generation: u64,
    pcm_sha256: String,
    expires_at_unix_ms: i64,
}

impl ConsumedReceipt {
    fn new(request: &LocalBlindCaptureRequest, pcm_sha256: &str) -> Option<Self> {
        canonical_sha256(pcm_sha256).then(|| Self {
            schema: CONSUMED_SCHEMA.to_string(),
            request_id: request.request_id.clone(),
            request_sha256: request.digest().unwrap_or_default(),
            capture_generation: request.capture_generation,
            clock_generation: request.clock_generation,
            pcm_sha256: pcm_sha256.to_string(),
            expires_at_unix_ms: request.expires_at_unix_ms,
        })
    }

    fn matches(&self, request: &LocalBlindCaptureRequest, pcm_sha256: &str) -> bool {
        self.schema == CONSUMED_SCHEMA
            && self.request_id == request.request_id
            && self.request_sha256 == request.digest().unwrap_or_default()
            && self.capture_generation == request.capture_generation
            && self.clock_generation == request.clock_generation
            && self.pcm_sha256 == pcm_sha256
            && self.expires_at_unix_ms == request.expires_at_unix_ms
            && canonical_sha256(pcm_sha256)
    }
}

pub struct LocalBlindPreCapture {
    pub receipt: LocalBlindPreCaptureReceipt,
    pub interleaved: Vec<f32>,
}

pub fn publish_local_blind_pre_capture_failure(
    kirin_root: &Path,
    instance_dir: &Path,
    request: &LocalBlindCaptureRequest,
    owner_failure: u8,
    capture_failure: u8,
    now_unix_ms: i64,
) -> io::Result<()> {
    if !active_capture_result_was_armed(kirin_root, instance_dir, request, now_unix_ms) {
        return Err(invalid("Local Blind PRE failure lost exact pair authority"));
    }
    if capture_path(instance_dir, request).exists() {
        return Err(invalid(
            "Local Blind PRE capture already has a successful terminal result",
        ));
    }
    let failure = LocalBlindPreCaptureFailure::new(request, owner_failure, capture_failure)
        .ok_or_else(|| invalid("Local Blind PRE failure code is invalid"))?;
    let bytes = serde_json::to_vec(&failure).map_err(io::Error::other)?;
    if bytes.len() > HEADER_MAX_BYTES {
        return Err(invalid("Local Blind PRE failure exceeds its bound"));
    }
    crate::atomic_file::write_bytes_immutable(&failure_path(instance_dir, request), &bytes)
}

pub fn read_local_blind_pre_capture_failure(
    kirin_root: &Path,
    instance_dir: &Path,
    request: &LocalBlindCaptureRequest,
    now_unix_ms: i64,
) -> Option<LocalBlindPreCaptureFailure> {
    if !active_capture_result_was_armed(kirin_root, instance_dir, request, now_unix_ms) {
        return None;
    }
    let failure: LocalBlindPreCaptureFailure = serde_json::from_slice(&read_bounded(
        &failure_path(instance_dir, request),
        HEADER_MAX_BYTES,
    )?)
    .ok()?;
    failure.matches_request(request).then_some(failure)
}

pub fn publish_local_blind_pre_capture(
    kirin_root: &Path,
    instance_dir: &Path,
    request: &LocalBlindCaptureRequest,
    interleaved: &[f32],
    now_unix_ms: i64,
) -> io::Result<LocalBlindPreCaptureReceipt> {
    if !active_capture_result_was_armed(kirin_root, instance_dir, request, now_unix_ms) {
        return Err(invalid("Local Blind PRE capture lost exact pair authority"));
    }
    if failure_path(instance_dir, request).exists() {
        return Err(invalid(
            "Local Blind PRE capture already has a failed terminal result",
        ));
    }
    let sample_count = expected_sample_count(request)
        .and_then(|value| usize::try_from(value).ok())
        .ok_or_else(|| invalid("Local Blind PRE sample count overflows"))?;
    if interleaved.len() != sample_count || interleaved.iter().any(|value| !value.is_finite()) {
        return Err(invalid(
            "Local Blind PRE capture is incomplete or non-finite",
        ));
    }

    let mut pcm_bytes = Vec::with_capacity(sample_count.saturating_mul(size_of::<f32>()));
    for value in interleaved {
        pcm_bytes.extend_from_slice(&value.to_bits().to_le_bytes());
    }
    let receipt = LocalBlindPreCaptureReceipt {
        schema: RECEIPT_SCHEMA.to_string(),
        request_id: request.request_id.clone(),
        request_sha256: request
            .digest()
            .ok_or_else(|| invalid("request digest failed"))?,
        pair_generation: request.authority.pair_generation,
        capture_generation: request.capture_generation,
        clock_generation: request.clock_generation,
        sample_rate: request.sample_rate,
        channels: request.channels,
        start: request.native_start,
        frames: request.frames,
        sample_count: sample_count as u64,
        pcm_sha256: hex::encode(Sha256::digest(&pcm_bytes)),
        expires_at_unix_ms: request.expires_at_unix_ms,
    };
    let header = serde_json::to_vec(&receipt).map_err(io::Error::other)?;
    if header.len() > HEADER_MAX_BYTES {
        return Err(invalid("Local Blind PRE receipt exceeds its bound"));
    }
    let header_len = u32::try_from(header.len()).map_err(io::Error::other)?;
    let mut blob =
        Vec::with_capacity(BLOB_MAGIC.len() + HEADER_LENGTH_BYTES + header.len() + pcm_bytes.len());
    blob.extend_from_slice(BLOB_MAGIC);
    blob.extend_from_slice(&header_len.to_le_bytes());
    blob.extend_from_slice(&header);
    blob.extend_from_slice(&pcm_bytes);
    crate::atomic_file::write_bytes_immutable(&capture_path(instance_dir, request), &blob)?;
    Ok(receipt)
}

pub fn read_local_blind_pre_capture(
    kirin_root: &Path,
    instance_dir: &Path,
    request: &LocalBlindCaptureRequest,
    now_unix_ms: i64,
) -> Option<LocalBlindPreCapture> {
    if !active_capture_result_was_armed(kirin_root, instance_dir, request, now_unix_ms) {
        return None;
    }
    let expected_pcm_bytes = usize::try_from(expected_sample_count(request)?)
        .ok()?
        .checked_mul(size_of::<f32>())?;
    let maximum_blob = BLOB_MAGIC
        .len()
        .checked_add(HEADER_LENGTH_BYTES)?
        .checked_add(HEADER_MAX_BYTES)?
        .checked_add(expected_pcm_bytes)?;
    decode_blob(
        request,
        &read_bounded(&capture_path(instance_dir, request), maximum_blob)?,
    )
}

pub fn publish_local_blind_pre_capture_consumed(
    kirin_root: &Path,
    instance_dir: &Path,
    request: &LocalBlindCaptureRequest,
    pcm_sha256: &str,
    now_unix_ms: i64,
) -> io::Result<()> {
    if !active_capture_result_was_armed(kirin_root, instance_dir, request, now_unix_ms) {
        return Err(invalid(
            "Local Blind PRE acknowledgement lost exact pair authority",
        ));
    }
    let consumed = ConsumedReceipt::new(request, pcm_sha256)
        .ok_or_else(|| invalid("Local Blind PRE acknowledgement does not match its request"))?;
    let bytes = serde_json::to_vec(&consumed).map_err(io::Error::other)?;
    crate::atomic_file::write_bytes_immutable(&consumed_path(instance_dir, request), &bytes)
}

pub fn local_blind_pre_capture_was_consumed(
    kirin_root: &Path,
    instance_dir: &Path,
    request: &LocalBlindCaptureRequest,
    pcm_sha256: &str,
    now_unix_ms: i64,
) -> bool {
    if !active_capture_result_was_armed(kirin_root, instance_dir, request, now_unix_ms) {
        return false;
    }
    read_bounded(&consumed_path(instance_dir, request), HEADER_MAX_BYTES)
        .and_then(|bytes| serde_json::from_slice::<ConsumedReceipt>(&bytes).ok())
        .is_some_and(|consumed| consumed.matches(request, pcm_sha256))
}

pub fn remove_local_blind_pre_capture(instance_dir: &Path, request_id: &str) -> io::Result<()> {
    if !crate::local_blind_capture_protocol::canonical_request_id(request_id) {
        return Err(invalid("Local Blind request ID is not canonical"));
    }
    for path in [
        capture_path_for_id(instance_dir, request_id),
        consumed_path_for_id(instance_dir, request_id),
        failure_path_for_id(instance_dir, request_id),
    ] {
        match fs::remove_file(path) {
            Ok(()) => {}
            Err(error) if error.kind() == io::ErrorKind::NotFound => {}
            Err(error) => return Err(error),
        }
    }
    Ok(())
}

fn decode_blob(request: &LocalBlindCaptureRequest, blob: &[u8]) -> Option<LocalBlindPreCapture> {
    let body = blob.strip_prefix(BLOB_MAGIC)?;
    let length_bytes: [u8; HEADER_LENGTH_BYTES] =
        body.get(..HEADER_LENGTH_BYTES)?.try_into().ok()?;
    let header_len = u32::from_le_bytes(length_bytes) as usize;
    if header_len == 0 || header_len > HEADER_MAX_BYTES {
        return None;
    }
    let header_end = HEADER_LENGTH_BYTES.checked_add(header_len)?;
    let receipt: LocalBlindPreCaptureReceipt =
        serde_json::from_slice(body.get(HEADER_LENGTH_BYTES..header_end)?).ok()?;
    if !receipt.matches_request(request) {
        return None;
    }
    let pcm_bytes = body.get(header_end..)?;
    let expected_bytes = usize::try_from(receipt.sample_count)
        .ok()?
        .checked_mul(size_of::<f32>())?;
    if pcm_bytes.len() != expected_bytes
        || hex::encode(Sha256::digest(pcm_bytes)) != receipt.pcm_sha256
    {
        return None;
    }
    let mut interleaved = Vec::with_capacity(receipt.sample_count as usize);
    for &chunk in pcm_bytes.as_chunks::<4>().0 {
        let value = f32::from_bits(u32::from_le_bytes(chunk));
        if !value.is_finite() {
            return None;
        }
        interleaved.push(value);
    }
    Some(LocalBlindPreCapture {
        receipt,
        interleaved,
    })
}

fn expected_sample_count(request: &LocalBlindCaptureRequest) -> Option<u64> {
    u64::try_from(request.frames)
        .ok()?
        .checked_mul(u64::from(request.channels))
}

fn capture_path(instance_dir: &Path, request: &LocalBlindCaptureRequest) -> PathBuf {
    capture_path_for_id(instance_dir, &request.request_id)
}

fn consumed_path(instance_dir: &Path, request: &LocalBlindCaptureRequest) -> PathBuf {
    consumed_path_for_id(instance_dir, &request.request_id)
}

fn failure_path(instance_dir: &Path, request: &LocalBlindCaptureRequest) -> PathBuf {
    failure_path_for_id(instance_dir, &request.request_id)
}

fn capture_path_for_id(instance_dir: &Path, request_id: &str) -> PathBuf {
    instance_dir
        .join("local_blind")
        .join("completed")
        .join(format!("{request_id}.pcm"))
}

fn consumed_path_for_id(instance_dir: &Path, request_id: &str) -> PathBuf {
    instance_dir
        .join("local_blind")
        .join("consumed")
        .join(format!("{request_id}.json"))
}

fn failure_path_for_id(instance_dir: &Path, request_id: &str) -> PathBuf {
    instance_dir
        .join("local_blind")
        .join("failed")
        .join(format!("{request_id}.json"))
}

fn valid_failure_codes(owner_failure: u8, capture_failure: u8) -> bool {
    (1..=7).contains(&owner_failure)
        && capture_failure <= 7
        && (owner_failure == 6 || capture_failure == 0)
}

fn read_bounded(path: &Path, maximum_bytes: usize) -> Option<Vec<u8>> {
    let capacity = maximum_bytes.checked_add(1)?;
    let read_limit = u64::try_from(capacity).ok()?;
    let mut bytes = Vec::with_capacity(capacity);
    fs::File::open(path)
        .ok()?
        .take(read_limit)
        .read_to_end(&mut bytes)
        .ok()?;
    (bytes.len() <= maximum_bytes).then_some(bytes)
}

fn canonical_sha256(value: &str) -> bool {
    value.len() == 64
        && value
            .bytes()
            .all(|byte| byte.is_ascii_digit() || (b'a'..=b'f').contains(&byte))
}

fn invalid(message: &'static str) -> io::Error {
    io::Error::new(io::ErrorKind::InvalidInput, message)
}

#[cfg(test)]
#[path = "local_blind_capture_result_tests.rs"]
mod tests;
