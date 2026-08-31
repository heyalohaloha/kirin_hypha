use super::*;

fn wav(bits: u16, samples: &[i32]) -> Vec<u8> {
    let width = usize::from(bits / 8);
    let data_len = samples.len() * width;
    let padding = data_len & 1;
    let mut bytes = Vec::with_capacity(44 + data_len + padding);
    bytes.extend_from_slice(b"RIFF");
    bytes.extend_from_slice(&((36 + data_len + padding) as u32).to_le_bytes());
    bytes.extend_from_slice(b"WAVEfmt ");
    bytes.extend_from_slice(&16_u32.to_le_bytes());
    bytes.extend_from_slice(&1_u16.to_le_bytes());
    bytes.extend_from_slice(&1_u16.to_le_bytes());
    bytes.extend_from_slice(&44_100_u32.to_le_bytes());
    bytes.extend_from_slice(&(44_100_u32 * u32::from(bits / 8)).to_le_bytes());
    bytes.extend_from_slice(&(bits / 8).to_le_bytes());
    bytes.extend_from_slice(&bits.to_le_bytes());
    bytes.extend_from_slice(b"data");
    bytes.extend_from_slice(&(data_len as u32).to_le_bytes());
    for sample in samples {
        if bits == 16 {
            bytes.extend_from_slice(&(*sample as i16).to_le_bytes());
        } else {
            let encoded = sample.to_le_bytes();
            bytes.extend_from_slice(&encoded[..3]);
        }
    }
    if padding != 0 {
        bytes.push(0);
    }
    bytes
}

#[test]
fn decodes_pcm16_and_pcm24_to_one_signed24_numerator() {
    let decoded = decode_mono_integer_pcm(&wav(16, &[-32_768, -1, 0, 1, 32_767])).unwrap();
    assert_eq!(decoded.metadata.sample_count, 5);
    assert_eq!(decoded.pcm24, [-8_388_608, -256, 0, 256, 8_388_352]);
    let decoded = decode_mono_integer_pcm(&wav(24, &[-8_388_608, -1, 0, 1, 8_388_607])).unwrap();
    assert_eq!(decoded.pcm24, [-8_388_608, -1, 0, 1, 8_388_607]);
}

#[test]
fn rejects_format_rate_channel_alignment_and_byte_rate_drift() {
    let base = wav(16, &[1, 2]);
    for (offset, replacement) in [
        (20, 3_u16.to_le_bytes().to_vec()),
        (22, 2_u16.to_le_bytes().to_vec()),
        (24, 48_000_u32.to_le_bytes().to_vec()),
        (32, 4_u16.to_le_bytes().to_vec()),
        (34, 32_u16.to_le_bytes().to_vec()),
    ] {
        let mut broken = base.clone();
        broken[offset..offset + replacement.len()].copy_from_slice(&replacement);
        assert!(decode_mono_integer_pcm(&broken).is_err());
    }
    let mut byte_rate = base;
    byte_rate[28..32].copy_from_slice(&1_u32.to_le_bytes());
    assert!(decode_mono_integer_pcm(&byte_rate).is_err());
}

#[test]
fn rejects_duplicate_missing_truncated_and_trailing_chunks() {
    let base = wav(16, &[1, 2]);
    let mut duplicate = base.clone();
    duplicate.extend_from_slice(b"data\0\0\0\0");
    let size = (duplicate.len() - 8) as u32;
    duplicate[4..8].copy_from_slice(&size.to_le_bytes());
    assert!(decode_mono_integer_pcm(&duplicate).is_err());

    let mut missing = base.clone();
    missing[36..40].copy_from_slice(b"JUNK");
    assert!(decode_mono_integer_pcm(&missing).is_err());

    for trailing in 1..=7 {
        let mut bytes = base.clone();
        bytes.extend(std::iter::repeat_n(0_u8, trailing));
        let size = (bytes.len() - 8) as u32;
        bytes[4..8].copy_from_slice(&size.to_le_bytes());
        assert!(
            decode_mono_integer_pcm(&bytes).is_err(),
            "trailing={trailing}"
        );
    }
    let mut truncated = base;
    truncated.pop();
    assert!(decode_mono_integer_pcm(&truncated).is_err());
}

#[test]
fn rejects_empty_or_misaligned_pcm_data() {
    assert!(decode_mono_integer_pcm(&wav(16, &[])).is_err());
    let mut bytes = wav(24, &[1]);
    bytes.pop();
    let size = (bytes.len() - 8) as u32;
    bytes[4..8].copy_from_slice(&size.to_le_bytes());
    bytes[40..44].copy_from_slice(&2_u32.to_le_bytes());
    assert!(decode_mono_integer_pcm(&bytes).is_err());
}
