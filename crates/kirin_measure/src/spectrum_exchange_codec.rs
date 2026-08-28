use std::fs;
use std::path::Path;

use uuid::Uuid;

use super::snapshot_path;
use crate::spectrum::{
    SpectrumChannelMode, SpectrumFrame, SPECTRUM_BAND_COUNT, SPECTRUM_FFT_SIZE,
    SPECTRUM_SCHEMA_VERSION,
};
use crate::spectrum_runtime::{SpectrumHistory, SPECTRUM_HISTORY_CAPACITY};

const SNAPSHOT_MAGIC: &[u8; 8] = b"KHSPEC02";
pub(super) const SNAPSHOT_MAX_BYTES: u64 = 16_384;

pub(super) struct DecodedSnapshot {
    pub(super) request_id: Uuid,
    pub(super) history: SpectrumHistory,
}

pub(super) fn read_snapshot(instance_dir: &Path) -> Option<DecodedSnapshot> {
    decode_snapshot(&read_bounded(
        &snapshot_path(instance_dir),
        SNAPSHOT_MAX_BYTES,
    )?)
}

pub(super) fn read_bounded(path: &Path, maximum_bytes: u64) -> Option<Vec<u8>> {
    (fs::metadata(path).ok()?.len() <= maximum_bytes)
        .then(|| fs::read(path).ok())
        .flatten()
}

pub(super) fn encode_snapshot(request_id: Uuid, history: &SpectrumHistory) -> Vec<u8> {
    let frame_count = history.frames().len().min(u16::MAX as usize) as u16;
    let mut bytes = Vec::with_capacity(40 + frame_count as usize * (28 + SPECTRUM_BAND_COUNT * 4));
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
        bytes.push(frame.channel_mode as u8);
        bytes.push(frame.channels);
        bytes.extend_from_slice(&0_u16.to_le_bytes());
        bytes.extend_from_slice(&frame.min_hz.to_le_bytes());
        bytes.extend_from_slice(&frame.max_hz.to_le_bytes());
        for value in frame.dbfs {
            bytes.extend_from_slice(&value.to_le_bytes());
        }
    }
    bytes
}

pub(super) fn decode_snapshot(bytes: &[u8]) -> Option<DecodedSnapshot> {
    let mut cursor = Cursor::new(bytes);
    (cursor.take(8)? == SNAPSHOT_MAGIC).then_some(())?;
    (cursor.u16()? == SPECTRUM_SCHEMA_VERSION).then_some(())?;
    (cursor.u16()? as usize == SPECTRUM_BAND_COUNT).then_some(())?;
    let sample_rate = cursor.u32()?;
    ((8_000..=384_000).contains(&sample_rate)).then_some(())?;
    (cursor.u32()? as usize == SPECTRUM_FFT_SIZE).then_some(())?;
    let frame_count = cursor.u16()? as usize;
    (frame_count <= SPECTRUM_HISTORY_CAPACITY).then_some(())?;
    let _reserved = cursor.u16()?;
    let request_id = Uuid::from_slice(cursor.take(16)?).ok()?;
    let mut history = SpectrumHistory::with_capacity();
    for _ in 0..frame_count {
        let presentation_end_samples = cursor.i64()?;
        let generation = cursor.u64()?;
        let channel_mode = SpectrumChannelMode::try_from(cursor.u8()?).ok()?;
        let channels = cursor.u8()?;
        ([1, 2].contains(&channels)).then_some(())?;
        if channel_mode == SpectrumChannelMode::Side && channels != 2 {
            return None;
        }
        let _reserved = cursor.u16()?;
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
            channel_mode,
            channels,
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
    fn f32(&mut self) -> Option<f32> {
        Some(f32::from_le_bytes(self.take(4)?.try_into().ok()?))
    }
    fn remaining(&self) -> usize {
        self.bytes.len().saturating_sub(self.offset)
    }
}
