use std::fs;
use std::io::ErrorKind;
use std::path::{Path, PathBuf};

const LABEL_COALESCE_SECS: f64 = 0.008;
const KICK_NOTES: [u8; 2] = [35, 36];
const HAT_NOTES: [u8; 3] = [42, 44, 46];
const PINNED_SELECTION: &str = include_str!("fixtures/transient_egmd_selection_v1.csv");

#[derive(Clone)]
pub(crate) struct Selection {
    pub(crate) split: String,
    pub(crate) midi: PathBuf,
    pub(crate) audio: PathBuf,
}

#[derive(Clone, Copy)]
pub(crate) struct LabelEvent {
    pub(crate) time_secs: f64,
    pub(crate) kick: bool,
    pub(crate) hat: bool,
}

pub(crate) fn read_selection(root: &Path) -> Result<Vec<Selection>, String> {
    let text = match fs::read_to_string(root.join("selection.csv")) {
        Ok(text) => text,
        Err(error) if error.kind() == ErrorKind::NotFound => PINNED_SELECTION.to_string(),
        Err(error) => return Err(error.to_string()),
    };
    let mut lines = text.lines();
    let header = lines.next().ok_or("empty selection.csv")?;
    if header != "drummer,session,id,style,bpm,beat_type,time_signature,duration,split,midi_filename,audio_filename,kit_name" {
        return Err("unexpected selection.csv header".to_string());
    }
    lines
        .filter(|line| !line.is_empty())
        .map(|line| {
            let fields = line.split(',').collect::<Vec<_>>();
            if fields.len() != 12 || !matches!(fields[8], "validation" | "test") {
                return Err(format!("invalid selection row: {line}"));
            }
            Ok(Selection {
                split: fields[8].to_string(),
                midi: root.join(fields[9]),
                audio: root.join(fields[10]),
            })
        })
        .collect()
}

pub(crate) fn read_pcm16_mono_wav(path: &Path) -> Result<(u32, Vec<f32>), String> {
    let bytes = fs::read(path).map_err(|error| error.to_string())?;
    if bytes.get(..4) != Some(b"RIFF") || bytes.get(8..12) != Some(b"WAVE") {
        return Err(format!("not a RIFF WAVE: {}", path.display()));
    }
    let mut offset = 12;
    let mut format = None;
    let mut data = None;
    while offset + 8 <= bytes.len() {
        let id = &bytes[offset..offset + 4];
        let len = u32::from_le_bytes(bytes[offset + 4..offset + 8].try_into().unwrap()) as usize;
        let start = offset + 8;
        let end = start.checked_add(len).ok_or("WAV chunk overflow")?;
        if end > bytes.len() {
            return Err("truncated WAV chunk".to_string());
        }
        if id == b"fmt " {
            format = Some(&bytes[start..end]);
        } else if id == b"data" {
            data = Some(&bytes[start..end]);
        }
        offset = end
            .checked_add(len & 1)
            .ok_or("WAV chunk padding overflow")?;
    }
    let format = format.ok_or("missing WAV fmt")?;
    let data = data.ok_or("missing WAV data")?;
    if format.len() < 16 {
        return Err("truncated WAV fmt".to_string());
    }
    let code = u16::from_le_bytes(format[0..2].try_into().unwrap());
    let channels = u16::from_le_bytes(format[2..4].try_into().unwrap());
    let sample_rate = u32::from_le_bytes(format[4..8].try_into().unwrap());
    let bits = u16::from_le_bytes(format[14..16].try_into().unwrap());
    if code != 1 || channels != 1 || bits != 16 || data.len() % 2 != 0 {
        return Err(format!(
            "unsupported E-GMD WAV: code={code} ch={channels} bits={bits}"
        ));
    }
    let samples = data
        .chunks_exact(2)
        .map(|sample| i16::from_le_bytes([sample[0], sample[1]]) as f32 / 32_768.0)
        .collect();
    Ok((sample_rate, samples))
}

