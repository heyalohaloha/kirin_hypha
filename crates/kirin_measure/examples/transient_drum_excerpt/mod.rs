//! Shared, candidate-independent excerpt identity for ATTACK DRUM research.

use sha2::{Digest, Sha256};

pub(crate) const EXCERPT_MAPPING_VERSION: &str = "attack-drum-hash-window-v1";
pub(crate) const EXCERPT_WINDOW_DOMAIN: &str = "attack-drum-development-excerpt-window-v1";
pub(crate) const EXCERPT_WINDOW_SEED: &str = "ATTACK-V2-20260830";
pub(crate) const RANK_KEY_DOMAIN: &str = "attack-drum-development-performance-rank-v1";
pub(crate) const RANK_KEY_VERSION: &str = "attack-drum-development-quota-first-v1";
pub(crate) const RANK_KEY_SEED: &str = "ATTACK-V2-20260830";
pub(crate) const EXCERPT_SAMPLE_RATE: u32 = 44_100;
pub(crate) const EXCERPT_CAP_SAMPLES: u64 = 1_323_000;
pub(crate) const EXCERPT_START_QUANTUM_SAMPLES: u64 = 441;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub(crate) struct ExcerptBounds {
    pub(crate) start_sample: u64,
    pub(crate) end_sample: u64,
}

pub(crate) fn performance_rank_key(split: &str, performance_id: &str) -> Result<String, String> {
    if split.is_empty() || performance_id.is_empty() {
        return Err("invalid performance rank identity".to_string());
    }
    Ok(hex::encode(identity_digest(
        RANK_KEY_DOMAIN,
        &[RANK_KEY_VERSION, RANK_KEY_SEED, split, performance_id],
    )))
}

pub(crate) fn decimal_seconds_to_samples_half_up(
    value: &str,
    sample_rate: u64,
) -> Result<u64, String> {
    let mut parts = value.split('.');
    let whole = parts.next().unwrap_or_default();
    let fraction = parts.next();
    if parts.next().is_some()
        || whole.is_empty()
        || !whole.bytes().all(|byte| byte.is_ascii_digit())
        || fraction.is_some_and(|digits| {
            digits.is_empty() || !digits.bytes().all(|byte| byte.is_ascii_digit())
        })
        || sample_rate == 0
    {
        return Err("invalid exact duration decimal".to_string());
    }
    let fraction = fraction.unwrap_or("");
    let denominator = (0..fraction.len()).try_fold(1_u128, |current, _| {
        current
            .checked_mul(10)
            .ok_or_else(|| "duration decimal overflow".to_string())
    })?;
    let parse_digits = |digits: &str| {
        digits.bytes().try_fold(0_u128, |current, byte| {
            current
                .checked_mul(10)
                .and_then(|next| next.checked_add(u128::from(byte - b'0')))
                .ok_or_else(|| "duration decimal overflow".to_string())
        })
    };
    let numerator = parse_digits(whole)?
        .checked_mul(denominator)
        .and_then(|current| current.checked_add(parse_digits(fraction).ok()?))
        .ok_or_else(|| "duration decimal overflow".to_string())?;
    let scaled = numerator
        .checked_mul(u128::from(sample_rate))
        .ok_or_else(|| "duration sample conversion overflow".to_string())?;
    let samples = scaled
        .checked_add(denominator / 2)
        .ok_or_else(|| "duration sample conversion overflow".to_string())?
        / denominator;
    let samples = u64::try_from(samples).map_err(|_| "duration samples exceed u64".to_string())?;
    if samples == 0 {
        return Err("duration rounds to zero samples".to_string());
    }
    Ok(samples)
}

pub(crate) fn excerpt_bounds_44100(
    source_samples: u64,
    split: &str,
    performance_id: &str,
) -> Result<ExcerptBounds, String> {
    if source_samples == 0 || split.is_empty() || performance_id.is_empty() {
        return Err("invalid excerpt identity or source duration".to_string());
    }
    if source_samples <= EXCERPT_CAP_SAMPLES {
        return Ok(ExcerptBounds {
            start_sample: 0,
            end_sample: source_samples,
        });
    }
    let maximum_start = source_samples - EXCERPT_CAP_SAMPLES;
    let position_count = maximum_start / EXCERPT_START_QUANTUM_SAMPLES + 1;
    let hash_u64 = excerpt_hash_u64(split, performance_id);
    let position = multiply_high(hash_u64, position_count);
    let start_sample = position
        .checked_mul(EXCERPT_START_QUANTUM_SAMPLES)
        .ok_or("excerpt start sample overflow")?;
    let end_sample = start_sample
        .checked_add(EXCERPT_CAP_SAMPLES)
        .ok_or("excerpt end sample overflow")?;
    if start_sample % EXCERPT_START_QUANTUM_SAMPLES != 0
        || end_sample > source_samples
        || end_sample - start_sample != EXCERPT_CAP_SAMPLES
    {
        return Err("invalid mapped excerpt bounds".to_string());
    }
    Ok(ExcerptBounds {
        start_sample,
        end_sample,
    })
}

