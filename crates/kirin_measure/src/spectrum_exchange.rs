//! Exact-pair, renewable Spectrum exchange between a visible POST and its latched PRE.
//!
//! Control requests are small JSON files for diagnosability. Spectrum payloads use one fixed,
//! versioned little-endian layout. Both are written atomically in a dedicated `spectrum/`
//! namespace beside the exact PRE `pre.json`; existing Watch and Record schemas are untouched.

use std::fs;
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};

use serde::{Deserialize, Serialize};
use uuid::Uuid;

use crate::spectrum::{
    difference_post_minus_pre, SpectrumDifference, SpectrumFrame, SPECTRUM_BAND_COUNT,
    SPECTRUM_FFT_SIZE, SPECTRUM_SCHEMA_VERSION,
};
use crate::spectrum_exchange_worker::SpectrumExchangeWorker;
use crate::spectrum_runtime::{SpectrumHistory, SpectrumRuntime};

const REQUEST_SCHEMA: &str = "kirin_hypha_spectrum_request_v1";
const SNAPSHOT_MAGIC: &[u8; 8] = b"KHSPEC01";
const REQUEST_RENEW_INTERVAL: Duration = Duration::from_millis(500);
const REQUEST_LEASE_MS: i64 = 1_500;
const WARMUP_LIMIT: Duration = Duration::from_secs(2);
const REQUEST_MAX_BYTES: u64 = 2_048;
const SNAPSHOT_MAX_BYTES: u64 = 16_384;

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SpectrumTarget {
    pub pre_instance_id: String,
    pub instance_dir: PathBuf,
}

impl SpectrumTarget {
    pub fn from_pre_json(pre_instance_id: String, pre_json: &Path) -> Option<Self> {
        (pre_json.file_name()?.to_str()? == "pre.json").then_some(Self {
            pre_instance_id,
            instance_dir: pre_json.parent()?.to_path_buf(),
        })
    }
}

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
#[repr(u8)]
pub enum SpectrumViewStatus {
    #[default]
    Hidden = 0,
    NoPair = 1,
    WarmingUp = 2,
    Active = 3,
    Unavailable = 4,
}

#[derive(Clone, Debug, Default, PartialEq)]
pub struct SpectrumViewSnapshot {
    pub status: SpectrumViewStatus,
    pub difference: Option<SpectrumDifference>,
}

#[derive(Debug, Serialize, Deserialize)]
struct SpectrumRequest {
    schema: String,
    request_id: String,
    requested_by_post_instance_id: String,
    target_pre_instance_id: String,
    sample_rate: u32,
    expires_at_unix_ms: i64,
}

struct PostSession {
    request_id: Uuid,
    target: Option<SpectrumTarget>,
    last_renewed: Option<Instant>,
    started_at: Option<Instant>,
}

struct PreSession {
    request_id: Uuid,
    last_written_end: Option<i64>,
    instance_dir: PathBuf,
}

pub struct SpectrumCoordinator {
    sample_rate: u32,
    runtime: Arc<SpectrumRuntime>,
    post_visible: AtomicBool,
    post_session: Mutex<Option<PostSession>>,
    pre_session: Mutex<Option<PreSession>>,
    view: Mutex<SpectrumViewSnapshot>,
    pub(crate) exchange_worker: SpectrumExchangeWorker,
}

impl SpectrumCoordinator {
    pub fn new(sample_rate: u32, runtime: Arc<SpectrumRuntime>) -> Arc<Self> {
        Arc::new(Self {
            sample_rate,
            runtime,
            post_visible: AtomicBool::new(false),
            post_session: Mutex::new(None),
            pre_session: Mutex::new(None),
            view: Mutex::new(SpectrumViewSnapshot::default()),
            exchange_worker: SpectrumExchangeWorker::new(),
        })
    }

    /// Message/control thread only. Filesystem work remains deferred to the existing IO thread.
    pub fn set_post_visible(&self, visible: bool) {
        let previous = self.post_visible.swap(visible, Ordering::AcqRel);
        if visible && !previous {
            if let Ok(mut session) = self.post_session.lock() {
                *session = Some(PostSession {
                    request_id: Uuid::new_v4(),
                    target: None,
                    last_renewed: None,
                    started_at: None,
                });
            }
        } else if !visible {
            let _ = self.runtime.set_enabled(false);
            self.store_view(SpectrumViewStatus::Hidden, None);
        }
        self.exchange_worker.notify();
    }

    pub fn post_visible(&self) -> bool {
        self.post_visible.load(Ordering::Acquire)
    }

