use super::*;
use std::io::Write;
use tempfile::{NamedTempFile, TempDir};

fn midi_file(division: u16, track: &[u8]) -> NamedTempFile {
    let mut file = NamedTempFile::new().unwrap();
    let mut bytes = b"MThd\0\0\0\x06\0\0\0\x01".to_vec();
    bytes.extend_from_slice(&division.to_be_bytes());
    bytes.extend_from_slice(b"MTrk");
    bytes.extend_from_slice(&(track.len() as u32).to_be_bytes());
    bytes.extend_from_slice(track);
    file.write_all(&bytes).unwrap();
    file
}

fn tempo_and_notes(gaps_ms: &[u8]) -> NamedTempFile {
    let mut track = vec![0, 0xff, 0x51, 3, 0x0f, 0x42, 0x40, 0, 0x99, 36, 100];
    for (index, gap) in gaps_ms.iter().enumerate() {
        track.extend_from_slice(&[*gap, 0x99, 42 + index as u8, 80]);
    }
    track.extend_from_slice(&[0, 0xff, 0x2f, 0]);
    midi_file(1_000, &track)
}

#[test]
fn compound_boundary_is_inclusive_without_adjacent_chaining() {
    for (gap, expected) in [(29, 1), (30, 1), (31, 2)] {
        let labels = read_midi_labels(tempo_and_notes(&[gap]).path()).unwrap();
        assert_eq!(labels.events.len(), expected, "gap={gap}");
        assert_eq!(labels.raw_note_count, 2);
        let expected_time = if gap <= 30 {
            f64::from(gap) / 2_000.0
        } else {
            0.0
        };
        assert_eq!(labels.events[0].time_secs, expected_time);
    }
    let labels = read_midi_labels(tempo_and_notes(&[20, 20]).path()).unwrap();
    assert_eq!(labels.events.len(), 2);
    assert_eq!(labels.events[0].note_count, 2);
    assert!((labels.events[0].time_secs - 0.010).abs() < 1e-12);
    assert!((labels.events[1].time_secs - 0.040).abs() < 1e-12);
}

#[test]
fn tempo_at_tick_zero_and_running_status_retain_note_data() {
    let track = [
        0, 0xff, 0x51, 3, 0x0f, 0x42, 0x40, 0x83, 0x60, 0x99, 36, 100, 10, 42, 77, 0, 0xff, 0x2f, 0,
    ];
    let labels = read_midi_labels(midi_file(480, &track).path()).unwrap();
    assert_eq!(labels.raw_note_count, 2);
    assert!((labels.events[0].notes[0].time_secs - 1.0).abs() < 1e-12);
    assert_eq!(labels.events[0].notes[1].velocity, 77);
    assert_eq!(labels.events[0].pitches, [36, 42]);
    assert!(labels.events[0].kick && labels.events[0].hat);
}

#[test]
fn official_kick_and_hat_pitches_are_exact() {
    let mut track = vec![0, 0x99, 35, 1];
    for pitch in [22, 26, 36, 44, 46] {
        track.extend_from_slice(&[0, 0x99, pitch, 1]);
    }
    track.extend_from_slice(&[0, 0xff, 0x2f, 0]);
    let labels = read_midi_labels(midi_file(480, &track).path()).unwrap();
    assert_eq!(labels.events[0].pitches, [22, 26, 35, 36, 44, 46]);
    assert!(labels.events[0].kick && labels.events[0].hat);
    assert_eq!(labels.events[0].note_count, 6);
}

fn wav_bytes(bits: u16, channels: u16, data: &[u8]) -> Vec<u8> {
    let align = channels * (bits / 8);
    let mut bytes = b"RIFF\0\0\0\0WAVEfmt \x10\0\0\0\x01\0".to_vec();
    bytes.extend_from_slice(&channels.to_le_bytes());
    bytes.extend_from_slice(&44_100_u32.to_le_bytes());
    bytes.extend_from_slice(&(44_100_u32 * u32::from(align)).to_le_bytes());
    bytes.extend_from_slice(&align.to_le_bytes());
    bytes.extend_from_slice(&bits.to_le_bytes());
    bytes.extend_from_slice(b"data");
    bytes.extend_from_slice(&(data.len() as u32).to_le_bytes());
    bytes.extend_from_slice(data);
    if data.len() & 1 == 1 {
        bytes.push(0);
    }
    let riff_len = bytes.len() as u32 - 8;
    bytes[4..8].copy_from_slice(&riff_len.to_le_bytes());
    bytes
}

