//! Version 5 extension: explicit source evidence and every detected event, including pending.
use super::{band, Cursor};
use crate::attack_perception::band::AttackBand;
use crate::attack_runtime::snapshot::{
    AttackBandObservation, AttackFinish, AttackObservationSnapshot, AttackSourceEvidence,
    AttackSourceKey,
};
use crate::attack_runtime::AttackBandResults;
use crate::{AttackEvent, AttackHistory};

pub(super) const EXTENSION_MAX_BYTES: usize = 160 + 240 * (48 + 20);

pub(super) fn encode(
    bytes: &mut Vec<u8>,
    history: &AttackHistory,
    snapshot: &AttackObservationSnapshot,
) {
    let source = snapshot.source.unwrap();
    bytes.extend_from_slice(&source.source.incarnation);
    bytes.extend_from_slice(&source.source.generation.to_le_bytes());
    bytes.extend_from_slice(&source.source.sample_rate.to_le_bytes());
    bytes.push(source.source.channels);
    bytes.extend_from_slice(&[0; 3]);
    bytes.extend_from_slice(&source.source.odf_hash);
    for value in [
        source.odf_support_start,
        source.odf_support_end,
        source.pcm_start,
        source.pcm_end,
        source.cutoff,
    ] {
        bytes.extend_from_slice(&value.to_le_bytes());
    }
    bytes.extend_from_slice(&source.band_semantic_hash);
    bytes.extend_from_slice(&source.clock_policy.to_le_bytes());
    bytes.extend_from_slice(&snapshot.revision.to_le_bytes());
    bytes.push(snapshot.band.map_or(0, AttackBand::index));
    bytes.push(0);
    bytes.extend_from_slice(&(snapshot.own.len() as u16).to_le_bytes());
    bytes.extend_from_slice(&(history.events().len() as u16).to_le_bytes());
    bytes.extend_from_slice(&0_u16.to_le_bytes());
    for event in history.events() {
        encode_event(bytes, event);
    }
    for entry in &snapshot.own {
        encode_event(bytes, &entry.event);
        for value in [entry.requested_end, entry.actual_end] {
            bytes.extend_from_slice(&value.to_le_bytes());
        }
        bytes.extend_from_slice(&entry.revision.to_le_bytes());
        bytes.push(entry.finish as u8);
        bytes.extend_from_slice(&[0; 3]);
    }
}

fn encode_event(bytes: &mut Vec<u8>, event: &AttackEvent) {
    bytes.extend_from_slice(&event.event_sample.to_le_bytes());
    bytes.extend_from_slice(&event.decision_sample.to_le_bytes());
    bytes.extend_from_slice(&event.value.to_le_bytes());
}

pub(super) fn decode(
    cursor: &mut Cursor<'_>,
    history: &mut AttackHistory,
    results: Option<&AttackBandResults>,
) -> Option<AttackObservationSnapshot> {
    let incarnation = cursor.take(16)?.try_into().ok()?;
    let generation = cursor.u64()?;
    let sample_rate = cursor.u32()?;
    let channels = cursor.u8()?;
    (cursor.take(3)? == [0; 3]).then_some(())?;
    let odf_hash = cursor.take(32)?.try_into().ok()?;
    let source = AttackSourceEvidence {
        source: AttackSourceKey {
            incarnation,
            generation,
            sample_rate,
            channels,
            odf_hash,
        },
        odf_support_start: cursor.i64()?,
        odf_support_end: cursor.i64()?,
        pcm_start: cursor.i64()?,
        pcm_end: cursor.i64()?,
        cutoff: cursor.i64()?,
        band_semantic_hash: cursor.take(32)?.try_into().ok()?,
        clock_policy: cursor.u64()?,
    };
    source.valid().then_some(())?;
    let header = history.newest()?;
    (header.generation == generation
        && header.sample_rate == sample_rate
        && header.channels == channels
        && header.definition_hash == odf_hash)
        .then_some(())?;
    let revision = cursor.u64()?;
    let index = cursor.u8()?;
    let band = if index == 0 {
        None
    } else {
        Some(AttackBand::from_index(index)?)
    };
    (cursor.u8()? == 0).then_some(())?;
    let count = usize::from(cursor.u16()?);
    let event_count = usize::from(cursor.u16()?);
    (cursor.u16()? == 0
        && count <= 240
        && event_count <= 240
        && results.map(|value| value.band) == band.map(Some)
        && (band.is_some() || count == 0))
        .then_some(())?;
    let mut events = Vec::with_capacity(event_count);
    for _ in 0..event_count {
        let event = decode_event(cursor, source.source)?;
        if events
            .last()
            .is_some_and(|previous: &AttackEvent| previous.event_sample >= event.event_sample)
        {
            return None;
        }
        events.push(event);
    }
    history.replace_wire_events(&events)?;
    let mut snapshot = AttackObservationSnapshot {
        source: Some(source),
        band,
        revision,
        ..Default::default()
    };
    for _ in 0..count {
        let event = decode_event(cursor, source.source)?;
        if events.iter().all(|actual| *actual != event)
            || snapshot
                .own
                .last()
                .is_some_and(|previous| previous.event.event_sample >= event.event_sample)
        {
            return None;
        }
        let requested_end = cursor.i64()?;
        let actual_end = cursor.i64()?;
        let entry_revision = cursor.u64()?;
        let finish = match cursor.u8()? {
            0 => AttackFinish::Acquiring,
            1 => AttackFinish::Full,
            2 => AttackFinish::AudioEnd,
            3 => AttackFinish::NotKept,
            4 => AttackFinish::Retired,
            _ => return None,
        };
        (cursor.take(3)? == [0; 3]).then_some(())?;
        let detail = results.and_then(|results| results.own_at(event.event_sample));
        let measure = detail.and_then(|detail| detail.measure);
        if detail
            .is_some_and(|detail| detail.span_end_sample != requested_end || detail.event != event)
        {
            return None;
        }
        let mut entry = if finish == AttackFinish::Acquiring {
            if measure.is_some() {
                return None;
            }
            AttackBandObservation::pending(event, requested_end, entry_revision)
        } else {
            AttackBandObservation::measured(event, requested_end, entry_revision, measure)
        };
        if entry.finish != finish || entry.actual_end != actual_end || !entry.valid() {
            return None;
        }
        entry.revision = entry_revision;
        snapshot.own.push(entry);
    }
    Some(snapshot)
}

fn decode_event(cursor: &mut Cursor<'_>, source: AttackSourceKey) -> Option<AttackEvent> {
    let event = AttackEvent {
        generation: source.generation,
        sample_rate: source.sample_rate,
        channels: source.channels,
        definition_hash: source.odf_hash,
        event_sample: cursor.i64()?,
        decision_sample: cursor.i64()?,
        value: cursor.f32()?,
    };
    event.has_valid_layout().then_some(event)
}

// Fixed v4 detail bytes remain untouched; the extension's measure refers to that same record.
const _: () = assert!(band::BAND_DETAIL_BYTES > 0);

#[cfg(test)]
#[path = "attack_exchange_codec_observation_tests.rs"]
mod tests;
