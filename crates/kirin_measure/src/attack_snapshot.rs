//! Typed observation facts. No field here changes the detector or a band measurement.
use super::{AttackEvent, AttackOdfFrame};
use crate::attack_perception::band::{
    frames_for_micros, AttackBand, AttackBandMeasure, BandSpanEnd, ATTACK_BAND_HEAD_LEAD_MICROS,
    ATTACK_BAND_HEAD_POINTS, ATTACK_BAND_HEAD_SPAN_MICROS, ATTACK_BAND_TAIL_MICROS,
    ATTACK_BAND_TAIL_POINTS,
};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum AttackObservationReadError {
    Busy,
    SourceUnavailable,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct AttackSourceKey {
    pub incarnation: [u8; 16],
    pub generation: u64,
    pub sample_rate: u32,
    pub channels: u8,
    pub odf_hash: [u8; 32],
}

impl AttackSourceKey {
    pub fn matches(&self, event: &AttackEvent) -> bool {
        self.generation == event.generation
            && self.sample_rate == event.sample_rate
            && self.channels == event.channels
            && self.odf_hash == event.definition_hash
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct AttackSourceEvidence {
    pub source: AttackSourceKey,
    pub odf_support_start: i64,
    pub odf_support_end: i64,
    pub pcm_start: i64,
    pub pcm_end: i64,
    pub cutoff: i64,
    pub band_semantic_hash: [u8; 32],
    /// Exact clock basis (low byte) and the announced output latency. A policy transition
    /// or forced measurement epoch increments SourceKey.generation even at the same sample.
    pub clock_policy: u64,
}

impl AttackSourceEvidence {
    pub fn valid(&self) -> bool {
        self.source.incarnation != [0; 16]
            && self.source.generation > 0
            && self.source.sample_rate > 0
            && matches!(self.source.channels, 1 | 2)
            && self.odf_support_start < self.odf_support_end
            && self.pcm_start < self.pcm_end
            && self.pcm_end <= self.cutoff
            && self.band_semantic_hash != [0; 32]
    }
    pub(super) fn from_frames(
        incarnation: [u8; 16],
        first: &AttackOdfFrame,
        last: &AttackOdfFrame,
        pcm_start: i64,
        pcm_end: i64,
        cutoff: i64,
        clock_policy: u64,
    ) -> Self {
        Self {
            source: AttackSourceKey {
                incarnation,
                generation: last.generation,
                sample_rate: last.sample_rate,
                channels: last.channels,
                odf_hash: last.definition_hash,
            },
            odf_support_start: first.support_start_samples,
            odf_support_end: last.support_end_samples,
            pcm_start,
            pcm_end,
            cutoff,
            band_semantic_hash: super::semantics::band_semantic_hash(),
            clock_policy,
        }
    }
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
#[repr(u8)]
pub enum AttackFinish {
    #[default]
    Acquiring = 0,
    Full = 1,
    AudioEnd = 2,
    NotKept = 3,
    Retired = 4,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct AttackBandObservation {
    pub event: AttackEvent,
    pub requested_end: i64,
    pub actual_end: i64,
    pub finish: AttackFinish,
    pub revision: u64,
    pub measure: Option<AttackBandMeasure>,
    pub head_valid: [u8; ATTACK_BAND_HEAD_POINTS],
    pub tail_valid: [u8; ATTACK_BAND_TAIL_POINTS],
}

impl AttackBandObservation {
    pub fn pending(event: AttackEvent, requested_end: i64, revision: u64) -> Self {
        Self {
            event,
            requested_end,
            actual_end: event.event_sample,
            finish: AttackFinish::Acquiring,
            revision,
            measure: None,
            head_valid: [0; ATTACK_BAND_HEAD_POINTS],
            tail_valid: [0; ATTACK_BAND_TAIL_POINTS],
        }
    }
    pub fn measured(
        event: AttackEvent,
        requested_end: i64,
        revision: u64,
        measure: Option<AttackBandMeasure>,
    ) -> Self {
        let mut result = Self::pending(event, requested_end, revision);
        result.finish = measure.map_or(AttackFinish::NotKept, |value| {
            if value.span_end == BandSpanEnd::AudioEnd {
                AttackFinish::AudioEnd
            } else {
                AttackFinish::Full
            }
        });
        result.actual_end = measure.map_or(event.event_sample, |value| value.span_end_sample);
        result.measure = measure;
        if let Some(value) = measure {
            let rate = event.sample_rate;
            let lead = frames_for_micros(rate, ATTACK_BAND_HEAD_LEAD_MICROS);
            let head = frames_for_micros(rate, ATTACK_BAND_HEAD_SPAN_MICROS);
            let tail = frames_for_micros(rate, ATTACK_BAND_TAIL_MICROS);
            for (index, valid) in result.head_valid.iter_mut().enumerate() {
                let sample = event.event_sample - lead
                    + head * (2 * index as i64 + 1) / (2 * ATTACK_BAND_HEAD_POINTS as i64);
                *valid = u8::from(sample < value.span_end_sample);
            }
            for (index, valid) in result.tail_valid.iter_mut().enumerate() {
                let sample = event.event_sample
                    + tail * (2 * index as i64 + 1) / (2 * ATTACK_BAND_TAIL_POINTS as i64);
                *valid = u8::from(sample < value.span_end_sample);
            }
        }
        result
    }
    pub fn valid(&self) -> bool {
        self.event.has_valid_layout()
            && self.requested_end > self.event.event_sample
            && self.actual_end >= self.event.event_sample
            && self.actual_end <= self.requested_end
            && self.measure.is_none_or(|value| {
                value.has_valid_layout()
                    && value.event_sample == self.event.event_sample
                    && value.span_end_sample == self.actual_end
            })
            && self
                .head_valid
                .iter()
                .chain(self.tail_valid.iter())
                .all(|v| *v <= 1)
            && (self.measure.is_some()
                == matches!(self.finish, AttackFinish::Full | AttackFinish::AudioEnd))
    }
}

/// A logical request can end after its PRE audio stopped. The POST reads only the actual
/// measured PRE window, while retaining the original requested end and AudioEnd reason.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct AttackObservationAnchor {
    pub event: AttackEvent,
    pub requested_end: i64,
    pub measurement_end: i64,
    pub span_end: crate::attack_perception::band::BandSpanEnd,
}

#[derive(Clone, Debug, Default, PartialEq)]
pub struct AttackObservationSnapshot {
    pub source: Option<AttackSourceEvidence>,
    pub band: Option<AttackBand>,
    pub revision: u64,
    pub own: Vec<AttackBandObservation>,
    pub anchored: Vec<AttackBandObservation>,
}

impl AttackObservationSnapshot {
    pub fn own_at(&self, onset: i64) -> Option<&AttackBandObservation> {
        self.own
            .iter()
            .find(|entry| entry.event.event_sample == onset)
    }
    pub fn anchored_at(&self, onset: i64, requested_end: i64) -> Option<&AttackBandObservation> {
        self.anchored
            .iter()
            .find(|entry| entry.event.event_sample == onset && entry.requested_end == requested_end)
    }
}