pub(crate) fn read_midi_labels(path: &Path) -> Result<Vec<LabelEvent>, String> {
    let bytes = fs::read(path).map_err(|error| error.to_string())?;
    if bytes.get(..4) != Some(b"MThd") || bytes.len() < 14 {
        return Err(format!("invalid MIDI: {}", path.display()));
    }
    let division = u16::from_be_bytes([bytes[12], bytes[13]]);
    if division & 0x8000 != 0 || division == 0 {
        return Err("SMPTE MIDI division is unsupported".to_string());
    }
    let tracks = u16::from_be_bytes([bytes[10], bytes[11]]) as usize;
    let mut offset = 14_usize;
    let mut tempos = vec![(0_u64, 500_000_u32)];
    let mut notes = Vec::new();
    for _ in 0..tracks {
        let header = bytes
            .get(offset..offset.saturating_add(8))
            .ok_or("truncated MIDI track header")?;
        if header.get(..4) != Some(b"MTrk") {
            return Err("missing MIDI track".to_string());
        }
        let len = u32::from_be_bytes(header[4..8].try_into().unwrap()) as usize;
        let start = offset.checked_add(8).ok_or("MIDI track offset overflow")?;
        let end = start.checked_add(len).ok_or("MIDI track length overflow")?;
        let track = bytes.get(start..end).ok_or("truncated MIDI track")?;
        parse_midi_track(track, &mut tempos, &mut notes)?;
        offset = end;
    }
    tempos.sort_unstable_by_key(|event| event.0);
    notes.sort_unstable_by_key(|event| event.0);
    let mut labels: Vec<LabelEvent> = Vec::new();
    for (tick, note) in notes {
        let time_secs = tick_to_seconds(tick, division, &tempos);
        if let Some(previous) = labels.last_mut() {
            if time_secs - previous.time_secs <= LABEL_COALESCE_SECS {
                previous.kick |= KICK_NOTES.contains(&note);
                previous.hat |= HAT_NOTES.contains(&note);
                continue;
            }
        }
        labels.push(LabelEvent {
            time_secs,
            kick: KICK_NOTES.contains(&note),
            hat: HAT_NOTES.contains(&note),
        });
    }
    Ok(labels)
}

fn parse_midi_track(
    track: &[u8],
    tempos: &mut Vec<(u64, u32)>,
    notes: &mut Vec<(u64, u8)>,
) -> Result<(), String> {
    let mut offset = 0;
    let mut tick = 0_u64;
    let mut running = None;
    while offset < track.len() {
        tick += read_variable(track, &mut offset)? as u64;
        let first = *track.get(offset).ok_or("truncated MIDI event")?;
        let status = if first & 0x80 != 0 {
            offset += 1;
            running = (first < 0xf0).then_some(first);
            first
        } else {
            running.ok_or("missing MIDI running status")?
        };
        if status == 0xff {
            let kind = *track.get(offset).ok_or("truncated MIDI meta")?;
            offset += 1;
            let len = read_variable(track, &mut offset)? as usize;
            let payload = track
                .get(offset..offset + len)
                .ok_or("truncated MIDI meta payload")?;
            if kind == 0x51 && len == 3 {
                tempos.push((
                    tick,
                    u32::from_be_bytes([0, payload[0], payload[1], payload[2]]),
                ));
            }
            offset += len;
        } else if status == 0xf0 || status == 0xf7 {
            let len = read_variable(track, &mut offset)? as usize;
            offset = offset.checked_add(len).ok_or("MIDI sysex overflow")?;
            if offset > track.len() {
                return Err("truncated MIDI sysex".to_string());
            }
        } else {
            let kind = status & 0xf0;
            let data_len = if matches!(kind, 0xc0 | 0xd0) { 1 } else { 2 };
            let payload = track
                .get(offset..offset + data_len)
                .ok_or("truncated MIDI channel event")?;
            if kind == 0x90 && payload[1] != 0 {
                notes.push((tick, payload[0]));
            }
            offset += data_len;
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

fn tick_to_seconds(tick: u64, division: u16, tempos: &[(u64, u32)]) -> f64 {
    let mut seconds = 0.0;
    let mut cursor = 0_u64;
    let mut tempo = 500_000_u32;
    for &(change_tick, next_tempo) in tempos {
        if change_tick > tick {
            break;
        }
        seconds += (change_tick - cursor) as f64 * tempo as f64 / (division as f64 * 1_000_000.0);
        cursor = change_tick;
        tempo = next_tempo;
    }
    seconds + (tick - cursor) as f64 * tempo as f64 / (division as f64 * 1_000_000.0)
}
