//! The band section of an ATTACK snapshot (version 4): the band PRE measures and every one of its
//! hits in that band, each outcome stated. A fixed record per hit, envelopes in centi-dBFS, so a
//! full history with a full band section always fits the snapshot bound (asserted in the codec).

use super::Cursor;
use crate::attack_perception::band::{
    AttackBand, AttackBandMeasure, BandArrival, BandEnvelope, BandRelease, BandSound, BandSpanEnd,
    ATTACK_BAND_HEAD_POINTS, ATTACK_BAND_HISTORY_CAPACITY, ATTACK_BAND_TAIL_POINTS,
};
use crate::attack_runtime::{AttackBandDetail, AttackBandResults};
use crate::AttackEvent;

/// Band byte, reserved byte and count.
pub(super) const BAND_SECTION_HEADER_BYTES: usize = 4;
/// Event (8 + 8 + 4), requested tail end (8), five state bytes and three reserved (8), the
/// measure's own tail end (8), five values (20), and the envelope (2 × (96 + 64)).
pub(super) const BAND_DETAIL_BYTES: usize =
    20 + 8 + 8 + 8 + 20 + 2 * (ATTACK_BAND_HEAD_POINTS + ATTACK_BAND_TAIL_POINTS);

/// The identity every decoded hit takes from the snapshot header.
pub(super) struct BandIdentity {
    pub(super) generation: u64,
    pub(super) sample_rate: u32,
    pub(super) channels: u8,
    pub(super) definition_hash: [u8; 32],
}

/// The declared band and its hits. Results are published separately from history: only records
/// of this header's band/run/definition may be encoded, never re-stamped as a newer run on decode.
pub(super) fn encode_band_section(
    bytes: &mut Vec<u8>,
    band: AttackBand,
    results: &AttackBandResults,
    identity: &BandIdentity,
) {
    let details = if results.band == Some(band) && results.generation == identity.generation {
        results.own()
    } else {
        &[]
    };
    let qualified = || {
        details.iter().filter(|detail| {
            detail.matches_run(
                identity.generation,
                identity.sample_rate,
                identity.channels,
                &identity.definition_hash,
            )
        })
    };
    let available = qualified().count();
    let skip = available.saturating_sub(ATTACK_BAND_HISTORY_CAPACITY);
    let count = (available - skip) as u16;
    bytes.push(band.index());
    bytes.push(0);
    bytes.extend_from_slice(&count.to_le_bytes());
    for detail in qualified().skip(skip) {
        encode_band_detail(bytes, detail);
    }
}

fn encode_band_detail(bytes: &mut Vec<u8>, detail: &AttackBandDetail) {
    let start = bytes.len();
    bytes.extend_from_slice(&detail.event.event_sample.to_le_bytes());
    bytes.extend_from_slice(&detail.event.decision_sample.to_le_bytes());
    bytes.extend_from_slice(&detail.event.value.to_le_bytes());
    bytes.extend_from_slice(&detail.span_end_sample.to_le_bytes());
    let measure = detail.measure;
    let (sound, arrival, release) = match measure.map(|measure| measure.sound) {
        Some(BandSound::Rises { arrival, release }) => (0, Some(arrival), Some(release)),
        Some(BandSound::RingsOn) => (1, None, None),
        Some(BandSound::Silent) | None => (2, None, None),
    };
    let (arrival_state, arrival_frames, attack_frames) = match arrival {
        Some(BandArrival::At {
            arrival_frames,
            attack_frames,
        }) => (0, arrival_frames, attack_frames),
        _ => (1, 0.0, 0.0),
    };
    let (release_state, release_frames) = match release {
        Some(BandRelease::At(frames)) => (0, frames),
        Some(BandRelease::CutByNextHit) | None => (1, 0.0),
        Some(BandRelease::AtLeast(frames)) => (2, frames),
    };
    bytes.extend_from_slice(&[
        measure.is_some() as u8,
        measure.map_or(0, |measure| span_code(measure.span_end)),
        sound,
        arrival_state,
        release_state,
        0,
        0,
        0,
    ]);
    bytes.extend_from_slice(
        &measure
            .map_or(detail.span_end_sample, |measure| measure.span_end_sample)
            .to_le_bytes(),
    );
    for value in [
        measure.map_or(0.0, |measure| measure.peak_frames),
        measure.map_or(0.0, |measure| measure.level_dbfs),
        arrival_frames,
        attack_frames,
        release_frames,
    ] {
        bytes.extend_from_slice(&value.to_le_bytes());
    }
    let envelope = measure.map_or_else(BandEnvelope::default, |measure| measure.envelope);
    for value in envelope.head.iter().chain(envelope.tail.iter()) {
        bytes.extend_from_slice(&value.to_le_bytes());
    }
    debug_assert_eq!(bytes.len() - start, BAND_DETAIL_BYTES);
}

