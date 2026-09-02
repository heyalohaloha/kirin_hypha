use std::fs;
use std::path::{Component, Path};

use crate::drum_midi::parse_drum_midi;

use super::io::sha256_bytes;

pub(crate) fn read_proxy_onsets(
    root: &Path,
    relative_path: &str,
    expected_sha256: &str,
) -> Result<Vec<u64>, String> {
    let root = fs::canonicalize(root)
        .map_err(|error| format!("cannot resolve MIDI root {}: {error}", root.display()))?;
    if !root.is_dir() {
        return Err(format!("MIDI root is not a directory: {}", root.display()));
    }
    let relative = Path::new(relative_path);
    if relative.is_absolute()
        || relative
            .components()
            .any(|component| !matches!(component, Component::Normal(_)))
    {
        return Err(format!("invalid MIDI relative path: {relative_path}"));
    }
    let path = fs::canonicalize(root.join(relative))
        .map_err(|error| format!("cannot resolve MIDI {relative_path}: {error}"))?;
    if !path.starts_with(&root) || !path.is_file() {
        return Err(format!(
            "MIDI escapes its root or is not a file: {relative_path}"
        ));
    }
    let bytes = fs::read(&path).map_err(|error| format!("cannot read MIDI: {error}"))?;
    let actual_sha256 = sha256_bytes(&bytes);
    if actual_sha256 != expected_sha256 {
        return Err(format!(
            "MIDI SHA-256 mismatch for {relative_path}: expected {expected_sha256}, got {actual_sha256}"
        ));
    }
    parse_proxy_onsets(&bytes)
}

fn parse_proxy_onsets(bytes: &[u8]) -> Result<Vec<u64>, String> {
    Ok(parse_drum_midi(bytes)?
        .events
        .into_iter()
        .map(|event| event.time_micros)
        .collect())
}

#[cfg(test)]
mod tests {
    use super::*;

    fn midi(gaps: &[u8]) -> Vec<u8> {
        let mut track = vec![0, 0xff, 0x51, 3, 0x0f, 0x42, 0x40, 0, 0x99, 36, 100];
        for (index, gap) in gaps.iter().enumerate() {
            track.extend_from_slice(&[*gap, 42 + index as u8, 80]);
        }
        track.extend_from_slice(&[0, 0xff, 0x2f, 0]);
        let mut bytes = b"MThd\0\0\0\x06\0\0\0\x01".to_vec();
        bytes.extend_from_slice(&1_000_u16.to_be_bytes());
        bytes.extend_from_slice(b"MTrk");
        bytes.extend_from_slice(&(track.len() as u32).to_be_bytes());
        bytes.extend_from_slice(&track);
        bytes
    }

    #[test]
    fn compound_span_is_inclusive_and_non_chaining() {
        assert_eq!(parse_proxy_onsets(&midi(&[29])).unwrap(), [14_500]);
        assert_eq!(parse_proxy_onsets(&midi(&[30])).unwrap(), [15_000]);
        assert_eq!(parse_proxy_onsets(&midi(&[31])).unwrap(), [0, 31_000]);
        assert_eq!(
            parse_proxy_onsets(&midi(&[20, 20])).unwrap(),
            [10_000, 40_000]
        );
    }

    #[test]
    fn malformed_and_empty_midi_fail_closed() {
        assert!(parse_proxy_onsets(b"not midi").is_err());
        let mut empty = midi(&[]);
        let note = empty
            .windows(4)
            .position(|window| window == [0, 0x99, 36, 100])
            .unwrap();
        empty[note + 3] = 0;
        assert!(parse_proxy_onsets(&empty).is_err());
    }
}
