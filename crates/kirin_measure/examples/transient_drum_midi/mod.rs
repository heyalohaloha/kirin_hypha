//! Shared integer-time MIDI contract for ATTACK DRUM research tools.

pub(crate) const COMPOUND_SPAN_MICROS: u64 = 30_000;

const DEFAULT_TEMPO_MICROS_PER_QUARTER: u32 = 500_000;
const OFFICIAL_HAT_NOTES: [u8; 5] = [22, 26, 42, 44, 46];

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub(crate) struct CompoundEvent {
    pub(crate) time_micros: u64,
    pub(crate) kick_only: bool,
    pub(crate) hat_only: bool,
    pub(crate) note_start: usize,
    pub(crate) note_end: usize,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub(crate) struct DrumNote {
    pub(crate) time_micros: u64,
    pub(crate) pitch: u8,
    pub(crate) velocity: u8,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub(crate) struct ParsedDrumMidi {
    pub(crate) raw_notes: usize,
    pub(crate) notes: Vec<DrumNote>,
    pub(crate) events: Vec<CompoundEvent>,
}

#[derive(Clone, Copy, Debug, Eq, Ord, PartialEq, PartialOrd)]
struct TickNote {
    tick: u64,
    pitch: u8,
    velocity: u8,
}

pub(crate) fn parse_drum_midi(bytes: &[u8]) -> Result<ParsedDrumMidi, String> {
    if bytes.get(..4) != Some(b"MThd") || bytes.len() < 14 {
        return Err("invalid MIDI header".to_string());
    }
    let header_len = u32::from_be_bytes(bytes[4..8].try_into().unwrap()) as usize;
    let header_end = 8_usize
        .checked_add(header_len)
        .ok_or("MIDI header overflow")?;
    if header_len < 6 || header_end > bytes.len() {
        return Err("truncated MIDI header".to_string());
    }
    let format = u16::from_be_bytes([bytes[8], bytes[9]]);
    let track_count = u16::from_be_bytes([bytes[10], bytes[11]]) as usize;
    let division = u16::from_be_bytes([bytes[12], bytes[13]]);
    if format > 1
        || track_count == 0
        || (format == 0 && track_count != 1)
        || division == 0
        || division & 0x8000 != 0
    {
        return Err("unsupported MIDI header".to_string());
    }

    let mut offset = header_end;
    let mut tempos = Vec::<(u64, u32)>::new();
    let mut notes = Vec::<TickNote>::new();
    for _ in 0..track_count {
        let header = bytes
            .get(offset..offset + 8)
            .ok_or("truncated MIDI track header")?;
        if header.get(..4) != Some(b"MTrk") {
            return Err("missing MIDI track".to_string());
        }
        let length = u32::from_be_bytes(header[4..8].try_into().unwrap()) as usize;
        let start = offset.checked_add(8).ok_or("MIDI track offset overflow")?;
        let end = start
            .checked_add(length)
            .ok_or("MIDI track length overflow")?;
        parse_track(
            bytes.get(start..end).ok_or("truncated MIDI track")?,
            &mut tempos,
            &mut notes,
        )?;
        offset = end;
    }
    if offset != bytes.len() {
        return Err("trailing bytes after declared MIDI tracks".to_string());
    }

    tempos.sort_unstable();
    if tempos
        .windows(2)
        .any(|pair| pair[0].0 == pair[1].0 && pair[0].1 != pair[1].1)
    {
        return Err("conflicting MIDI tempos at the same tick".to_string());
    }
    tempos.dedup();
    notes.sort_unstable();
    if notes.is_empty() {
        return Err("MIDI contains no nonzero-velocity note-on events".to_string());
    }

    let timed_notes = notes
        .into_iter()
        .map(|note| {
            Ok(DrumNote {
                time_micros: tick_to_micros(note.tick, division, &tempos)?,
                pitch: note.pitch,
                velocity: note.velocity,
            })
        })
        .collect::<Result<Vec<_>, String>>()?;
    let raw_notes = timed_notes.len();
    Ok(ParsedDrumMidi {
        raw_notes,
        events: compound(&timed_notes)?,
        notes: timed_notes,
    })
}

/// Returns the raw note-on events in the exact half-open sample interval and
/// rebuilds compounds from that excerpt. Absolute integer-microsecond times
/// are retained so labels and audio share the original content clock.
#[allow(dead_code)] // Some tools import the shared parser but do not excerpt labels.
pub(crate) fn excerpt_drum_midi(
    parsed: &ParsedDrumMidi,
    start_sample: u64,
    end_sample: u64,
    sample_rate: u32,
) -> Result<ParsedDrumMidi, String> {
    if sample_rate == 0 || start_sample >= end_sample {
        return Err("invalid MIDI excerpt sample bounds".to_string());
    }
    let rate = u128::from(sample_rate);
    let start_scaled = u128::from(start_sample) * 1_000_000;
    let end_scaled = u128::from(end_sample) * 1_000_000;
    let notes = parsed
        .notes
        .iter()
        .filter_map(|note| {
            let note_scaled = u128::from(note.time_micros) * rate;
            (start_scaled <= note_scaled && note_scaled < end_scaled).then_some(*note)
        })
        .collect::<Vec<_>>();
    Ok(ParsedDrumMidi {
        raw_notes: notes.len(),
        events: compound(&notes)?,
        notes,
    })
}

fn parse_track(
    track: &[u8],
    tempos: &mut Vec<(u64, u32)>,
    notes: &mut Vec<TickNote>,
) -> Result<(), String> {
    let (mut offset, mut tick, mut running) = (0_usize, 0_u64, None);
    while offset < track.len() {
        tick = tick
            .checked_add(u64::from(read_variable(track, &mut offset)?))
            .ok_or("MIDI tick overflow")?;
        let first = *track.get(offset).ok_or("truncated MIDI event")?;
        let status = if first & 0x80 != 0 {
            offset += 1;
            if first < 0xf0 {
                running = Some(first);
            }
            first
        } else {
            running.ok_or("missing MIDI running status")?
        };
        if status == 0xff {
            let kind = *track.get(offset).ok_or("truncated MIDI meta event")?;
            offset += 1;
            let length = read_variable(track, &mut offset)? as usize;
            let end = offset.checked_add(length).ok_or("MIDI meta overflow")?;
            let payload = track
                .get(offset..end)
                .ok_or("truncated MIDI meta payload")?;
            if kind == 0x51 && length == 3 {
                let tempo = u32::from_be_bytes([0, payload[0], payload[1], payload[2]]);
                if tempo == 0 {
                    return Err("zero MIDI tempo".to_string());
                }
                tempos.push((tick, tempo));
            }
            offset = end;
        } else if matches!(status, 0xf0 | 0xf7) {
            let length = read_variable(track, &mut offset)? as usize;
            offset = offset.checked_add(length).ok_or("MIDI sysex overflow")?;
            if offset > track.len() {
                return Err("truncated MIDI sysex".to_string());
            }
        } else if status < 0xf0 {
            let kind = status & 0xf0;
            let length = if matches!(kind, 0xc0 | 0xd0) { 1 } else { 2 };
            let payload = track
                .get(offset..offset + length)
                .ok_or("truncated MIDI channel event")?;
            if payload.iter().any(|byte| byte & 0x80 != 0) {
                return Err("invalid MIDI channel data".to_string());
            }
            if kind == 0x90 && payload[1] != 0 {
                notes.push(TickNote {
                    tick,
                    pitch: payload[0],
                    velocity: payload[1],
                });
            }
            offset += length;
        } else {
            return Err(format!("unsupported MIDI status: {status:#x}"));
        }
    }
    Ok(())
}

fn read_variable(bytes: &[u8], offset: &mut usize) -> Result<u32, String> {
    let mut value = 0_u32;
    for _ in 0..4 {
        let byte = *bytes
            .get(*offset)
            .ok_or("truncated MIDI variable integer")?;
        *offset += 1;
        value = (value << 7) | u32::from(byte & 0x7f);
        if byte & 0x80 == 0 {
            return Ok(value);
        }
    }
    Err("oversized MIDI variable integer".to_string())
}

fn tick_to_micros(tick: u64, division: u16, tempos: &[(u64, u32)]) -> Result<u64, String> {
    let (mut numerator, mut cursor, mut tempo) = (0_u128, 0_u64, DEFAULT_TEMPO_MICROS_PER_QUARTER);
    for &(change_tick, next_tempo) in tempos {
        if change_tick > tick {
            break;
        }
        numerator = numerator
            .checked_add(u128::from(change_tick - cursor) * u128::from(tempo))
            .ok_or("MIDI time overflow")?;
        cursor = change_tick;
        tempo = next_tempo;
    }
    numerator = numerator
        .checked_add(u128::from(tick - cursor) * u128::from(tempo))
        .ok_or("MIDI time overflow")?;
    let divisor = u128::from(division);
    let rounded = numerator
        .checked_add(divisor / 2)
        .ok_or("MIDI time overflow")?
        / divisor;
    u64::try_from(rounded).map_err(|_| "MIDI time exceeds u64 microseconds".to_string())
}

fn compound(notes: &[DrumNote]) -> Result<Vec<CompoundEvent>, String> {
    let mut events = Vec::new();
    let mut start = 0;
    while start < notes.len() {
        let mut end = start + 1;
        while end < notes.len()
            && notes[end].time_micros - notes[start].time_micros <= COMPOUND_SPAN_MICROS
        {
            end += 1;
        }
        let cluster = &notes[start..end];
        let sum = cluster.iter().try_fold(0_u128, |sum, note| {
            sum.checked_add(u128::from(note.time_micros))
        });
        let sum = sum.ok_or("compound event time overflow")?;
        let count = cluster.len() as u128;
        let time_micros = u64::try_from((sum + count / 2) / count)
            .map_err(|_| "compound event time exceeds u64".to_string())?;
        events.push(CompoundEvent {
            time_micros,
            kick_only: cluster.iter().all(|note| note.pitch == 36),
            hat_only: cluster
                .iter()
                .all(|note| OFFICIAL_HAT_NOTES.contains(&note.pitch)),
            note_start: start,
            note_end: end,
        });
        start = end;
    }
    Ok(events)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn rounded_note_times_define_the_inclusive_compound_boundary() {
        let inside = midi_file(2, &tempo_and_notes(59_999, &[(0, 36, 100), (1, 42, 100)]));
        let outside = midi_file(2, &tempo_and_notes(60_001, &[(0, 36, 100), (1, 42, 100)]));
        assert_eq!(parse_drum_midi(&inside).unwrap().events.len(), 1);
        assert_eq!(
            parse_drum_midi(&inside).unwrap().events[0].time_micros,
            15_000
        );
        assert_eq!(parse_drum_midi(&outside).unwrap().events.len(), 2);
    }

    #[test]
    fn compounds_from_the_first_tap_without_chaining() {
        let bytes = midi_file(
            1_000,
            &tempo_and_notes(1_000_000, &[(0, 36, 100), (20, 42, 100), (20, 44, 100)]),
        );
        let parsed = parse_drum_midi(&bytes).unwrap();
        assert_eq!(
            parsed
                .events
                .iter()
                .map(|event| event.time_micros)
                .collect::<Vec<_>>(),
            [10_000, 40_000]
        );
    }

    #[test]
    fn compound_mean_rounds_half_up_in_integer_microseconds() {
        let bytes = midi_file(1, &tempo_and_notes(1, &[(0, 36, 100), (1, 36, 100)]));
        let parsed = parse_drum_midi(&bytes).unwrap();
        assert_eq!(parsed.events.len(), 1);
        assert_eq!(parsed.events[0].time_micros, 1);
        assert!(parsed.events[0].kick_only);
    }

    #[test]
    fn tempo_changes_are_integrated_before_one_rounding_step() {
        let track = [
            0, 0x99, 36, 100, 2, 0xff, 0x51, 3, 0x03, 0xd0, 0x90, 2, 0x99, 42, 100, 0, 0xff, 0x2f,
            0,
        ];
        let parsed = parse_drum_midi(&midi_file(10, &track)).unwrap();
        assert_eq!(parsed.events[0].time_micros, 0);
        assert_eq!(parsed.events[1].time_micros, 150_000);
    }

    #[test]
    fn only_nonzero_note_ons_count_and_official_hats_are_exact() {
        let bytes = midi_file(
            1_000,
            &tempo_and_notes(
                1_000_000,
                &[
                    (0, 22, 100),
                    (0, 26, 1),
                    (40, 42, 0),
                    (40, 44, 100),
                    (0, 46, 100),
                ],
            ),
        );
        let parsed = parse_drum_midi(&bytes).unwrap();
        assert_eq!(parsed.raw_notes, 4);
        assert!(parsed.events.iter().all(|event| event.hat_only));
        assert!(parsed.events.iter().all(|event| !event.kick_only));

        let non_hat = midi_file(1_000, &tempo_and_notes(1_000_000, &[(0, 23, 100)]));
        assert!(!parse_drum_midi(&non_hat).unwrap().events[0].hat_only);
    }

    #[test]
    fn excerpt_uses_exact_half_open_sample_bounds_then_recompounds() {
        let parsed = ParsedDrumMidi {
            raw_notes: 4,
            notes: vec![
                DrumNote {
                    time_micros: 0,
                    pitch: 36,
                    velocity: 100,
                },
                DrumNote {
                    time_micros: 22,
                    pitch: 36,
                    velocity: 100,
                },
                DrumNote {
                    time_micros: 23,
                    pitch: 42,
                    velocity: 100,
                },
                DrumNote {
                    time_micros: 30_000,
                    pitch: 42,
                    velocity: 100,
                },
            ],
            events: Vec::new(),
        };
        // At 44.1 kHz, note 22 us is before sample 1 and note 23 us is after it.
        let excerpt = excerpt_drum_midi(&parsed, 1, 1_324, 44_100).unwrap();
        assert_eq!(excerpt.raw_notes, 2);
        assert_eq!(excerpt.notes[0].pitch, 42);
        assert_eq!(excerpt.notes[0].time_micros, 23);
        assert_eq!(excerpt.events.len(), 1);
        assert_eq!(excerpt.events[0].note_start, 0);
        assert_eq!(excerpt.events[0].note_end, 2);
        assert!(excerpt.events[0].hat_only);
    }

    #[test]
    fn excerpt_excludes_exact_end_accepts_empty_and_rejects_invalid_bounds() {
        let parsed = ParsedDrumMidi {
            raw_notes: 1,
            notes: vec![DrumNote {
                time_micros: 1_000,
                pitch: 36,
                velocity: 100,
            }],
            events: Vec::new(),
        };
        let empty = excerpt_drum_midi(&parsed, 0, 1, 1_000).unwrap();
        assert_eq!(empty.raw_notes, 0);
        assert!(empty.notes.is_empty());
        assert!(empty.events.is_empty());
        assert_eq!(
            excerpt_drum_midi(&parsed, 1, 2, 1_000).unwrap().notes[0].time_micros,
            1_000
        );
        assert!(excerpt_drum_midi(&parsed, 1, 1, 1_000).is_err());
        assert!(excerpt_drum_midi(&parsed, 0, 1, 0).is_err());
    }

    #[test]
    fn malformed_empty_conflicting_tempo_and_trailing_data_fail_closed() {
        assert!(parse_drum_midi(b"not midi").is_err());
        let empty = midi_file(480, &[0, 0x99, 36, 0, 0, 0xff, 0x2f, 0]);
        assert!(parse_drum_midi(&empty).is_err());
        let conflict = midi_file(
            480,
            &[
                0, 0xff, 0x51, 3, 0x07, 0xa1, 0x20, 0, 0xff, 0x51, 3, 0x06, 0x1a, 0x80, 0, 0x99,
                36, 100, 0, 0xff, 0x2f, 0,
            ],
        );
        assert!(parse_drum_midi(&conflict).is_err());
        let mut trailing = midi_file(480, &[0, 0x99, 36, 100, 0, 0xff, 0x2f, 0]);
        trailing.push(0);
        assert!(parse_drum_midi(&trailing).is_err());
    }

    fn tempo_and_notes(tempo: u32, notes: &[(u8, u8, u8)]) -> Vec<u8> {
        let tempo_bytes = tempo.to_be_bytes();
        let mut track = vec![
            0,
            0xff,
            0x51,
            3,
            tempo_bytes[1],
            tempo_bytes[2],
            tempo_bytes[3],
        ];
        for &(gap, pitch, velocity) in notes {
            track.extend_from_slice(&[gap, 0x99, pitch, velocity]);
        }
        track.extend_from_slice(&[0, 0xff, 0x2f, 0]);
        track
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
