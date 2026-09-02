use crate::receipt::{CORE_PCM_DOMAIN, GUARD_PCM_DOMAIN, SOURCE_PCM_DOMAIN};

use super::*;

#[test]
fn exact_statistics_cover_signed_24_bit_boundaries() {
    let stats = statistics(&[-8_388_608, -2, 0, 3, 8_388_607]).unwrap();
    assert_eq!(stats.zero_samples, 1);
    assert_eq!(stats.minimum_pcm24, -8_388_608);
    assert_eq!(stats.maximum_pcm24, 8_388_607);
    assert_eq!(stats.peak_abs_pcm24, 8_388_608);
    let expected = 8_388_608_u128.pow(2) + 2_u128.pow(2) + 3_u128.pow(2) + 8_388_607_u128.pow(2);
    assert_eq!(stats.sum_squares_pcm24, expected);
}

#[test]
fn canonical_core_hash_is_relative_and_domain_separated() {
    let first = region_evidence(&[91, 10, -20, 30, 92], 1, 4, CORE_PCM_DOMAIN, 44_100, 1).unwrap();
    let shifted =
        region_evidence(&[1, 2, 10, -20, 30, 3], 2, 5, CORE_PCM_DOMAIN, 44_100, 1).unwrap();
    assert_eq!(first.canonical_sha256, shifted.canonical_sha256);
    let source = region_evidence(&[10, -20, 30], 0, 3, SOURCE_PCM_DOMAIN, 44_100, 1).unwrap();
    let guard = region_evidence(&[10, -20, 30], 0, 3, GUARD_PCM_DOMAIN, 44_100, 1).unwrap();
    assert_ne!(source.canonical_sha256, guard.canonical_sha256);
    assert_eq!(source.statistics, guard.statistics);
}

#[test]
fn metadata_and_content_are_bound_without_absolute_crop_origin() {
    let samples = [10, -20, 30];
    let base = region_evidence(&samples, 0, 3, CORE_PCM_DOMAIN, 44_100, 1).unwrap();
    let rate = region_evidence(&samples, 0, 3, CORE_PCM_DOMAIN, 48_000, 1).unwrap();
    let channels = region_evidence(&samples, 0, 3, CORE_PCM_DOMAIN, 44_100, 2).unwrap();
    let shorter = region_evidence(&samples, 0, 2, CORE_PCM_DOMAIN, 44_100, 1).unwrap();
    assert_ne!(base.canonical_sha256, rate.canonical_sha256);
    assert_ne!(base.canonical_sha256, channels.canonical_sha256);
    assert_ne!(base.canonical_sha256, shorter.canonical_sha256);
}

#[test]
fn invalid_empty_bounds_and_numerators_fail_closed() {
    assert!(region_evidence(&[1], 0, 0, CORE_PCM_DOMAIN, 44_100, 1).is_err());
    assert!(region_evidence(&[1], 0, 2, CORE_PCM_DOMAIN, 44_100, 1).is_err());
    assert!(statistics(&[]).is_err());
    assert!(statistics(&[8_388_608]).is_err());
    assert!(statistics(&[-8_388_609]).is_err());
}