    /// POST IO-thread tick. `target` must come from the already-confirmed exact pair latch.
    pub(crate) fn post_tick(&self, post_instance_id: &str, target: Option<SpectrumTarget>) -> bool {
        let mut session_slot = match self.post_session.lock() {
            Ok(session) => session,
            Err(_) => return false,
        };
        if !self.post_visible() {
            if let Some(session) = session_slot.take() {
                cleanup_owned_request(session.target.as_ref(), session.request_id);
            }
            return false;
        }
        let session = session_slot.get_or_insert_with(|| PostSession {
            request_id: Uuid::new_v4(),
            target: None,
            last_renewed: None,
            started_at: None,
        });
        let Some(target) = target else {
            cleanup_owned_request(session.target.as_ref(), session.request_id);
            session.target = None;
            session.last_renewed = None;
            session.started_at = None;
            let _ = self.runtime.set_enabled(false);
            self.store_view(SpectrumViewStatus::NoPair, None);
            return false;
        };
        if session.target.as_ref() != Some(&target) {
            cleanup_owned_request(session.target.as_ref(), session.request_id);
            session.target = Some(target.clone());
            session.last_renewed = None;
            session.started_at = None;
            let _ = self.runtime.set_enabled(false);
        }
        let now = Instant::now();
        if session
            .last_renewed
            .is_none_or(|last| now.duration_since(last) >= REQUEST_RENEW_INTERVAL)
        {
            let request = SpectrumRequest {
                schema: REQUEST_SCHEMA.to_string(),
                request_id: session.request_id.to_string(),
                requested_by_post_instance_id: post_instance_id.to_string(),
                target_pre_instance_id: target.pre_instance_id.clone(),
                sample_rate: self.sample_rate,
                expires_at_unix_ms: unix_ms_now().saturating_add(REQUEST_LEASE_MS),
            };
            if write_request(&target.instance_dir, &request).is_err() {
                let _ = self.runtime.set_enabled(false);
                self.store_view(SpectrumViewStatus::Unavailable, None);
                return false;
            }
            session.last_renewed = Some(now);
        }
        if !self.runtime.set_enabled(true) {
            cleanup_owned_request(Some(&target), session.request_id);
            session.last_renewed = None;
            self.store_view(SpectrumViewStatus::Unavailable, None);
            return false;
        }
        if session.started_at.is_none() {
            session.started_at = Some(now);
        }
        let local = self.runtime.try_history();
        let remote = read_snapshot(&target.instance_dir)
            .filter(|snapshot| snapshot.request_id == session.request_id)
            .map(|snapshot| snapshot.history);
        match local
            .as_ref()
            .zip(remote.as_ref())
            .and_then(|(post, pre)| newest_exact_difference(post, pre))
        {
            Some(difference) => self.store_view(SpectrumViewStatus::Active, Some(difference)),
            None if local
                .as_ref()
                .is_some_and(|history| history.newest().is_some())
                && remote
                    .as_ref()
                    .is_some_and(|history| history.newest().is_some()) =>
            {
                self.store_view(SpectrumViewStatus::Unavailable, None)
            }
            None if session
                .started_at
                .is_some_and(|started| now.duration_since(started) >= WARMUP_LIMIT) =>
            {
                self.store_view(SpectrumViewStatus::Unavailable, None)
            }
            None => self.store_view(SpectrumViewStatus::WarmingUp, None),
        }
        true
    }

    /// PRE IO-thread tick. An active exact request may be called at the 30 Hz Spectrum cadence.
    pub(crate) fn pre_tick(&self, pre_instance_id: &str, instance_dir: &Path) -> bool {
        let request = read_request(instance_dir).and_then(|request| {
            (request.schema == REQUEST_SCHEMA
                && request.target_pre_instance_id == pre_instance_id
                && request.sample_rate == self.sample_rate
                && request.expires_at_unix_ms >= unix_ms_now()
                && !request.requested_by_post_instance_id.is_empty())
            .then(|| Uuid::parse_str(&request.request_id).ok())
            .flatten()
        });
        let mut session = match self.pre_session.lock() {
            Ok(session) => session,
            Err(_) => return false,
        };
        let Some(request_id) = request else {
            if session.take().is_some() {
                let _ = self.runtime.set_enabled(false);
                let _ = fs::remove_file(snapshot_path(instance_dir));
            }
            return false;
        };
        if session.as_ref().map(|state| state.request_id) != Some(request_id) {
            let _ = self.runtime.set_enabled(false);
            let _ = fs::remove_file(snapshot_path(instance_dir));
            if !self.runtime.set_enabled(true) {
                return false;
            }
            *session = Some(PreSession {
                request_id,
                last_written_end: None,
                instance_dir: instance_dir.to_path_buf(),
            });
        }
        let Some(history) = self.runtime.try_history() else {
            return true;
        };
        let newest_end = history.newest().map(|frame| frame.presentation_end_samples);
        let state = session.as_mut().expect("PRE session established above");
        if newest_end.is_none() || newest_end == state.last_written_end {
            return true;
        }
        let bytes = encode_snapshot(state.request_id, &history);
        if crate::atomic_file::write_bytes_atomic(&snapshot_path(instance_dir), &bytes).is_ok() {
            state.last_written_end = newest_end;
        }
        true
    }