fn span_code(span: BandSpanEnd) -> u8 {
    match span {
        BandSpanEnd::Window => 0,
        BandSpanEnd::NextHit => 1,
        BandSpanEnd::AudioEnd => 2,
    }
}

/// `None` refuses the whole snapshot: an unknown band, too many hits, or any hit whose outcome
/// does not hold together.
pub(super) fn decode_band_section(
    cursor: &mut Cursor<'_>,
    identity: &BandIdentity,
) -> Option<AttackBandResults> {
    let band = AttackBand::from_index(cursor.u8()?)?;
    let _reserved = cursor.u8()?;
    let count = cursor.u16()? as usize;
    (count <= ATTACK_BAND_HISTORY_CAPACITY).then_some(())?;
    let mut results = AttackBandResults::new(Some(band), identity.generation);
    for _ in 0..count {
        let detail = decode_band_detail(cursor, band, identity)?;
        results.put_own(detail).then_some(())?;
    }
    (results.own().len() == count).then_some(results)
}

fn decode_band_detail(
    cursor: &mut Cursor<'_>,
    band: AttackBand,
    identity: &BandIdentity,
) -> Option<AttackBandDetail> {
    let event = AttackEvent {
        generation: identity.generation,
        sample_rate: identity.sample_rate,
        channels: identity.channels,
        definition_hash: identity.definition_hash,
        event_sample: cursor.i64()?,
        decision_sample: cursor.i64()?,
        value: cursor.f32()?,
    };
    let span_end_sample = cursor.i64()?;
    let kept = cursor.bool()?;
    let span = match cursor.u8()? {
        0 => BandSpanEnd::Window,
        1 => BandSpanEnd::NextHit,
        2 => BandSpanEnd::AudioEnd,
        _ => return None,
    };
    let (sound, arrival_state, release_state) = (cursor.u8()?, cursor.u8()?, cursor.u8()?);
    let _reserved = cursor.take(3)?;
    let measure_span_end = cursor.i64()?;
    let [peak_frames, level_dbfs, arrival_frames, attack_frames, release_frames] =
        cursor.f32_array::<5>()?;
    let mut envelope = BandEnvelope::default();
    for value in envelope.head.iter_mut().chain(envelope.tail.iter_mut()) {
        *value = i16::from_le_bytes(cursor.take(2)?.try_into().ok()?);
    }
    let measure = if kept {
        let sound = match sound {
            0 => BandSound::Rises {
                arrival: match arrival_state {
                    0 => BandArrival::At {
                        arrival_frames,
                        attack_frames,
                    },
                    1 => BandArrival::Ringing,
                    _ => return None,
                },
                release: match release_state {
                    0 => BandRelease::At(release_frames),
                    1 => BandRelease::CutByNextHit,
                    2 => BandRelease::AtLeast(release_frames),
                    _ => return None,
                },
            },
            1 => BandSound::RingsOn,
            2 => BandSound::Silent,
            _ => return None,
        };
        Some(AttackBandMeasure {
            band,
            sample_rate: identity.sample_rate,
            channels: identity.channels,
            event_sample: event.event_sample,
            span_end_sample: measure_span_end,
            span_end: span,
            peak_frames,
            level_dbfs,
            sound,
            envelope,
        })
    } else {
        None
    };
    let detail = AttackBandDetail {
        event,
        band,
        span_end_sample,
        measure,
    };
    detail.has_valid_layout().then_some(detail)
}
