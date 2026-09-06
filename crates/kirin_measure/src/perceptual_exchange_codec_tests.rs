use super::*;

fn encoded(count: usize, psb: Option<[f64; 20]>) -> Vec<u8> {
    let mut history = PerceptualHistory::with_capacity();
    for i in 1..=count {
        history.push(PerceptualFrame {
            schema_version: PERCEPTUAL_SCHEMA_VERSION,
            sample_rate: 48_000,
            aperture_samples: 4_800,
            presentation_end_samples: i as i64 * 4_800,
            state_epoch_samples: 0,
            generation: 7,
            channel_mode: SpectrumChannelMode::Lr,
            channels: 2,
            sharpness: 1.5,
            psb,
        });
    }
    encode_perceptual_snapshot(Uuid::nil(), &history)
}

#[test]
fn psb_codec_preserves_full_recovery_window_and_optional_silence() {
    for shares in [None, Some([0.05; 20])] {
        let bytes = encoded(PERCEPTUAL_HISTORY_CAPACITY, shares);
        assert_eq!(bytes.len(), 32 + 201 * PERCEPTUAL_HISTORY_CAPACITY);
        assert!(bytes.len() <= PERCEPTUAL_SNAPSHOT_MAX_BYTES as usize);
        let decoded = decode_perceptual_snapshot(&bytes).unwrap();
        assert_eq!(decoded.history.frames().len(), PERCEPTUAL_HISTORY_CAPACITY);
        assert_eq!(decoded.history.newest().unwrap().psb, shares);
    }
}

#[test]
fn psb_codec_rejects_old_truncated_invalid_or_oversized_payloads() {
    let bytes = encoded(2, Some([0.05; 20]));
    for end in 0..bytes.len() {
        assert!(decode_perceptual_snapshot(&bytes[..end]).is_none());
    }
    for (index, value) in [(7, b'2'), (10, 17), (32 + 40, 2), (32 + 26, 1)] {
        let mut invalid = bytes.clone();
        invalid[index] = value;
        assert!(
            decode_perceptual_snapshot(&invalid).is_none(),
            "offset {index}"
        );
    }
    for value in [f64::NAN, f64::INFINITY, -0.1, 0.5] {
        let mut invalid = bytes.clone();
        invalid[73..81].copy_from_slice(&value.to_le_bytes());
        assert!(decode_perceptual_snapshot(&invalid).is_none());
    }
    assert!(decode_perceptual_snapshot(&vec![0; 4_097]).is_none());
    let mut duplicate = bytes.clone();
    duplicate[233..241].copy_from_slice(&4_800i64.to_le_bytes());
    assert!(decode_perceptual_snapshot(&duplicate).is_none());
}
