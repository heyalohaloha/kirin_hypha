#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub(crate) struct WavMetadata {
    pub(crate) sample_rate: u32,
    pub(crate) channels: u16,
    pub(crate) bits_per_sample: u16,
    pub(crate) sample_frames: u64,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub(crate) struct DecodedWav {
    pub(crate) metadata: WavMetadata,
    pub(crate) pcm24: Vec<i32>,
}

pub(crate) fn decode_integer_pcm(bytes: &[u8]) -> Result<DecodedWav, String> {
    if bytes.len() < 12 || bytes.get(..4) != Some(b"RIFF") || bytes.get(8..12) != Some(b"WAVE") {
        return Err("audio is not a RIFF WAVE file".to_string());
    }
    let declared = u32::from_le_bytes(bytes[4..8].try_into().unwrap()) as usize;
    if declared.checked_add(8) != Some(bytes.len()) {
        return Err("RIFF size does not match the exact file length".to_string());
    }
    let mut offset = 12_usize;
    let mut format = None;
    let mut data = None;
    while offset < bytes.len() {
        let fixed_end = offset
            .checked_add(8)
            .ok_or("WAV chunk header range overflow")?;
        let fixed = bytes
            .get(offset..fixed_end)
            .ok_or("trailing bytes do not form a WAV chunk header")?;
        let length = u32::from_le_bytes(fixed[4..8].try_into().unwrap()) as usize;
        let end = fixed_end
            .checked_add(length)
            .ok_or("WAV chunk payload range overflow")?;
        let chunk = bytes
            .get(fixed_end..end)
            .ok_or("WAV chunk payload is truncated")?;
        match &fixed[..4] {
            b"fmt " if format.replace(chunk).is_some() => {
                return Err("duplicate WAV fmt chunk".to_string());
            }
            b"data" if data.replace(chunk).is_some() => {
                return Err("duplicate WAV data chunk".to_string());
            }
            _ => {}
        }
        offset = end
            .checked_add(length & 1)
            .ok_or("WAV chunk padding range overflow")?;
        if offset > bytes.len() {
            return Err("WAV chunk padding is truncated".to_string());
        }
    }
    let format = format.ok_or("WAV fmt chunk is missing")?;
    let data = data.ok_or("WAV data chunk is missing")?;
    if format.len() != 16 {
        return Err("PCM WAV requires an exact 16-byte fmt chunk".to_string());
    }
    let code = u16::from_le_bytes(format[0..2].try_into().unwrap());
    let channels = u16::from_le_bytes(format[2..4].try_into().unwrap());
    let sample_rate = u32::from_le_bytes(format[4..8].try_into().unwrap());
    let byte_rate = u32::from_le_bytes(format[8..12].try_into().unwrap());
    let block_align = u16::from_le_bytes(format[12..14].try_into().unwrap());
    let bits_per_sample = u16::from_le_bytes(format[14..16].try_into().unwrap());
    let bytes_per_sample = usize::from(bits_per_sample / 8);
    let expected_align = usize::from(channels)
        .checked_mul(bytes_per_sample)
        .ok_or("WAV block alignment overflow")?;
    if code != 1
        || !matches!(channels, 1 | 2)
        || sample_rate != 44_100
        || !matches!(bits_per_sample, 16 | 24)
        || usize::from(block_align) != expected_align
        || u64::from(byte_rate)
            != u64::from(sample_rate)
                .checked_mul(expected_align as u64)
                .ok_or("WAV byte rate overflow")?
        || data.is_empty()
        || data.len() % expected_align != 0
    {
        return Err(format!(
            "unsupported WAV contract: code={code} channels={channels} rate={sample_rate} bits={bits_per_sample}"
        ));
    }
    let pcm24 = if bits_per_sample == 16 {
        data.as_chunks::<2>()
            .0
            .iter()
            .map(|sample| i32::from(i16::from_le_bytes([sample[0], sample[1]])) << 8)
            .collect()
    } else {
        data.as_chunks::<3>()
            .0
            .iter()
            .map(|sample| {
                let raw = i32::from(sample[0])
                    | (i32::from(sample[1]) << 8)
                    | (i32::from(sample[2]) << 16);
                (raw << 8) >> 8
            })
            .collect::<Vec<_>>()
    };
    Ok(DecodedWav {
        metadata: WavMetadata {
            sample_rate,
            channels,
            bits_per_sample,
            sample_frames: (data.len() / expected_align) as u64,
        },
        pcm24,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    fn fixture(channels: u16, bits: u16, samples: &[u8]) -> Vec<u8> {
        let align = channels * (bits / 8);
        let mut bytes = Vec::new();
        bytes.extend_from_slice(b"RIFF");
        bytes.extend_from_slice(&0_u32.to_le_bytes());
        bytes.extend_from_slice(b"WAVEfmt ");
        bytes.extend_from_slice(&16_u32.to_le_bytes());
        bytes.extend_from_slice(&1_u16.to_le_bytes());
        bytes.extend_from_slice(&channels.to_le_bytes());
        bytes.extend_from_slice(&44_100_u32.to_le_bytes());
        bytes.extend_from_slice(&(44_100_u32 * u32::from(align)).to_le_bytes());
        bytes.extend_from_slice(&align.to_le_bytes());
        bytes.extend_from_slice(&bits.to_le_bytes());
        bytes.extend_from_slice(b"data");
        bytes.extend_from_slice(&(samples.len() as u32).to_le_bytes());
        bytes.extend_from_slice(samples);
        if samples.len() % 2 == 1 {
            bytes.push(0);
        }
        let size = (bytes.len() - 8) as u32;
        bytes[4..8].copy_from_slice(&size.to_le_bytes());
        bytes
    }

    #[test]
    fn decodes_mono_and_stereo_integer_pcm() {
        let mono = decode_integer_pcm(&fixture(1, 16, &[0, 128, 255, 127])).unwrap();
        assert_eq!(mono.metadata.sample_frames, 2);
        assert_eq!(mono.pcm24, vec![-8_388_608, 8_388_352]);
        let stereo = decode_integer_pcm(&fixture(2, 24, &[1, 0, 0, 255, 255, 255])).unwrap();
        assert_eq!(stereo.metadata.sample_frames, 1);
        assert_eq!(stereo.pcm24, vec![1, -1]);
    }

    #[test]
    fn rejects_unsupported_topology_rate_and_truncation() {
        assert!(decode_integer_pcm(&fixture(3, 16, &[0; 6])).is_err());
        let mut truncated = fixture(1, 16, &[0, 0]);
        truncated.pop();
        assert!(decode_integer_pcm(&truncated).is_err());
    }
}
