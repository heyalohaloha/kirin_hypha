use std::fs;
use std::path::Path;

use serde::Serialize;

use crate::contract::sha256_bytes;
use crate::drum_midi::{excerpt_drum_midi, parse_drum_midi};

#[derive(Clone, Debug, Serialize)]
pub(crate) struct MidiSummary {
    pub(crate) sha256: String,
    pub(crate) raw_notes: usize,
    pub(crate) compound_events: usize,
    pub(crate) kick_only_events: usize,
    pub(crate) hat_only_events: usize,
    pub(crate) first_event_time_secs: f64,
    pub(crate) last_event_time_secs: f64,
    pub(crate) source_raw_notes: usize,
    pub(crate) source_compound_events: usize,
    pub(crate) source_kick_only_events: usize,
    pub(crate) source_hat_only_events: usize,
    pub(crate) source_first_raw_note_time_secs: f64,
    pub(crate) source_last_raw_note_time_secs: f64,
}

pub(crate) fn inspect_midi(
    path: &Path,
    start_sample: u64,
    end_sample: u64,
    sample_rate: u32,
) -> Result<MidiSummary, String> {
    let bytes = fs::read(path)
        .map_err(|error| format!("cannot read eligible MIDI {}: {error}", path.display()))?;
    inspect_midi_bytes(&bytes, start_sample, end_sample, sample_rate)
        .map_err(|error| format!("{}: {error}", path.display()))
}

fn inspect_midi_bytes(
    bytes: &[u8],
    start_sample: u64,
    end_sample: u64,
    sample_rate: u32,
) -> Result<MidiSummary, String> {
    let source = parse_drum_midi(bytes)?;
    let parsed = excerpt_drum_midi(&source, start_sample, end_sample, sample_rate)?;
    let first_event_time_secs = parsed
        .events
        .first()
        .map_or(0.0, |event| event.time_micros as f64 / 1_000_000.0);
    let last_event_time_secs = parsed
        .events
        .last()
        .map_or(0.0, |event| event.time_micros as f64 / 1_000_000.0);
    Ok(MidiSummary {
        sha256: sha256_bytes(bytes),
        raw_notes: parsed.raw_notes,
        compound_events: parsed.events.len(),
        kick_only_events: parsed.events.iter().filter(|event| event.kick_only).count(),
        hat_only_events: parsed.events.iter().filter(|event| event.hat_only).count(),
        first_event_time_secs,
        last_event_time_secs,
        source_raw_notes: source.raw_notes,
        source_compound_events: source.events.len(),
        source_kick_only_events: source.events.iter().filter(|event| event.kick_only).count(),
        source_hat_only_events: source.events.iter().filter(|event| event.hat_only).count(),
        source_first_raw_note_time_secs: source
            .notes
            .first()
            .map_or(0.0, |note| note.time_micros as f64 / 1_000_000.0),
        source_last_raw_note_time_secs: source
            .notes
            .last()
            .map_or(0.0, |note| note.time_micros as f64 / 1_000_000.0),
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn thirty_milliseconds_is_one_compound_event() {
        for (gap, expected) in [(29_u8, 1), (30, 1), (31, 2)] {
            let mut track = vec![0, 0xff, 0x51, 3, 0x0f, 0x42, 0x40, 0, 0x99, 36, 100];
            track.extend_from_slice(&[gap, 0x99, 42, 80, 0, 0xff, 0x2f, 0]);
            let summary =
                inspect_midi_bytes(&midi_file(1_000, &track), 0, u64::MAX, 1_000_000).unwrap();
            assert_eq!(summary.compound_events, expected, "gap={gap}");
        }
    }

    #[test]
    fn kick_only_and_hat_only_are_not_double_counted() {
        let track = [
            0, 0x99, 36, 100, 80, 0x99, 42, 100, 80, 0x99, 36, 100, 0, 0x99, 44, 100, 80, 0x99, 36,
            100, 0, 0x99, 38, 100, 80, 0x99, 42, 100, 0, 0x99, 38, 100, 0, 0xff, 0x2f, 0,
        ];
        let summary =
            inspect_midi_bytes(&midi_file(1_000, &track), 0, u64::MAX, 1_000_000).unwrap();
        assert_eq!(summary.compound_events, 5);
        assert_eq!(summary.kick_only_events, 1);
        assert_eq!(summary.hat_only_events, 1);
    }

    #[test]
    fn malformed_midi_fails_closed() {
        assert!(inspect_midi_bytes(b"not midi", 0, 1, 1_000_000).is_err());
        let track = [0, 0x90, 36];
        assert!(inspect_midi_bytes(&midi_file(480, &track), 0, 1, 1_000_000).is_err());
    }

    fn midi_file(division: u16, track: &[u8]) -> Vec<u8> {
        let mut bytes = b"MThd\0\0\0\x06\0\0\0\x01".to_vec();
        bytes.extend_from_slice(&division.to_be_bytes());
        bytes.extend_from_slice(b"MTrk");
        bytes.extend_from_slice(&(track.len() as u32).to_be_bytes());
        bytes.extend_from_slice(track);
        bytes
    }
}
