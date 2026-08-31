use sha2::{Digest, Sha256};

use crate::receipt::{PcmRegionEvidence, PcmStatistics};

pub(crate) fn region_evidence(
    samples: &[i32],
    start: u64,
    end: u64,
    domain: &str,
    sample_rate: u32,
    channels: u16,
) -> Result<PcmRegionEvidence, String> {
    if start >= end {
        return Err("canonical PCM region must be nonempty".to_string());
    }
    let start_index = usize::try_from(start).map_err(|_| "PCM region start is too large")?;
    let end_index = usize::try_from(end).map_err(|_| "PCM region end is too large")?;
    let values = samples
        .get(start_index..end_index)
        .ok_or("canonical PCM region exceeds decoded audio")?;
    let mut hasher = pcm_hasher(domain, sample_rate, channels, values.len());
    let statistics = hash_values(values, &mut [&mut hasher])?;
    Ok(PcmRegionEvidence {
        start_sample_44100: start,
        end_sample_44100: end,
        samples: values.len() as u64,
        canonical_sha256: hex::encode(hasher.finalize()),
        statistics,
    })
}

pub(crate) fn full_source_and_guard_evidence(
    samples: &[i32],
    source_domain: &str,
    guard_domain: &str,
    sample_rate: u32,
    channels: u16,
) -> Result<(PcmRegionEvidence, PcmRegionEvidence), String> {
    if samples.is_empty() {
        return Err("full PCM evidence requires samples".to_string());
    }
    let mut source = pcm_hasher(source_domain, sample_rate, channels, samples.len());
    let mut guard = pcm_hasher(guard_domain, sample_rate, channels, samples.len());
    let statistics = hash_values(samples, &mut [&mut source, &mut guard])?;
    let region = |canonical_sha256| PcmRegionEvidence {
        start_sample_44100: 0,
        end_sample_44100: samples.len() as u64,
        samples: samples.len() as u64,
        canonical_sha256,
        statistics,
    };
    Ok((
        region(hex::encode(source.finalize())),
        region(hex::encode(guard.finalize())),
    ))
}

fn pcm_hasher(domain: &str, sample_rate: u32, channels: u16, samples: usize) -> Sha256 {
    let mut hasher = Sha256::new();
    hasher.update((domain.len() as u64).to_be_bytes());
    hasher.update(domain.as_bytes());
    hasher.update(sample_rate.to_be_bytes());
    hasher.update(channels.to_be_bytes());
    hasher.update((samples as u64).to_be_bytes());
    hasher
}

fn hash_values(values: &[i32], hashers: &mut [&mut Sha256]) -> Result<PcmStatistics, String> {
    let first = *values.first().ok_or("PCM statistics require samples")?;
    let mut minimum = first;
    let mut maximum = first;
    let mut zero_samples = 0_u64;
    let mut sum_squares = 0_u128;
    let mut encoded = Vec::with_capacity(32 * 1024);
    for value in values {
        if !(-8_388_608..=8_388_607).contains(value) {
            return Err("decoded PCM numerator is outside signed 24-bit range".to_string());
        }
        minimum = minimum.min(*value);
        maximum = maximum.max(*value);
        zero_samples += u64::from(*value == 0);
        let magnitude = i64::from(*value).unsigned_abs();
        sum_squares = sum_squares
            .checked_add(u128::from(magnitude) * u128::from(magnitude))
            .ok_or("PCM sum-of-squares overflow")?;
        encoded.extend_from_slice(&value.to_be_bytes());
        if encoded.len() >= 32 * 1024 {
            for hasher in hashers.iter_mut() {
                hasher.update(&encoded);
            }
            encoded.clear();
        }
    }
    if !encoded.is_empty() {
        for hasher in hashers {
            hasher.update(&encoded);
        }
    }
    let peak = i64::from(minimum)
        .unsigned_abs()
        .max(i64::from(maximum).unsigned_abs());
    Ok(PcmStatistics {
        zero_samples,
        minimum_pcm24: minimum,
        maximum_pcm24: maximum,
        peak_abs_pcm24: peak as u32,
        sum_squares_pcm24: sum_squares,
    })
}

#[cfg(test)]
pub(crate) fn statistics(samples: &[i32]) -> Result<PcmStatistics, String> {
    let first = *samples.first().ok_or("PCM statistics require samples")?;
    let mut minimum = first;
    let mut maximum = first;
    let mut zero_samples = 0_u64;
    let mut sum_squares = 0_u128;
    for value in samples {
        if !(-8_388_608..=8_388_607).contains(value) {
            return Err("PCM statistic value is outside signed 24-bit range".to_string());
        }
        minimum = minimum.min(*value);
        maximum = maximum.max(*value);
        zero_samples = zero_samples
            .checked_add(u64::from(*value == 0))
            .ok_or("PCM zero count overflow")?;
        let magnitude = i64::from(*value).unsigned_abs();
        let square = u128::from(magnitude)
            .checked_mul(u128::from(magnitude))
            .ok_or("PCM square overflow")?;
        sum_squares = sum_squares
            .checked_add(square)
            .ok_or("PCM sum-of-squares overflow")?;
    }
    let peak_abs = i64::from(minimum)
        .unsigned_abs()
        .max(i64::from(maximum).unsigned_abs());
    Ok(PcmStatistics {
        zero_samples,
        minimum_pcm24: minimum,
        maximum_pcm24: maximum,
        peak_abs_pcm24: u32::try_from(peak_abs).map_err(|_| "PCM peak overflow")?,
        sum_squares_pcm24: sum_squares,
    })
}

#[cfg(test)]
#[path = "pcm_tests.rs"]
mod tests;