    pub fn try_view(&self) -> Option<SpectrumViewSnapshot> {
        self.view.try_lock().ok().map(|view| view.clone())
    }

    pub fn shutdown(&self) {
        self.post_visible.store(false, Ordering::Release);
        self.exchange_worker.shutdown_and_join();
        if let Ok(mut session) = self.post_session.lock() {
            if let Some(session) = session.take() {
                cleanup_owned_request(session.target.as_ref(), session.request_id);
            }
        }
        if let Ok(mut session) = self.pre_session.lock() {
            if let Some(session) = session.take() {
                let _ = fs::remove_file(snapshot_path(&session.instance_dir));
            }
        }
        let _ = self.runtime.set_enabled(false);
    }

    fn store_view(&self, status: SpectrumViewStatus, difference: Option<SpectrumDifference>) {
        if let Ok(mut view) = self.view.lock() {
            *view = SpectrumViewSnapshot { status, difference };
        }
    }
}

fn newest_exact_difference(
    post: &SpectrumHistory,
    pre: &SpectrumHistory,
) -> Option<SpectrumDifference> {
    post.frames().rev().find_map(|post_frame| {
        pre.matching_presentation_end(post_frame.presentation_end_samples)
            .and_then(|pre_frame| difference_post_minus_pre(post_frame, pre_frame))
    })
}

fn request_path(instance_dir: &Path) -> PathBuf {
    instance_dir.join("spectrum").join("request.json")
}

fn snapshot_path(instance_dir: &Path) -> PathBuf {
    instance_dir.join("spectrum").join("pre.bin")
}

fn write_request(instance_dir: &Path, request: &SpectrumRequest) -> std::io::Result<()> {
    let bytes = serde_json::to_vec(request).map_err(std::io::Error::other)?;
    crate::atomic_file::write_bytes_atomic(&request_path(instance_dir), &bytes)
}

fn read_request(instance_dir: &Path) -> Option<SpectrumRequest> {
    serde_json::from_slice(&read_bounded(
        &request_path(instance_dir),
        REQUEST_MAX_BYTES,
    )?)
    .ok()
}

fn cleanup_owned_request(target: Option<&SpectrumTarget>, request_id: Uuid) {
    let Some(target) = target else { return };
    let path = request_path(&target.instance_dir);
    if read_request(&target.instance_dir)
        .is_some_and(|request| request.request_id == request_id.to_string())
    {
        let _ = fs::remove_file(path);
    }
}

struct DecodedSnapshot {
    request_id: Uuid,
    history: SpectrumHistory,
}

fn read_snapshot(instance_dir: &Path) -> Option<DecodedSnapshot> {
    decode_snapshot(&read_bounded(
        &snapshot_path(instance_dir),
        SNAPSHOT_MAX_BYTES,
    )?)
}

fn read_bounded(path: &Path, maximum_bytes: u64) -> Option<Vec<u8>> {
    (fs::metadata(path).ok()?.len() <= maximum_bytes)
        .then(|| fs::read(path).ok())
        .flatten()
}