fn excerpt_hash_u64(split: &str, performance_id: &str) -> u64 {
    let digest = identity_digest(
        EXCERPT_WINDOW_DOMAIN,
        &[
            EXCERPT_MAPPING_VERSION,
            EXCERPT_WINDOW_SEED,
            RANK_KEY_VERSION,
            split,
            performance_id,
        ],
    );
    u64::from_be_bytes(digest[..8].try_into().expect("eight hash bytes"))
}

fn identity_digest(domain: &str, fields: &[&str]) -> [u8; 32] {
    let mut hasher = Sha256::new();
    for field in std::iter::once(domain).chain(fields.iter().copied()) {
        hasher.update((field.len() as u64).to_be_bytes());
        hasher.update(field.as_bytes());
    }
    hasher.finalize().into()
}

fn multiply_high(hash_u64: u64, position_count: u64) -> u64 {
    ((u128::from(hash_u64) * u128::from(position_count)) >> 64) as u64
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn short_and_exact_cap_sources_use_the_complete_half_open_range() {
        assert_eq!(
            excerpt_bounds_44100(100, "train", "short").unwrap(),
            ExcerptBounds {
                start_sample: 0,
                end_sample: 100
            }
        );
        assert_eq!(
            excerpt_bounds_44100(EXCERPT_CAP_SAMPLES, "validation", "cap").unwrap(),
            ExcerptBounds {
                start_sample: 0,
                end_sample: EXCERPT_CAP_SAMPLES
            }
        );
        for extra in [1, EXCERPT_START_QUANTUM_SAMPLES - 1] {
            assert_eq!(
                excerpt_bounds_44100(EXCERPT_CAP_SAMPLES + extra, "train", "one-position").unwrap(),
                ExcerptBounds {
                    start_sample: 0,
                    end_sample: EXCERPT_CAP_SAMPLES,
                }
            );
        }
    }

    #[test]
    fn long_mapping_is_quantized_bounded_and_hash_pinned() {
        let source = EXCERPT_CAP_SAMPLES + 10_000_000;
        let first = excerpt_bounds_44100(source, "train", "performance-a").unwrap();
        let second = excerpt_bounds_44100(source, "train", "performance-a").unwrap();
        assert_eq!(first, second);
        assert_eq!(first.start_sample % EXCERPT_START_QUANTUM_SAMPLES, 0);
        assert_eq!(first.end_sample - first.start_sample, EXCERPT_CAP_SAMPLES);
        assert!(first.end_sample <= source);
        assert_eq!(first.start_sample, 9_118_998);
        assert_ne!(
            first,
            excerpt_bounds_44100(source, "train", "performance-b").unwrap()
        );
    }

    #[test]
    fn multiply_high_boundaries_and_invalid_identity_fail_closed() {
        assert_eq!(multiply_high(0, 10), 0);
        assert_eq!(multiply_high(u64::MAX, 10), 9);
        assert!(excerpt_bounds_44100(0, "train", "id").is_err());
        assert!(excerpt_bounds_44100(1, "", "id").is_err());
        assert!(excerpt_bounds_44100(1, "train", "").is_err());
    }

    #[test]
    fn rank_key_is_domain_separated_length_prefixed_and_pinned() {
        assert_eq!(
            performance_rank_key("train", "performance-a").unwrap(),
            "e8d036dd15643ae780463537c7166845b9af5571f7f23a87b1551b243208cee2"
        );
        assert_ne!(
            performance_rank_key("ab", "c").unwrap(),
            performance_rank_key("a", "bc").unwrap()
        );
        assert!(performance_rank_key("", "id").is_err());
    }

    #[test]
    fn exact_decimal_duration_rounds_half_up_without_f64() {
        assert_eq!(
            decimal_seconds_to_samples_half_up("30", 44_100).unwrap(),
            1_323_000
        );
        assert_eq!(
            decimal_seconds_to_samples_half_up("0.005", 44_100).unwrap(),
            221
        );
        assert_eq!(
            decimal_seconds_to_samples_half_up("0.0001", 44_100).unwrap(),
            4
        );
        for invalid in ["1e3", "-1", ".5", "1.", "0", "0.000001"] {
            assert!(decimal_seconds_to_samples_half_up(invalid, 44_100).is_err());
        }
    }
}
