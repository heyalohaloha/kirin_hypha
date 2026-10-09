//! Bounded binary snapshot for the optional exact-pair ATTACK presentation.

use std::path::Path;

use uuid::Uuid;

use super::attack_snapshot_path;
use crate::analysis_exchange_transport::{self, AnalysisSlot};
use crate::attack_perception::band::{AttackBand, ATTACK_BAND_HISTORY_CAPACITY};
use crate::attack_runtime::AttackBandResults;
use crate::{
    AttackDetailedEvent, AttackEvent, AttackEventShape, AttackHistory, AttackOdfFrame,
    AttackPerceptualFeatures, AttackWaveformPoint, ATTACK_EVENT_HISTORY_CAPACITY,
    ATTACK_ODF_HISTORY_CAPACITY, ATTACK_SHAPE_POINT_CAPACITY, ATTACK_WAVEFORM_HISTORY_CAPACITY,
};

const SNAPSHOT_MAGIC: &[u8; 8] = b"KHATK001";
/// Version 2 (B-1016): content-grid windows, body end, TRANSIENT as head minus body, and the
/// loudness-weighted Sharpness of the 100 ms from the onset. Version 3 (B-1024): a detail may be
/// head-only (`complete` = 0) and its shape covers only the measured span. Version 4 (B-1096,
/// fixed records since B-1098): written only while a band is requested, it appends the band and
/// every hit's outcome in it. A POST that never requests a band keeps receiving version 3.
const SNAPSHOT_VERSION: u16 = 3;
const SNAPSHOT_VERSION_BAND: u16 = 4;
const SNAPSHOT_VERSION_OBSERVATION: u16 = 5;
const HEADER_BYTES: usize = 92;
const FRAME_BYTES: usize = 12;
const WAVEFORM_BYTES: usize = 24;
const DETAIL_BYTES: usize = 460;
pub(super) const ATTACK_SNAPSHOT_MAX_BYTES: u64 = 262_144;
/// Every history at its bounds, with a full band section, fits: the reader never refuses a
/// snapshot the writer produced, however dense the hits.
pub(super) const ATTACK_SNAPSHOT_WORST_CASE_BYTES: usize = HEADER_BYTES
    + ATTACK_ODF_HISTORY_CAPACITY * FRAME_BYTES
    + ATTACK_WAVEFORM_HISTORY_CAPACITY * WAVEFORM_BYTES
    + ATTACK_EVENT_HISTORY_CAPACITY * DETAIL_BYTES
    + band::BAND_SECTION_HEADER_BYTES
    + ATTACK_BAND_HISTORY_CAPACITY * band::BAND_DETAIL_BYTES;
const _: () = assert!(ATTACK_SNAPSHOT_WORST_CASE_BYTES as u64 <= ATTACK_SNAPSHOT_MAX_BYTES);

#[path = "attack_exchange_codec_band.rs"]
mod band;
#[path = "attack_exchange_codec_observation.rs"]
mod observation;
const _: () = assert!(
    ATTACK_SNAPSHOT_WORST_CASE_BYTES + observation::EXTENSION_MAX_BYTES
        <= ATTACK_SNAPSHOT_MAX_BYTES as usize
);

pub(super) struct DecodedAttackSnapshot {
    pub(super) request_id: Uuid,
    pub(super) history: AttackHistory,
    /// Version 4: the band PRE declares and its hits in it. `None` for version 3, which a PRE
    /// that predates bands always writes.
    pub(super) band_results: Option<AttackBandResults>,
    pub(super) observations: Option<crate::attack_runtime::snapshot::AttackObservationSnapshot>,
}

pub(super) fn read_attack_snapshot(instance_dir: &Path) -> Option<DecodedAttackSnapshot> {
    decode_attack_snapshot(&analysis_exchange_transport::read(
        instance_dir,
        &attack_snapshot_path(instance_dir),
        AnalysisSlot::Attack,
        ATTACK_SNAPSHOT_MAX_BYTES,
    )?)
}