fn encode_snapshot(request_id: Uuid, history: &SpectrumHistory) -> Vec<u8> {
    let frame_count = history.frames().len().min(u16::MAX as usize) as u16;
    let mut bytes = Vec::with_capacity(40 + frame_count as usize * (24 + SPECTRUM_BAND_COUNT * 4));
    bytes.extend_from_slice(SNAPSHOT_MAGIC);
    bytes.extend_from_slice(&SPECTRUM_SCHEMA_VERSION.to_le_bytes());
    bytes.extend_from_slice(&(SPECTRUM_BAND_COUNT as u16).to_le_bytes());
    bytes.extend_from_slice(
        &history
            .newest()
            .map_or(0, |frame| frame.sample_rate)
            .to_le_bytes(),
    );
    bytes.extend_from_slice(&(SPECTRUM_FFT_SIZE as u32).to_le_bytes());
    bytes.extend_from_slice(&frame_count.to_le_bytes());
    bytes.extend_from_slice(&0_u16.to_le_bytes());
    bytes.extend_from_slice(request_id.as_bytes());
    for frame in history.frames() {
        bytes.extend_from_slice(&frame.presentation_end_samples.to_le_bytes());
        bytes.extend_from_slice(&frame.generation.to_le_bytes());
        bytes.extend_from_slice(&frame.min_hz.to_le_bytes());
        bytes.extend_from_slice(&frame.max_hz.to_le_bytes());
        for value in frame.dbfs {
            bytes.extend_from_slice(&value.to_le_bytes());
        }
    }
    bytes
}

fn decode_snapshot(bytes: &[u8]) -> Option<DecodedSnapshot> {
    let mut cursor = Cursor::new(bytes);
    (cursor.take(8)? == SNAPSHOT_MAGIC).then_some(())?;
    (cursor.u16()? == SPECTRUM_SCHEMA_VERSION).then_some(())?;
    (cursor.u16()? as usize == SPECTRUM_BAND_COUNT).then_some(())?;
    let sample_rate = cursor.u32()?;
    ((8_000..=384_000).contains(&sample_rate)).then_some(())?;
    (cursor.u32()? as usize == SPECTRUM_FFT_SIZE).then_some(())?;
    let frame_count = cursor.u16()? as usize;
    (frame_count <= crate::SPECTRUM_HISTORY_CAPACITY).then_some(())?;
    let _reserved = cursor.u16()?;
    let request_id = Uuid::from_slice(cursor.take(16)?).ok()?;
    let mut history = SpectrumHistory::with_capacity();
    for _ in 0..frame_count {
        let presentation_end_samples = cursor.i64()?;
        let generation = cursor.u64()?;
        let min_hz = cursor.f32()?;
        let max_hz = cursor.f32()?;
        if !(min_hz.is_finite() && max_hz.is_finite() && max_hz > min_hz) {
            return None;
        }
        let mut dbfs = [0.0; SPECTRUM_BAND_COUNT];
        for value in &mut dbfs {
            *value = cursor.f32()?;
            if !value.is_finite() || !(-300.0..=100.0).contains(value) {
                return None;
            }
        }
        history.push(SpectrumFrame {
            schema_version: SPECTRUM_SCHEMA_VERSION,
            sample_rate,
            fft_size: SPECTRUM_FFT_SIZE as u32,
            band_count: SPECTRUM_BAND_COUNT as u16,
            presentation_end_samples,
            generation,
            min_hz,
            max_hz,
            dbfs,
        });
    }
    (cursor.remaining() == 0).then_some(DecodedSnapshot {
        request_id,
        history,
    })
}

struct Cursor<'a> {
    bytes: &'a [u8],
    offset: usize,
}

impl<'a> Cursor<'a> {
    fn new(bytes: &'a [u8]) -> Self {
        Self { bytes, offset: 0 }
    }

    fn take(&mut self, count: usize) -> Option<&'a [u8]> {
        let end = self.offset.checked_add(count)?;
        let value = self.bytes.get(self.offset..end)?;
        self.offset = end;
        Some(value)
    }

    fn u16(&mut self) -> Option<u16> {
        Some(u16::from_le_bytes(self.take(2)?.try_into().ok()?))
    }

    fn u32(&mut self) -> Option<u32> {
        Some(u32::from_le_bytes(self.take(4)?.try_into().ok()?))
    }

    fn u64(&mut self) -> Option<u64> {
        Some(u64::from_le_bytes(self.take(8)?.try_into().ok()?))
    }

    fn i64(&mut self) -> Option<i64> {
        Some(i64::from_le_bytes(self.take(8)?.try_into().ok()?))
    }

    fn f32(&mut self) -> Option<f32> {
        Some(f32::from_le_bytes(self.take(4)?.try_into().ok()?))
    }

    fn remaining(&self) -> usize {
        self.bytes.len().saturating_sub(self.offset)
    }
}

fn unix_ms_now() -> i64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .ok()
        .and_then(|duration| i64::try_from(duration.as_millis()).ok())
        .unwrap_or(0)
}

#[cfg(test)]
#[path = "spectrum_exchange_tests.rs"]
mod tests;
