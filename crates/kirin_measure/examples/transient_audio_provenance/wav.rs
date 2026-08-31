#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub(crate) struct WavMetadata {
    pub(crate) sample_rate: u32,
    pub(crate) channels: u16,
    pub(crate) bits_per_sample: u16,
    pub(crate) sample_count: u64,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub(crate) struct DecodedWav {
    pub(crate) metadata: WavMetadata,
    pub(crate) pcm24: Vec<i32>,
}

pub(crate) fn decode_mono_integer_pcm(bytes: &[u8]) -> Result<DecodedWav, String> {
    if bytes.len() < 12 || bytes.get(..4) != Some(b"RIFF") || bytes.get(8..12) != Some(b"WAVE") {
        return Err("audio member is not a RIFF WAVE file".to_string());
    }
    let declared = u32::from_le_bytes(bytes[4..8].try_into().unwrap()) as usize;
    if declared.checked_add(8) != Some(bytes.len()) {
        return Err("RIFF size does not match the exact member length".to_string());
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
            .ok_or("trailing bytes do not form a complete WAV chunk header")?;
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
    if offset != bytes.len() {
        return Err("WAV parser did not consume the exact member".to_string());
    }
    let format = format.ok_or("WAV fmt chunk is missing")?;
    let data = data.ok_or("WAV data chunk is missing")?;
    if format.len() != 16 {
        return Err("canonical PCM WAV requires an exact 16-byte fmt chunk".to_string());
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
        || channels != 1
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
            sample_count: pcm24.len() as u64,
        },
        pcm24,
    })
}

#[cfg(test)]
#[path = "wav_tests.rs"]
mod tests;
