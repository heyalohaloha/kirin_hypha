use sha2::{Digest, Sha256};

use crate::drum_excerpt::EXCERPT_SAMPLE_RATE;
use crate::drum_midi::{CompoundEvent, DrumNote, ParsedDrumMidi};

pub(crate) const SOURCE_NOTES_DOMAIN: &str = "attack-drum-midi-source-notes-v1";
pub(crate) const SOURCE_EVENTS_DOMAIN: &str = "attack-drum-midi-source-compounds-v1";
pub(crate) const EXCERPT_NOTES_DOMAIN: &str = "attack-drum-midi-excerpt-notes-v1";
pub(crate) const EXCERPT_EVENTS_DOMAIN: &str = "attack-drum-midi-excerpt-compounds-v1";

#[derive(Clone, Debug, Eq, PartialEq)]
pub(crate) struct CanonicalDigests {
    pub(crate) source_notes_sha256: String,
    pub(crate) source_events_sha256: String,
    pub(crate) excerpt_notes_sha256: String,
    pub(crate) excerpt_events_sha256: String,
}

pub(crate) fn digest_contract(
    source: &ParsedDrumMidi,
    excerpt: &ParsedDrumMidi,
    excerpt_start_sample: u64,
    excerpt_end_sample: u64,
) -> Result<CanonicalDigests, String> {
    if excerpt_start_sample >= excerpt_end_sample {
        return Err("canonical digest received an invalid excerpt range".to_string());
    }
    Ok(CanonicalDigests {
        source_notes_sha256: digest_notes(
            SOURCE_NOTES_DOMAIN,
            &source.notes,
            TimeOrigin::AbsoluteMicros,
            None,
        )?,
        source_events_sha256: digest_events(
            SOURCE_EVENTS_DOMAIN,
            &source.events,
            TimeOrigin::AbsoluteMicros,
            None,
        )?,
        excerpt_notes_sha256: digest_notes(
            EXCERPT_NOTES_DOMAIN,
            &excerpt.notes,
            TimeOrigin::ExcerptSample(excerpt_start_sample),
            Some(excerpt_end_sample - excerpt_start_sample),
        )?,
        excerpt_events_sha256: digest_events(
            EXCERPT_EVENTS_DOMAIN,
            &excerpt.events,
            TimeOrigin::ExcerptSample(excerpt_start_sample),
            Some(excerpt_end_sample - excerpt_start_sample),
        )?,
    })
}

#[derive(Clone, Copy)]
enum TimeOrigin {
    AbsoluteMicros,
    ExcerptSample(u64),
}

fn digest_notes(
    domain: &str,
    notes: &[DrumNote],
    origin: TimeOrigin,
    excerpt_length_samples: Option<u64>,
) -> Result<String, String> {
    let mut writer = CanonicalWriter::new(domain);
    writer.optional_u64(excerpt_length_samples);
    writer.usize(notes.len())?;
    for note in notes {
        writer.u128(time_units(note.time_micros, origin)?);
        writer.byte(note.pitch);
        writer.byte(note.velocity);
    }
    Ok(writer.finish())
}

fn digest_events(
    domain: &str,
    events: &[CompoundEvent],
    origin: TimeOrigin,
    excerpt_length_samples: Option<u64>,
) -> Result<String, String> {
    let mut writer = CanonicalWriter::new(domain);
    writer.optional_u64(excerpt_length_samples);
    writer.usize(events.len())?;
    for event in events {
        writer.u128(time_units(event.time_micros, origin)?);
        writer.byte(u8::from(event.kick_only));
        writer.byte(u8::from(event.hat_only));
        writer.usize(event.note_start)?;
        writer.usize(event.note_end)?;
    }
    Ok(writer.finish())
}

fn time_units(time_micros: u64, origin: TimeOrigin) -> Result<u128, String> {
    match origin {
        TimeOrigin::AbsoluteMicros => Ok(u128::from(time_micros)),
        TimeOrigin::ExcerptSample(start_sample) => u128::from(time_micros)
            .checked_mul(u128::from(EXCERPT_SAMPLE_RATE))
            .and_then(|scaled| scaled.checked_sub(u128::from(start_sample).checked_mul(1_000_000)?))
            .ok_or_else(|| "excerpt event precedes the canonical sample origin".to_string()),
    }
}

struct CanonicalWriter(Sha256);

impl CanonicalWriter {
    fn new(domain: &str) -> Self {
        let mut hasher = Sha256::new();
        hasher.update((domain.len() as u64).to_be_bytes());
        hasher.update(domain.as_bytes());
        Self(hasher)
    }

    fn optional_u64(&mut self, value: Option<u64>) {
        self.byte(u8::from(value.is_some()));
        if let Some(value) = value {
            self.0.update(value.to_be_bytes());
        }
    }

    fn usize(&mut self, value: usize) -> Result<(), String> {
        let value = u64::try_from(value).map_err(|_| "canonical count exceeds u64".to_string())?;
        self.0.update(value.to_be_bytes());
        Ok(())
    }

    fn u128(&mut self, value: u128) {
        self.0.update(value.to_be_bytes());
    }

    fn byte(&mut self, value: u8) {
        self.0.update([value]);
    }

    fn finish(self) -> String {
        hex::encode(self.0.finalize())
    }
}

#[cfg(test)]
#[path = "canonical_tests.rs"]
mod tests;
