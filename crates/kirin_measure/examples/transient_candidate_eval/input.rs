use std::collections::HashSet;
use std::fs;
use std::path::{Component, Path, PathBuf};

use sha2::{Digest, Sha256};

const MANIFEST_HEADER: &str = "drummer,session,id,style,bpm,beat_type,time_signature,duration,split,midi_filename,audio_filename,kit_name";
const LABEL_CLUSTER_SECS: f64 = 0.030;
const HAT_NOTES: [u8; 5] = [22, 26, 42, 44, 46];

#[derive(Clone, Debug)]
pub(crate) struct SelectionManifest {
    pub(crate) path: PathBuf,
    pub(crate) sha256: String,
    pub(crate) entries: Vec<Selection>,
}

#[derive(Clone, Debug)]
pub(crate) struct Selection {
    pub(crate) drummer: String,
    pub(crate) session: String,
    pub(crate) id: String,
    pub(crate) style: String,
    pub(crate) bpm: f64,
    pub(crate) beat_type: String,
    pub(crate) time_signature: String,
    pub(crate) declared_duration: f64,
    pub(crate) split: String,
    pub(crate) midi: PathBuf,
    pub(crate) audio: PathBuf,
    pub(crate) kit_name: String,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub(crate) struct MidiNote {
    pub(crate) time_secs: f64,
    pub(crate) pitch: u8,
    pub(crate) velocity: u8,
}

#[derive(Clone, Debug)]
pub(crate) struct LabelEvent {
    pub(crate) time_secs: f64,
    pub(crate) kick: bool,
    pub(crate) hat: bool,
    pub(crate) pitches: Vec<u8>,
    pub(crate) note_count: usize,
    pub(crate) notes: Vec<MidiNote>,
}

#[derive(Clone, Debug)]
pub(crate) struct MidiLabels {
    pub(crate) events: Vec<LabelEvent>,
    pub(crate) raw_note_count: usize,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub(crate) struct WavMetadata {
    pub(crate) sample_rate: u32,
    pub(crate) channels: u16,
    pub(crate) bits_per_sample: u16,
    pub(crate) sample_count: usize,
    pub(crate) duration_secs: f64,
}

#[derive(Clone, Debug)]
pub(crate) struct MonoWav {
    pub(crate) metadata: WavMetadata,
    pub(crate) samples: Vec<f32>,
}

pub(crate) fn read_selection(
    root: &Path,
    explicit_manifest_path: &Path,
) -> Result<SelectionManifest, String> {
    let root = fs::canonicalize(root).map_err(|error| format!("dataset root: {error}"))?;
    if !root.is_dir() {
        return Err(format!(
            "dataset root is not a directory: {}",
            root.display()
        ));
    }
    let bytes = fs::read(explicit_manifest_path)
        .map_err(|error| format!("manifest {}: {error}", explicit_manifest_path.display()))?;
    let path = fs::canonicalize(explicit_manifest_path)
        .map_err(|error| format!("manifest path: {error}"))?;
    let text = std::str::from_utf8(&bytes).map_err(|error| format!("manifest UTF-8: {error}"))?;
    let mut lines = text.lines();
    let header = lines.next().ok_or("empty manifest")?.trim_end_matches('\r');
    if header != MANIFEST_HEADER {
        return Err("unexpected manifest header".to_string());
    }
    let mut row_keys = HashSet::new();
    let mut input_paths = HashSet::new();
    let mut entries = Vec::new();
    for (index, raw_line) in lines.enumerate() {
        let line = raw_line.trim_end_matches('\r');
        if line.is_empty() {
            continue;
        }
        let fields = parse_csv_line(line).map_err(|error| format!("row {}: {error}", index + 2))?;
        if fields.len() != 12 || fields.iter().any(String::is_empty) {
            return Err(format!("invalid manifest row {}", index + 2));
        }
        if !matches!(fields[8].as_str(), "train" | "validation" | "test") {
            return Err(format!("invalid split at row {}: {}", index + 2, fields[8]));
        }
        let bpm = positive_finite(&fields[4], "bpm", index + 2)?;
        let declared_duration = positive_finite(&fields[7], "duration", index + 2)?;
        let row_key = (fields[2].clone(), fields[11].clone());
        if !row_keys.insert(row_key) {
            return Err(format!("duplicate id + kit_name at row {}", index + 2));
        }
        let midi = resolve_input(&root, &fields[9], "MIDI")?;
        let audio = resolve_input(&root, &fields[10], "audio")?;
        if !input_paths.insert(midi.clone()) || !input_paths.insert(audio.clone()) {
            return Err(format!("duplicate input path at row {}", index + 2));
        }
        entries.push(Selection {
            drummer: fields[0].clone(),
            session: fields[1].clone(),
            id: fields[2].clone(),
            style: fields[3].clone(),
            bpm,
            beat_type: fields[5].clone(),
            time_signature: fields[6].clone(),
            declared_duration,
            split: fields[8].clone(),
            midi,
            audio,
            kit_name: fields[11].clone(),
        });
    }
    if entries.is_empty() {
        return Err("manifest has no entries".to_string());
    }
    Ok(SelectionManifest {
        path,
        sha256: hex::encode(Sha256::digest(&bytes)),
        entries,
    })
}

fn positive_finite(value: &str, name: &str, row: usize) -> Result<f64, String> {
    let parsed = value
        .parse::<f64>()
        .map_err(|_| format!("invalid {name} at row {row}: {value}"))?;
    if !parsed.is_finite() || parsed <= 0.0 {
        return Err(format!("invalid {name} at row {row}: {value}"));
    }
    Ok(parsed)
}

fn resolve_input(root: &Path, value: &str, kind: &str) -> Result<PathBuf, String> {
    let relative = Path::new(value);
    if relative
        .components()
        .any(|part| !matches!(part, Component::Normal(_)))
    {
        return Err(format!("invalid {kind} relative path: {value}"));
    }
    let resolved = fs::canonicalize(root.join(relative))
        .map_err(|error| format!("missing {kind} {value}: {error}"))?;
    if !resolved.starts_with(root) || !resolved.is_file() {
        return Err(format!(
            "{kind} is outside dataset root or not a file: {value}"
        ));
    }
    Ok(resolved)
}

fn parse_csv_line(line: &str) -> Result<Vec<String>, String> {
    let mut fields = Vec::new();
    let mut field = String::new();
    let mut chars = line.chars().peekable();
    let mut quoted = false;
    let mut closed_quote = false;
    while let Some(character) = chars.next() {
        if quoted {
            if character == '"' {
                if chars.peek() == Some(&'"') {
                    chars.next();
                    field.push('"');
                } else {
                    quoted = false;
                    closed_quote = true;
                }
            } else {
                field.push(character);
            }
        } else if character == ',' {
            fields.push(std::mem::take(&mut field));
            closed_quote = false;
        } else if character == '"' && field.is_empty() && !closed_quote {
            quoted = true;
        } else if closed_quote || character == '"' {
            return Err("invalid CSV quoting".to_string());
        } else {
            field.push(character);
        }
    }
    if quoted {
        return Err("unterminated CSV quote".to_string());
    }
    fields.push(field);
    Ok(fields)
}

pub(crate) fn read_mono_pcm_wav(path: &Path) -> Result<MonoWav, String> {
    let bytes = fs::read(path).map_err(|error| error.to_string())?;
    if bytes.get(..4) != Some(b"RIFF") || bytes.get(8..12) != Some(b"WAVE") {
        return Err(format!("not a RIFF WAVE: {}", path.display()));
    }
    let declared_riff_size = u32::from_le_bytes(bytes[4..8].try_into().unwrap()) as usize;
    if declared_riff_size.checked_add(8) != Some(bytes.len()) {
        return Err("RIFF size does not match file length".to_string());
    }
    let mut offset = 12_usize;
    let (mut format, mut data) = (None, None);
    while offset + 8 <= bytes.len() {
        let id = &bytes[offset..offset + 4];
        let len = u32::from_le_bytes(bytes[offset + 4..offset + 8].try_into().unwrap()) as usize;
        let start = offset + 8;
        let end = start.checked_add(len).ok_or("WAV chunk overflow")?;
        let chunk = bytes.get(start..end).ok_or("truncated WAV chunk")?;
        match id {
            b"fmt " if format.replace(chunk).is_some() => {
                return Err("duplicate WAV fmt chunk".to_string());
            }
            b"data" if data.replace(chunk).is_some() => {
                return Err("duplicate WAV data chunk".to_string());
            }
            _ => {}
        }
        offset = end
            .checked_add(len & 1)
            .ok_or("WAV chunk padding overflow")?;
        if offset > bytes.len() {
            return Err("truncated WAV chunk padding".to_string());
        }
    }
    let format = format.ok_or("missing WAV fmt")?;
    let data = data.ok_or("missing WAV data")?;
    if format.len() < 16 {
        return Err("truncated WAV fmt".to_string());
    }
    let code = u16::from_le_bytes(format[0..2].try_into().unwrap());
    let channels = u16::from_le_bytes(format[2..4].try_into().unwrap());
    let sample_rate = u32::from_le_bytes(format[4..8].try_into().unwrap());
    let byte_rate = u32::from_le_bytes(format[8..12].try_into().unwrap());
    let block_align = u16::from_le_bytes(format[12..14].try_into().unwrap());
    let bits_per_sample = u16::from_le_bytes(format[14..16].try_into().unwrap());
    let bytes_per_sample = usize::from(bits_per_sample / 8);
    let expected_align = usize::from(channels) * bytes_per_sample;
    if code != 1
        || channels != 1
        || !matches!(bits_per_sample, 16 | 24)
        || sample_rate == 0
        || usize::from(block_align) != expected_align
        || u64::from(byte_rate) != u64::from(sample_rate) * expected_align as u64
        || data.len() % expected_align != 0
    {
        return Err(format!(
            "unsupported WAV: code={code} ch={channels} rate={sample_rate} bits={bits_per_sample}"
        ));
    }
    let samples = if bits_per_sample == 16 {
        data.chunks_exact(2)
            .map(|value| i16::from_le_bytes([value[0], value[1]]) as f32 / 32_768.0)
            .collect::<Vec<_>>()
    } else {
        data.chunks_exact(3)
            .map(|value| {
                let raw =
                    i32::from(value[0]) | (i32::from(value[1]) << 8) | (i32::from(value[2]) << 16);
                ((raw << 8) >> 8) as f32 / 8_388_608.0
            })
            .collect::<Vec<_>>()
    };
    let sample_count = samples.len();
    Ok(MonoWav {
        metadata: WavMetadata {
            sample_rate,
            channels,
            bits_per_sample,
            sample_count,
            duration_secs: sample_count as f64 / f64::from(sample_rate),
        },
        samples,
    })
}

pub(crate) fn read_midi_labels(path: &Path) -> Result<MidiLabels, String> {
    let bytes = fs::read(path).map_err(|error| error.to_string())?;
    if bytes.get(..4) != Some(b"MThd") || bytes.len() < 14 {
        return Err(format!("invalid MIDI: {}", path.display()));
    }
    let header_len = u32::from_be_bytes(bytes[4..8].try_into().unwrap()) as usize;
    if header_len < 6 || bytes.len() < 8 + header_len {
        return Err("truncated MIDI header".to_string());
    }
    let format = u16::from_be_bytes([bytes[8], bytes[9]]);
    let tracks = u16::from_be_bytes([bytes[10], bytes[11]]) as usize;
    let division = u16::from_be_bytes([bytes[12], bytes[13]]);
    if format > 1
        || tracks == 0
        || (format == 0 && tracks != 1)
        || division & 0x8000 != 0
        || division == 0
    {
        return Err("unsupported MIDI header".to_string());
    }
    let mut offset = 8 + header_len;
    let mut tempos = Vec::new();
    let mut notes = Vec::new();
    for _ in 0..tracks {
        let header = bytes
            .get(offset..offset + 8)
            .ok_or("truncated MIDI track header")?;
        if header.get(..4) != Some(b"MTrk") {
            return Err("missing MIDI track".to_string());
        }
        let len = u32::from_be_bytes(header[4..8].try_into().unwrap()) as usize;
        let start = offset.checked_add(8).ok_or("MIDI track offset overflow")?;
        let end = start.checked_add(len).ok_or("MIDI track length overflow")?;
        parse_midi_track(
            bytes.get(start..end).ok_or("truncated MIDI track")?,
            &mut tempos,
            &mut notes,
        )?;
        offset = end;
    }
    tempos.sort_by_key(|event| event.0);
    if tempos
        .windows(2)
        .any(|pair| pair[0].0 == pair[1].0 && pair[0].1 != pair[1].1)
    {
        return Err("conflicting MIDI tempos at the same tick".to_string());
    }
    notes.sort_by_key(|event| event.0);
    let raw_notes = notes
        .into_iter()
        .map(|(tick, pitch, velocity)| MidiNote {
            time_secs: tick_to_seconds(tick, division, &tempos),
            pitch,
            velocity,
        })
        .collect::<Vec<_>>();
    let raw_note_count = raw_notes.len();
    let mut events = Vec::new();
    let mut start = 0;
    while start < raw_notes.len() {
        let mut end = start + 1;
        while end < raw_notes.len()
            && raw_notes[end].time_secs - raw_notes[start].time_secs <= LABEL_CLUSTER_SECS + 1e-12
        {
            end += 1;
        }
        let notes = raw_notes[start..end].to_vec();
        let mut pitches = notes.iter().map(|note| note.pitch).collect::<Vec<_>>();
        pitches.sort_unstable();
        pitches.dedup();
        events.push(LabelEvent {
            time_secs: notes.iter().map(|note| note.time_secs).sum::<f64>() / notes.len() as f64,
            kick: pitches.contains(&36),
            hat: pitches.iter().any(|pitch| HAT_NOTES.contains(pitch)),
            note_count: notes.len(),
            pitches,
            notes,
        });
        start = end;
    }
    Ok(MidiLabels {
        events,
        raw_note_count,
    })
}

fn parse_midi_track(
    track: &[u8],
    tempos: &mut Vec<(u64, u32)>,
    notes: &mut Vec<(u64, u8, u8)>,
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
            let kind = *track.get(offset).ok_or("truncated MIDI meta")?;
            offset += 1;
            let len = read_variable(track, &mut offset)? as usize;
            let end = offset.checked_add(len).ok_or("MIDI meta overflow")?;
            let payload = track
                .get(offset..end)
                .ok_or("truncated MIDI meta payload")?;
            if kind == 0x51 && len == 3 {
                let tempo = u32::from_be_bytes([0, payload[0], payload[1], payload[2]]);
                if tempo == 0 {
                    return Err("zero MIDI tempo".to_string());
                }
                tempos.push((tick, tempo));
            }
            offset = end;
        } else if matches!(status, 0xf0 | 0xf7) {
            let len = read_variable(track, &mut offset)? as usize;
            offset = offset.checked_add(len).ok_or("MIDI sysex overflow")?;
            if offset > track.len() {
                return Err("truncated MIDI sysex".to_string());
            }
        } else if status < 0xf0 {
            let kind = status & 0xf0;
            let data_len = if matches!(kind, 0xc0 | 0xd0) { 1 } else { 2 };
            let payload = track
                .get(offset..offset + data_len)
                .ok_or("truncated MIDI channel event")?;
            if payload.iter().any(|byte| byte & 0x80 != 0) {
                return Err("invalid MIDI channel data".to_string());
            }
            if kind == 0x90 && payload[1] != 0 {
                notes.push((tick, payload[0], payload[1]));
            }
            offset += data_len;
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

fn tick_to_seconds(tick: u64, division: u16, tempos: &[(u64, u32)]) -> f64 {
    let (mut seconds, mut cursor, mut tempo) = (0.0, 0_u64, 500_000_u32);
    for &(change_tick, next_tempo) in tempos {
        if change_tick > tick {
            break;
        }
        seconds +=
            (change_tick - cursor) as f64 * f64::from(tempo) / (f64::from(division) * 1_000_000.0);
        cursor = change_tick;
        tempo = next_tempo;
    }
    seconds + (tick - cursor) as f64 * f64::from(tempo) / (f64::from(division) * 1_000_000.0)
}

#[cfg(test)]
#[path = "input_tests.rs"]
mod tests;
