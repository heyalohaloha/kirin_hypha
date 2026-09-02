//! Deterministic host-rate layout for the on-demand ATTACK candidate evaluator.

use sha2::{Digest, Sha256};

pub const TRANSIENT_REFERENCE_RATE: u32 = 48_000;
pub const TRANSIENT_REFERENCE_WINDOW: u32 = 1_024;
pub const TRANSIENT_REFERENCE_HOP: u32 = 256;
pub const TRANSIENT_MIN_HZ: f32 = 30.0;
pub const TRANSIENT_MAX_HZ: f32 = 20_000.0;
pub const TRANSIENT_POWER_EPSILON: f32 = 1.0e-12;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum TransientOdfKind {
    Mel32,
    Mel40,
    Complex,
    Hybrid,
}

impl TransientOdfKind {
    pub const ALL: [Self; 4] = [Self::Mel32, Self::Mel40, Self::Complex, Self::Hybrid];

    pub const fn as_str(self) -> &'static str {
        match self {
            Self::Mel32 => "mel32",
            Self::Mel40 => "mel40",
            Self::Complex => "complex",
            Self::Hybrid => "hybrid",
        }
    }

    pub const fn mel_bands(self) -> usize {
        match self {
            Self::Mel32 => 32,
            Self::Mel40 | Self::Hybrid => 40,
            Self::Complex => 0,
        }
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct TransientLayout {
    pub sample_rate: u32,
    pub window_samples: usize,
    pub hop_samples: usize,
    pub fft_size: usize,
    pub mel_bands: usize,
    pub definition_hash: [u8; 32],
}

impl TransientLayout {
    pub fn for_rate(sample_rate: u32, kind: TransientOdfKind) -> Result<Self, &'static str> {
        if !(8_000..=384_000).contains(&sample_rate) {
            return Err("unsupported transient sample rate");
        }
        let window_samples = rounded_reference_samples(sample_rate, TRANSIENT_REFERENCE_WINDOW);
        let hop_samples = rounded_reference_samples(sample_rate, TRANSIENT_REFERENCE_HOP);
        let fft_size = window_samples
            .checked_next_power_of_two()
            .ok_or("transient FFT size overflow")?;
        let mel_bands = kind.mel_bands();
        let definition_hash = definition_hash(
            sample_rate,
            window_samples,
            hop_samples,
            fft_size,
            mel_bands,
            kind,
        );
        Ok(Self {
            sample_rate,
            window_samples,
            hop_samples,
            fft_size,
            mel_bands,
            definition_hash,
        })
    }

    pub fn definition_hex(&self) -> String {
        hex::encode(self.definition_hash)
    }
}

fn rounded_reference_samples(sample_rate: u32, reference_samples: u32) -> usize {
    let numerator = u64::from(sample_rate) * u64::from(reference_samples);
    ((numerator + u64::from(TRANSIENT_REFERENCE_RATE / 2)) / u64::from(TRANSIENT_REFERENCE_RATE))
        as usize
}

fn definition_hash(
    sample_rate: u32,
    window_samples: usize,
    hop_samples: usize,
    fft_size: usize,
    mel_bands: usize,
    kind: TransientOdfKind,
) -> [u8; 32] {
    let mut hasher = Sha256::new();
    for field in [
        "kirin-transient-v1",
        kind.as_str(),
        "periodic-hann",
        "window-energy-compensation",
        "single-sided-power",
        "slaney-mel-triangles-no-area-normalization",
        "log10-power",
        "lag-1",
        "complex-prediction-error",
        "hybrid-mel40-plus-4ln1p100complex",
    ] {
        hasher.update(field.as_bytes());
        hasher.update([0]);
    }
    hasher.update(sample_rate.to_le_bytes());
    hasher.update((window_samples as u64).to_le_bytes());
    hasher.update((hop_samples as u64).to_le_bytes());
    hasher.update((fft_size as u64).to_le_bytes());
    hasher.update((mel_bands as u64).to_le_bytes());
    hasher.update(TRANSIENT_MIN_HZ.to_bits().to_le_bytes());
    hasher.update(TRANSIENT_MAX_HZ.to_bits().to_le_bytes());
    hasher.update(TRANSIENT_POWER_EPSILON.to_bits().to_le_bytes());
    hasher.finalize().into()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn standard_rates_keep_one_rational_observation_duration() {
        let expected = [
            (44_100, 941, 235, 1_024),
            (48_000, 1_024, 256, 1_024),
            (88_200, 1_882, 470, 2_048),
            (96_000, 2_048, 512, 2_048),
            (176_400, 3_763, 941, 4_096),
            (192_000, 4_096, 1_024, 4_096),
        ];
        for (rate, window, hop, fft) in expected {
            let layout = TransientLayout::for_rate(rate, TransientOdfKind::Mel40).unwrap();
            assert_eq!(
                (layout.window_samples, layout.hop_samples, layout.fft_size),
                (window, hop, fft)
            );
        }
    }

    #[test]
    fn candidate_and_rate_are_part_of_the_definition_hash() {
        let mel32 = TransientLayout::for_rate(48_000, TransientOdfKind::Mel32).unwrap();
        let mel40 = TransientLayout::for_rate(48_000, TransientOdfKind::Mel40).unwrap();
        let high_rate = TransientLayout::for_rate(96_000, TransientOdfKind::Mel40).unwrap();
        assert_ne!(mel32.definition_hash, mel40.definition_hash);
        assert_ne!(mel40.definition_hash, high_rate.definition_hash);
        assert_eq!(mel40.definition_hex().len(), 64);
    }

    #[test]
    fn sample_rate_boundary_is_explicit() {
        assert!(TransientLayout::for_rate(8_000, TransientOdfKind::Mel32).is_ok());
        assert!(TransientLayout::for_rate(384_000, TransientOdfKind::Mel32).is_ok());
        assert!(TransientLayout::for_rate(7_999, TransientOdfKind::Mel32).is_err());
        assert!(TransientLayout::for_rate(384_001, TransientOdfKind::Mel32).is_err());
    }
}