#[test]
fn reads_pcm16_and_pcm24_with_metadata() {
    for (bits, data, expected) in [
        (16, vec![0xff, 0x7f, 0x00, 0x80], 2),
        (24, vec![0xff, 0xff, 0x7f, 0, 0, 0, 0, 0, 0x80], 3),
    ] {
        let mut file = NamedTempFile::new().unwrap();
        file.write_all(&wav_bytes(bits, 1, &data)).unwrap();
        let wav = read_mono_pcm_wav(file.path()).unwrap();
        assert_eq!(wav.metadata.bits_per_sample, bits);
        assert_eq!(wav.metadata.sample_count, expected);
        assert_eq!(wav.samples.len(), expected);
        assert!(wav
            .samples
            .iter()
            .all(|sample| (-1.0..=1.0).contains(sample)));
    }
}

#[test]
fn rejects_unsupported_or_truncated_wav() {
    let mut wrong_riff_size = wav_bytes(16, 1, &[0; 4]);
    wrong_riff_size[4..8].copy_from_slice(&1_u32.to_le_bytes());
    for bytes in [
        wav_bytes(32, 1, &[0; 4]),
        wav_bytes(16, 2, &[0; 4]),
        b"RIFF".to_vec(),
        wrong_riff_size,
    ] {
        let mut file = NamedTempFile::new().unwrap();
        file.write_all(&bytes).unwrap();
        assert!(read_mono_pcm_wav(file.path()).is_err());
    }
}

#[test]
fn conflicting_tempos_at_one_tick_fail_closed() {
    let track = [
        0, 0xff, 0x51, 3, 0x07, 0xa1, 0x20, 0, 0xff, 0x51, 3, 0x0f, 0x42, 0x40, 0, 0x99, 36, 100,
        0, 0xff, 0x2f, 0,
    ];
    let error = read_midi_labels(midi_file(480, &track).path()).unwrap_err();
    assert!(error.contains("conflicting"), "{error}");
}

fn row(id: &str, kit: &str, midi: &str, wav: &str, split: &str) -> String {
    format!("d,s,{id},rock,120,beat,4-4,1.0,{split},{midi},{wav},{kit}")
}

fn manifest_case(rows: &[String]) -> (TempDir, PathBuf) {
    let root = TempDir::new().unwrap();
    for name in ["a.mid", "a.wav", "b.mid", "b.wav"] {
        fs::write(root.path().join(name), b"x").unwrap();
    }
    let manifest = root.path().join("manifest.csv");
    fs::write(
        &manifest,
        format!("{MANIFEST_HEADER}\n{}\n", rows.join("\n")),
    )
    .unwrap();
    (root, manifest)
}

#[test]
fn manifest_is_explicit_hashed_and_preserves_metadata() {
    let rows = [
        row("same", "Kit A", "a.mid", "a.wav", "train"),
        row("same", "Kit B", "b.mid", "b.wav", "test"),
    ];
    let (root, path) = manifest_case(&rows);
    let manifest = read_selection(root.path(), &path).unwrap();
    assert_eq!(manifest.entries.len(), 2);
    assert_eq!(manifest.sha256.len(), 64);
    assert_eq!(manifest.path, fs::canonicalize(path).unwrap());
    let first = &manifest.entries[0];
    assert_eq!(first.id, "same");
    assert_eq!(first.kit_name, "Kit A");
    assert_eq!(first.bpm, 120.0);
    assert_eq!(first.declared_duration, 1.0);
}

#[test]
fn manifest_rejects_duplicate_split_and_path_failures() {
    let cases = [
        (
            vec![
                row("x", "K", "a.mid", "a.wav", "train"),
                row("x", "K", "b.mid", "b.wav", "test"),
            ],
            "duplicate",
        ),
        (vec![row("x", "K", "a.mid", "a.wav", "bogus")], "split"),
        (
            vec![row("x", "K", "../a.mid", "a.wav", "train")],
            "relative path",
        ),
        (
            vec![row("x", "K", "missing.mid", "a.wav", "train")],
            "missing",
        ),
        (
            vec![
                row("x", "K", "a.mid", "a.wav", "train"),
                row("y", "K", "a.mid", "b.wav", "test"),
            ],
            "duplicate input path",
        ),
    ];
    for (rows, expected) in cases {
        let (root, path) = manifest_case(&rows);
        let error = read_selection(root.path(), &path).unwrap_err();
        assert!(error.contains(expected), "{error:?}");
    }
    let (root, path) = manifest_case(&[row("x", "K", "a.mid", "a.wav", "train")]);
    fs::write(&path, "wrong\n").unwrap();
    assert!(read_selection(root.path(), &path)
        .unwrap_err()
        .contains("header"));
}
