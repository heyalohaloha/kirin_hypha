use std::fs;
use std::path::Path;

use serde::Serialize;

use crate::contract::sha256_bytes;
use crate::drum_midi::parse_drum_midi;

#[derive(Clone, Debug, Serialize)]
pub(crate) struct MidiSummary {
    pub(crate) sha256: String,
    pub(crate) raw_notes: usize,
    pub(crate) compound_events: usize,
    pub(crate) kick_only_events: usize,
    pub(crate) hat_only_events: usize,
    pub(crate) first_event_time_secs: f64,
    pub(crate) last_event_time_secs: f64,
}

pub(crate) fn inspect_midi(path: &Path) -> Result<MidiSummary, String> {
    let bytes = fs::read(path)
        .map_err(|error| format!("cannot read eligible MIDI {}: {error}", path.display()))?;
    inspect_midi_bytes(&bytes).map_err(|error| format!("{}: {error}", path.display()))
}

fn inspect_midi_bytes(bytes: &[u8]) -> Result<MidiSummary, String> {
    let parsed = parse_drum_midi(bytes)?;
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
            let summary = inspect_midi_bytes(&midi_file(1_000, &track)).unwrap();
            assert_eq!(summary.compound_events, expected, "gap={gap}");
        }
    }

    #[test]
    fn kick_only_and_hat_only_are_not_double_counted() {
        let track = [
            0, 0x99, 36, 100, 80, 0x99, 42, 100, 80, 0x99, 36, 100, 0, 0x99, 44, 100, 80, 0x99, 36,
            100, 0, 0x99, 38, 100, 80, 0x99, 42, 100, 0, 0x99, 38, 100, 0, 0xff, 0x2f, 0,
        ];
        let summary = inspect_midi_bytes(&midi_file(1_000, &track)).unwrap();
        assert_eq!(summary.compound_events, 5);
        assert_eq!(summary.kick_only_events, 1);
        assert_eq!(summary.hat_only_events, 1);
    }

    #[test]
    fn malformed_midi_fails_closed() {
        assert!(inspect_midi_bytes(b"not midi").is_err());
        let track = [0, 0x90, 36];
        assert!(inspect_midi_bytes(&midi_file(480, &track)).is_err());
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
