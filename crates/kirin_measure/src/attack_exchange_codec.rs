//! Bounded binary snapshot for the optional exact-pair ATTACK presentation.

use std::path::Path;

use uuid::Uuid;

use super::attack_snapshot_path;
use crate::analysis_exchange_transport::{self, AnalysisSlot};
use crate::{
    AttackDetailedEvent, AttackEvent, AttackEventShape, AttackHistory, AttackOdfFrame,
    AttackPerceptualFeatures, AttackWaveformPoint, ATTACK_EVENT_HISTORY_CAPACITY,
    ATTACK_ODF_HISTORY_CAPACITY, ATTACK_SHAPE_POINT_CAPACITY, ATTACK_WAVEFORM_HISTORY_CAPACITY,
};

const SNAPSHOT_MAGIC: &[u8; 8] = b"KHATK001";
const SNAPSHOT_VERSION: u16 = 1;
pub(super) const ATTACK_SNAPSHOT_MAX_BYTES: u64 = 196_608;

pub(super) struct DecodedAttackSnapshot {
    pub(super) request_id: Uuid,
    pub(super) history: AttackHistory,
}

pub(super) fn read_attack_snapshot(instance_dir: &Path) -> Option<DecodedAttackSnapshot> {
    decode_attack_snapshot(&analysis_exchange_transport::read(
        instance_dir,
        &attack_snapshot_path(instance_dir),
        AnalysisSlot::Attack,
        ATTACK_SNAPSHOT_MAX_BYTES,
    )?)
}

pub(super) fn write_attack_snapshot(instance_dir: &Path, bytes: &[u8]) -> std::io::Result<()> {
    analysis_exchange_transport::write(
        instance_dir,
        &attack_snapshot_path(instance_dir),
        AnalysisSlot::Attack,
        bytes,
    )
}

pub(super) fn remove_attack_snapshot(instance_dir: &Path) {
    let _ = analysis_exchange_transport::remove(
        instance_dir,
        &attack_snapshot_path(instance_dir),
        AnalysisSlot::Attack,
    );
}

pub(super) fn encode_attack_snapshot(request_id: Uuid, history: &AttackHistory) -> Vec<u8> {
    let Some(identity) = history.newest() else {
        return Vec::new();
    };
    let frame_count = history.frames().len().min(ATTACK_ODF_HISTORY_CAPACITY) as u16;
    let waveform_count = history
        .waveform()
        .len()
        .min(ATTACK_WAVEFORM_HISTORY_CAPACITY) as u16;
    let detail_count = history.details().len().min(ATTACK_EVENT_HISTORY_CAPACITY) as u16;
    let mut bytes = Vec::with_capacity(
        92 + frame_count as usize * 12 + waveform_count as usize * 24 + detail_count as usize * 468,
    );
    bytes.extend_from_slice(SNAPSHOT_MAGIC);
    bytes.extend_from_slice(&SNAPSHOT_VERSION.to_le_bytes());
    bytes.extend_from_slice(&0_u16.to_le_bytes());
    bytes.extend_from_slice(request_id.as_bytes());
    bytes.extend_from_slice(&identity.sample_rate.to_le_bytes());
    bytes.push(identity.channels);
    bytes.extend_from_slice(&[0; 3]);
    bytes.extend_from_slice(&identity.generation.to_le_bytes());
    bytes.extend_from_slice(&identity.definition_hash);
    bytes.extend_from_slice(&identity.window_samples.to_le_bytes());
    bytes.extend_from_slice(&identity.hop_samples.to_le_bytes());
    bytes.extend_from_slice(&frame_count.to_le_bytes());
    bytes.extend_from_slice(&waveform_count.to_le_bytes());
    bytes.extend_from_slice(&detail_count.to_le_bytes());
    bytes.extend_from_slice(&0_u16.to_le_bytes());
    for frame in history.frames() {
        bytes.extend_from_slice(&frame.event_sample.to_le_bytes());
        bytes.extend_from_slice(&frame.value.to_le_bytes());
    }
    for point in history.waveform() {
        bytes.extend_from_slice(&point.start_sample.to_le_bytes());
        bytes.extend_from_slice(&point.end_sample.to_le_bytes());
        bytes.extend_from_slice(&point.peak_linear.to_le_bytes());
        bytes.extend_from_slice(&point.rms_dbfs.to_le_bytes());
    }
    for detail in history.details() {
        encode_detail(&mut bytes, detail);
    }
    bytes
}

fn encode_detail(bytes: &mut Vec<u8>, detail: &AttackDetailedEvent) {
    let features = detail.features;
    bytes.extend_from_slice(&detail.event.event_sample.to_le_bytes());
    bytes.extend_from_slice(&detail.event.decision_sample.to_le_bytes());
    bytes.extend_from_slice(&detail.event.value.to_le_bytes());
    bytes.push(features.contrast_floor_limited as u8);
    bytes.push(features.temporal_centroid_ms.is_some() as u8);
    bytes.push(features.sharpness_acum.is_some() as u8);
    bytes.push(0);
    bytes.extend_from_slice(&features.context_frames.to_le_bytes());
    bytes.extend_from_slice(&features.attack_frames.to_le_bytes());
    for value in [
        features.contrast_db,
        features.context_rms_dbfs,
        features.attack_rms_dbfs,
        features.sample_peak_dbfs,
        features.crest_db,
        features.sample_edge_ratio_db,
        features.peak_plateau_ms,
        features.temporal_centroid_ms.unwrap_or(0.0),
        features.sharpness_acum.unwrap_or(0.0),
    ] {
        bytes.extend_from_slice(&value.to_le_bytes());
    }
    bytes.extend_from_slice(&detail.shape.start_sample.to_le_bytes());
    bytes.extend_from_slice(&detail.shape.end_sample.to_le_bytes());
    for value in detail.shape.points {
        bytes.extend_from_slice(&value.to_le_bytes());
    }
}

