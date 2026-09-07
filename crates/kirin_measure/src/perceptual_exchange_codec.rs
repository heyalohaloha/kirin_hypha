use std::path::Path;

use uuid::Uuid;

use super::perceptual_snapshot_path;
use crate::analysis_exchange_transport::{self, AnalysisSlot};
use crate::perceptual::{PerceptualFrame, PERCEPTUAL_SCHEMA_VERSION};
use crate::spectrum::SpectrumChannelMode;
use crate::spectrum_runtime::{PerceptualHistory, PERCEPTUAL_HISTORY_CAPACITY};

const SNAPSHOT_MAGIC: &[u8; 8] = b"KHPERC03";
pub(super) const PERCEPTUAL_SNAPSHOT_MAX_BYTES: u64 = 4_096;

pub(super) struct DecodedPerceptualSnapshot {
    pub(super) request_id: Uuid,
    pub(super) history: PerceptualHistory,
}

pub(super) fn read_perceptual_snapshot(instance_dir: &Path) -> Option<DecodedPerceptualSnapshot> {
    decode_perceptual_snapshot(&analysis_exchange_transport::read(
        instance_dir,
        &perceptual_snapshot_path(instance_dir),
        AnalysisSlot::Perceptual,
        PERCEPTUAL_SNAPSHOT_MAX_BYTES,
    )?)
}

pub(super) fn write_perceptual_snapshot(instance_dir: &Path, bytes: &[u8]) -> std::io::Result<()> {
    analysis_exchange_transport::write(
        instance_dir,
        &perceptual_snapshot_path(instance_dir),
        AnalysisSlot::Perceptual,
        bytes,
    )
}

pub(super) fn remove_perceptual_snapshot(instance_dir: &Path) {
    let _ = analysis_exchange_transport::remove(
        instance_dir,
        &perceptual_snapshot_path(instance_dir),
        AnalysisSlot::Perceptual,
    );
}

pub(super) fn encode_perceptual_snapshot(request_id: Uuid, history: &PerceptualHistory) -> Vec<u8> {
    let frame_count = history.frames().len().min(u16::MAX as usize) as u16;
    let mut bytes = Vec::with_capacity(32 + frame_count as usize * 201);
    bytes.extend_from_slice(SNAPSHOT_MAGIC);
    bytes.extend_from_slice(&PERCEPTUAL_SCHEMA_VERSION.to_le_bytes());
    bytes.extend_from_slice(&frame_count.to_le_bytes());
    bytes.extend_from_slice(
        &history
            .newest()
            .map_or(0, |frame| frame.sample_rate)
            .to_le_bytes(),
    );
    bytes.extend_from_slice(request_id.as_bytes());
    for frame in history.frames() {
        bytes.extend_from_slice(&frame.presentation_end_samples.to_le_bytes());
        bytes.extend_from_slice(&frame.state_epoch_samples.to_le_bytes());
        bytes.extend_from_slice(&frame.generation.to_le_bytes());
        bytes.push(frame.channel_mode as u8);
        bytes.push(frame.channels);
        bytes.extend_from_slice(&0_u16.to_le_bytes());
        bytes.extend_from_slice(&frame.aperture_samples.to_le_bytes());
        bytes.extend_from_slice(&frame.sharpness.to_le_bytes());
        bytes.push(u8::from(frame.psb.is_some()));
        for value in frame.psb.unwrap_or([0.0; 20]) {
            bytes.extend_from_slice(&value.to_le_bytes());
        }
    }
    bytes
}

pub(super) fn decode_perceptual_snapshot(bytes: &[u8]) -> Option<DecodedPerceptualSnapshot> {
    (bytes.len() <= PERCEPTUAL_SNAPSHOT_MAX_BYTES as usize).then_some(())?;
    let mut cursor = Cursor::new(bytes);
    (cursor.take(8)? == SNAPSHOT_MAGIC).then_some(())?;
    (cursor.u16()? == PERCEPTUAL_SCHEMA_VERSION).then_some(())?;
    let frame_count = cursor.u16()? as usize;
    (frame_count <= PERCEPTUAL_HISTORY_CAPACITY).then_some(())?;
    let sample_rate = cursor.u32()?;
    ((8_000..=384_000).contains(&sample_rate)
        && sample_rate.is_multiple_of(crate::PERCEPTUAL_PRESENTATION_HZ))
    .then_some(())?;
    let request_id = Uuid::from_slice(cursor.take(16)?).ok()?;
    let mut history = PerceptualHistory::with_capacity();
    for _ in 0..frame_count {
        let presentation_end_samples = cursor.i64()?;
        let state_epoch_samples = cursor.i64()?;
        let generation = cursor.u64()?;
        (generation != 0).then_some(())?;
        let channel_mode = SpectrumChannelMode::try_from(cursor.u8()?).ok()?;
        let channels = cursor.u8()?;
        ([1, 2].contains(&channels)).then_some(())?;
        if channel_mode == SpectrumChannelMode::Side && channels != 2 {
            return None;
        }
        (cursor.u16()? == 0).then_some(())?;
        let aperture_samples = cursor.u32()?;
        (aperture_samples == sample_rate / crate::PERCEPTUAL_PRESENTATION_HZ).then_some(())?;
        (state_epoch_samples.rem_euclid(i64::from(aperture_samples)) == 0
            && presentation_end_samples > state_epoch_samples
            && presentation_end_samples.rem_euclid(i64::from(aperture_samples)) == 0)
            .then_some(())?;
        let sharpness = cursor.f64()?;
        (sharpness.is_finite() && (0.0..=100.0).contains(&sharpness)).then_some(())?;
        let has_psb = cursor.u8()?;
        let mut shares = [0.0; 20];
        for value in &mut shares {
            *value = cursor.f64()?;
        }
        let psb = match has_psb {
            0 if shares == [0.0; 20] => None,
            1 if crate::phase_d::display::valid_shares(&shares) => Some(shares),
            _ => return None,
        };
        if history
            .newest()
            .is_some_and(|f| presentation_end_samples <= f.presentation_end_samples)
        {
            return None;
        }
        history.push(PerceptualFrame {
            schema_version: PERCEPTUAL_SCHEMA_VERSION,
            sample_rate,
            aperture_samples,
            presentation_end_samples,
            state_epoch_samples,
            generation,
            channel_mode,
            channels,
            sharpness,
            psb,
        });
    }
    (cursor.remaining() == 0).then_some(DecodedPerceptualSnapshot {
        request_id,
        history,
    })
}

#[cfg(test)]
#[path = "perceptual_exchange_codec_tests.rs"]
mod tests;

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
    fn f64(&mut self) -> Option<f64> {
        Some(f64::from_le_bytes(self.take(8)?.try_into().ok()?))
    }
    fn remaining(&self) -> usize {
        self.bytes.len().saturating_sub(self.offset)
    }
}
