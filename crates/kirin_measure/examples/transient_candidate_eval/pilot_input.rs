use std::fs;
use std::path::Path;

use crate::contract::sha256_bytes;
use crate::drum_midi::{excerpt_drum_midi, parse_drum_midi};
use crate::evaluation::LoadedTrack;
use crate::input::{decode_mono_pcm_wav, labels_from_parsed, Selection};

const SAMPLE_RATE: u32 = 44_100;

pub(crate) fn load_development_pilot_track(
    root: &Path,
    selection: Selection,
) -> Result<LoadedTrack, String> {
    let formal = selection
        .formal
        .as_ref()
        .ok_or("development pilot requires formal manifest metadata")?;
    let audio_bytes = fs::read(&selection.audio)
        .map_err(|error| format!("cannot read {}: {error}", selection.audio.display()))?;
    let midi_bytes = fs::read(&selection.midi)
        .map_err(|error| format!("cannot read {}: {error}", selection.midi.display()))?;
    let audio_sha256 = sha256_bytes(&audio_bytes);
    let midi_sha256 = sha256_bytes(&midi_bytes);
    if midi_sha256 != formal.expected_midi_sha256 {
        return Err(format!("MIDI SHA-256 mismatch: {}", selection.id));
    }

    let wav = decode_mono_pcm_wav(&selection.audio, &audio_bytes)?;
    if wav.metadata.sample_rate != SAMPLE_RATE {
        return Err(format!("pilot input is not 44.1 kHz: {}", selection.id));
    }
    let core_start = usize::try_from(formal.excerpt_start_sample_44100)
        .map_err(|_| format!("core start does not fit usize: {}", selection.id))?;
    let core_end = usize::try_from(formal.excerpt_end_sample_44100)
        .map_err(|_| format!("core end does not fit usize: {}", selection.id))?;
    let core = wav
        .samples
        .get(core_start..core_end)
        .ok_or_else(|| format!("pilot core is outside source audio: {}", selection.id))?;
    if core.is_empty() || core.iter().all(|sample| *sample == 0.0) {
        return Err(format!(
            "pilot core is empty or exact-silent: {}",
            selection.id
        ));
    }

    let parsed = parse_drum_midi(&midi_bytes)
        .map_err(|error| format!("invalid MIDI {}: {error}", selection.midi.display()))?;
    let excerpt = excerpt_drum_midi(
        &parsed,
        formal.excerpt_start_sample_44100,
        formal.excerpt_end_sample_44100,
        SAMPLE_RATE,
    )?;
    let kick_only = excerpt
        .events
        .iter()
        .filter(|event| event.kick_only)
        .count();
    let hat_only = excerpt.events.iter().filter(|event| event.hat_only).count();
    if excerpt.raw_notes != formal.declared_excerpt_raw_notes
        || excerpt.events.len() != formal.declared_excerpt_compound_events
        || kick_only != formal.declared_excerpt_kick_only_events
        || hat_only != formal.declared_excerpt_hat_only_events
    {
        return Err(format!("pilot MIDI count mismatch: {}", selection.id));
    }
    let labels = labels_from_parsed(&excerpt)?;
    let peak_abs = core
        .iter()
        .map(|sample| sample.abs())
        .fold(0.0_f32, f32::max);
    Ok(LoadedTrack {
        midi_relative: relative_utf8(root, &selection.midi)?,
        audio_relative: relative_utf8(root, &selection.audio)?,
        midi_size_bytes: midi_bytes.len() as u64,
        audio_size_bytes: audio_bytes.len() as u64,
        midi_sha256,
        audio_sha256,
        selection,
        wav,
        labels,
        peak_abs,
    })
}

fn relative_utf8(root: &Path, path: &Path) -> Result<String, String> {
    path.strip_prefix(root)
        .map_err(|_| format!("pilot input outside root: {}", path.display()))?
        .to_str()
        .map(str::to_string)
        .ok_or_else(|| format!("pilot input path is not UTF-8: {}", path.display()))
}