pub(super) fn read_attack_snapshot_request_id(instance_dir: &Path) -> Option<Uuid> {
    let bytes = analysis_exchange_transport::read_prefix(
        instance_dir,
        &attack_snapshot_path(instance_dir),
        AnalysisSlot::Attack,
        28,
        ATTACK_SNAPSHOT_MAX_BYTES,
    )?;
    let mut cursor = Cursor::new(&bytes);
    (cursor.take(8)? == SNAPSHOT_MAGIC).then_some(())?;
    matches!(cursor.u16()?, 3..=5).then_some(())?;
    let _flags = cursor.u16()?;
    Uuid::from_slice(cursor.take(16)?).ok()
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

/// `band` is the band POST asked for; `results` what this side's worker published. Version 4 is
/// written whenever a band is asked for, with no hits while the results are another band's/run's.
pub(super) fn encode_attack_snapshot(
    request_id: Uuid,
    history: &AttackHistory,
    band: Option<AttackBand>,
    results: &AttackBandResults,
) -> Vec<u8> {
    let Some(identity) = history.newest() else {
        return Vec::new();
    };
    let frame_count = history.frames().len().min(ATTACK_ODF_HISTORY_CAPACITY) as u16;
    let waveform_count = history
        .waveform()
        .len()
        .min(ATTACK_WAVEFORM_HISTORY_CAPACITY) as u16;
    let detail_count = history.details().len().min(ATTACK_EVENT_HISTORY_CAPACITY) as u16;
    let band_count = band.map_or(0, |_| results.own().len().min(ATTACK_BAND_HISTORY_CAPACITY));
    let mut bytes = Vec::with_capacity(
        HEADER_BYTES
            + frame_count as usize * FRAME_BYTES
            + waveform_count as usize * WAVEFORM_BYTES
            + detail_count as usize * DETAIL_BYTES
            + band::BAND_SECTION_HEADER_BYTES
            + band_count * band::BAND_DETAIL_BYTES,
    );
    bytes.extend_from_slice(SNAPSHOT_MAGIC);
    let version = if band.is_some() {
        SNAPSHOT_VERSION_BAND
    } else {
        SNAPSHOT_VERSION
    };
    bytes.extend_from_slice(&version.to_le_bytes());
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
    if let Some(band) = band {
        band::encode_band_section(
            &mut bytes,
            band,
            results,
            &band::BandIdentity {
                generation: identity.generation,
                sample_rate: identity.sample_rate,
                channels: identity.channels,
                definition_hash: identity.definition_hash,
            },
        );
    }
    bytes
}

pub(super) fn encode_attack_observation_snapshot(
    request_id: Uuid,
    history: &AttackHistory,
    observations: &crate::attack_runtime::snapshot::AttackObservationSnapshot,
) -> Vec<u8> {
    let Some(source) = observations.source.filter(|value| value.valid()) else {
        return Vec::new();
    };
    let Some(identity) = history.newest() else {
        return Vec::new();
    };
    if source.source.generation != identity.generation
        || source.source.sample_rate != identity.sample_rate
        || source.source.channels != identity.channels
        || source.source.odf_hash != identity.definition_hash
    {
        return Vec::new();
    }
    // Build the v4 measure records from the exact same typed publication. Separate legacy cache
    // reads cannot attach a new physical window to an old finish/mask packet.
    let mut observations = observations.clone();
    observations
        .own
        .retain(|entry| history.events().any(|event| *event == entry.event));
    let band = observations.band;
    let mut results = AttackBandResults::new(band, source.source.generation);
    if let Some(band) = band {
        for entry in &observations.own {
            if entry.finish != crate::attack_runtime::snapshot::AttackFinish::Acquiring {
                results.put_own(crate::attack_runtime::AttackBandDetail {
                    event: entry.event,
                    band,
                    span_end_sample: entry.requested_end,
                    measure: entry.measure,
                });
            }
        }
    }
    let mut bytes = encode_attack_snapshot(request_id, history, band, &results);
    bytes[8..10].copy_from_slice(&SNAPSHOT_VERSION_OBSERVATION.to_le_bytes());
    bytes[10] = band.map_or(0, AttackBand::index);
    observation::encode(&mut bytes, history, &observations);
    bytes
}

fn encode_detail(bytes: &mut Vec<u8>, detail: &AttackDetailedEvent) {
    let features = detail.features;
    bytes.extend_from_slice(&detail.event.event_sample.to_le_bytes());
    bytes.extend_from_slice(&detail.event.decision_sample.to_le_bytes());
    bytes.extend_from_slice(&detail.event.value.to_le_bytes());
    bytes.push(features.body_rms_dbfs.is_some() as u8);
    bytes.push(features.sharpness_acum.is_some() as u8);
    bytes.push(features.complete as u8);
    bytes.push(0);
    bytes.extend_from_slice(&features.bin_frames.to_le_bytes());
    bytes.extend_from_slice(&features.body_end_sample.to_le_bytes());
    for value in [
        features.attack_rms_dbfs,
        features.sample_peak_dbfs,
        features.crest_db,
        features.body_rms_dbfs.unwrap_or(0.0),
        features.transient_db.unwrap_or(0.0),
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
    let version = cursor.u16()?;
    (version == SNAPSHOT_VERSION
        || version == SNAPSHOT_VERSION_BAND
        || version == SNAPSHOT_VERSION_OBSERVATION)
        .then_some(())?;
    let flags = cursor.u16()?;
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
    let band_results = if version == SNAPSHOT_VERSION_BAND
        || (version == SNAPSHOT_VERSION_OBSERVATION && flags != 0)
    {
        Some(band::decode_band_section(
            &mut cursor,
            &band::BandIdentity {
                generation,
                sample_rate,
                channels,
                definition_hash,
            },
        )?)
    } else {
        None
    };
    let observations = if version == SNAPSHOT_VERSION_OBSERVATION {
        let snapshot = observation::decode(&mut cursor, &mut history, band_results.as_ref())?;
        (u16::from(snapshot.band.map_or(0, AttackBand::index)) == flags).then_some(())?;
        Some(snapshot)
    } else {
        None
    };
    (cursor.remaining() == 0).then_some(DecodedAttackSnapshot {
        request_id,
        history,
        band_results,
        observations,
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
    let body_available = cursor.bool()?;
    let sharpness_available = cursor.bool()?;
    let complete = cursor.bool()?;
    let _reserved = cursor.u8()?;
    let bin_frames = cursor.u32()?;
    let body_end_sample = cursor.i64()?;
    let values = cursor.f32_array::<6>()?;
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
    let bin = i64::from(bin_frames);
    let features = AttackPerceptualFeatures {
        sample_rate,
        channels,
        bin_frames,
        window_start_sample: event_sample.div_euclid(bin.max(1)) * bin,
        attack_rms_dbfs: values[0],
        sample_peak_dbfs: values[1],
        crest_db: values[2],
        complete,
        body_end_sample,
        body_rms_dbfs: body_available.then_some(values[3]),
        transient_db: body_available.then_some(values[4]),
        sharpness_acum: sharpness_available.then_some(values[5]),
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