pub(super) fn decode_attack_snapshot(bytes: &[u8]) -> Option<DecodedAttackSnapshot> {
    let mut cursor = Cursor::new(bytes);
    (cursor.take(8)? == SNAPSHOT_MAGIC).then_some(())?;
    (cursor.u16()? == SNAPSHOT_VERSION).then_some(())?;
    let _reserved = cursor.u16()?;
    let request_id = Uuid::from_slice(cursor.take(16)?).ok()?;
    let sample_rate = cursor.u32()?;
    let channels = cursor.u8()?;
    (sample_rate > 0 && matches!(channels, 1 | 2)).then_some(())?;
    let _reserved = cursor.take(3)?;
    let generation = cursor.u64()?;
    (generation > 0).then_some(())?;
    let definition_hash: [u8; 32] = cursor.take(32)?.try_into().ok()?;
    let window_samples = cursor.u32()?;
    let hop_samples = cursor.u32()?;
    let frame_count = cursor.u16()? as usize;
    let waveform_count = cursor.u16()? as usize;
    let detail_count = cursor.u16()? as usize;
    let _reserved = cursor.u16()?;
    (frame_count <= ATTACK_ODF_HISTORY_CAPACITY
        && waveform_count <= ATTACK_WAVEFORM_HISTORY_CAPACITY
        && detail_count <= ATTACK_EVENT_HISTORY_CAPACITY)
        .then_some(())?;
    let mut history = AttackHistory::with_capacity();
    for _ in 0..frame_count {
        let event_sample = cursor.i64()?;
        let support_start_samples = event_sample.checked_sub(i64::from(window_samples / 2))?;
        let support_end_samples = support_start_samples.checked_add(i64::from(window_samples))?;
        history.push(AttackOdfFrame {
            generation,
            sample_rate,
            channels,
            definition_hash,
            window_samples,
            hop_samples,
            support_start_samples,
            support_end_samples,
            event_sample,
            value: cursor.f32()?,
        });
    }
    for _ in 0..waveform_count {
        history.push_waveform(AttackWaveformPoint {
            generation,
            sample_rate,
            channels,
            start_sample: cursor.i64()?,
            end_sample: cursor.i64()?,
            peak_linear: cursor.f32()?,
            rms_dbfs: cursor.f32()?,
        });
    }
    for _ in 0..detail_count {
        let detail = decode_detail(
            &mut cursor,
            generation,
            sample_rate,
            channels,
            definition_hash,
        )?;
        history.push_event(detail.event);
        history.push_detail(detail);
    }
    (cursor.remaining() == 0).then_some(DecodedAttackSnapshot {
        request_id,
        history,
    })
}

fn decode_detail(
    cursor: &mut Cursor<'_>,
    generation: u64,
    sample_rate: u32,
    channels: u8,
    definition_hash: [u8; 32],
) -> Option<AttackDetailedEvent> {
    let event_sample = cursor.i64()?;
    let decision_sample = cursor.i64()?;
    let value = cursor.f32()?;
    let contrast_floor_limited = cursor.bool()?;
    let temporal_available = cursor.bool()?;
    let sharpness_available = cursor.bool()?;
    let _reserved = cursor.u8()?;
    let context_frames = cursor.u32()?;
    let attack_frames = cursor.u32()?;
    let values = cursor.f32_array::<9>()?;
    let shape_start = cursor.i64()?;
    let shape_end = cursor.i64()?;
    let points = cursor.f32_array::<ATTACK_SHAPE_POINT_CAPACITY>()?;
    let event = AttackEvent {
        generation,
        sample_rate,
        channels,
        definition_hash,
        event_sample,
        decision_sample,
        value,
    };
    let features = AttackPerceptualFeatures {
        sample_rate,
        channels,
        context_frames,
        attack_frames,
        contrast_db: values[0],
        contrast_floor_limited,
        context_rms_dbfs: values[1],
        attack_rms_dbfs: values[2],
        sample_peak_dbfs: values[3],
        crest_db: values[4],
        sample_edge_ratio_db: values[5],
        peak_plateau_ms: values[6],
        temporal_centroid_ms: temporal_available.then_some(values[7]),
        sharpness_acum: sharpness_available.then_some(values[8]),
    };
    let shape = AttackEventShape {
        start_sample: shape_start,
        end_sample: shape_end,
        event_sample,
        points,
    };
    let detail = AttackDetailedEvent {
        event,
        features,
        shape,
    };
    detail.has_valid_layout().then_some(detail)
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
    fn u8(&mut self) -> Option<u8> {
        Some(*self.take(1)?.first()?)
    }
    fn bool(&mut self) -> Option<bool> {
        match self.u8()? {
            0 => Some(false),
            1 => Some(true),
            _ => None,
        }
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
        let value = f32::from_le_bytes(self.take(4)?.try_into().ok()?);
        value.is_finite().then_some(value)
    }
    fn f32_array<const N: usize>(&mut self) -> Option<[f32; N]> {
        let mut values = [0.0; N];
        for value in &mut values {
            *value = self.f32()?;
        }
        Some(values)
    }
    fn remaining(&self) -> usize {
        self.bytes.len().saturating_sub(self.offset)
    }
}

#[cfg(test)]
#[path = "attack_exchange_codec_tests.rs"]
mod tests;
