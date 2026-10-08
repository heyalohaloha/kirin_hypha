//! Canonical band meaning descriptor. Display precision and language do not enter the hash.
use sha2::{Digest, Sha256};
use std::sync::OnceLock;

pub const BAND_SEMANTIC_VERSION: u32 = 1;

pub fn band_semantic_bytes() -> Vec<u8> {
    let mut bytes = b"KirinHypha/BandMeaning\0".to_vec();
    use crate::attack_perception::band::*;
    // Stable u32 wire order: version, RBJ filter family, second-order section order,
    // settle periods, centre-unity normalization, base-two octave series, HEAD lead/span,
    // peak search, TAIL limit, HEAD/TAIL cell counts, linear crossing, first-peak tie,
    // half-open span convention, algorithm revision. Language and display precision are absent.
    const RBJ_FILTER_FAMILY: u32 = 1;
    const BIQUAD_ORDER: u32 = 2;
    const CENTRE_UNITY: u32 = 1;
    const BASE_TWO_OCTAVES: u32 = 1;
    const LINEAR_CROSSING: u32 = 1;
    const FIRST_PEAK_TIE: u32 = 1;
    const HALF_OPEN_SPAN: u32 = 1;
    const ALGORITHM_REVISION: u32 = 1;
    for value in [
        BAND_SEMANTIC_VERSION,
        RBJ_FILTER_FAMILY,
        BIQUAD_ORDER,
        SETTLE_PERIODS as u32,
        CENTRE_UNITY,
        BASE_TWO_OCTAVES,
        ATTACK_BAND_HEAD_LEAD_MICROS as u32,
        ATTACK_BAND_HEAD_SPAN_MICROS as u32,
        ATTACK_BAND_PEAK_SEARCH_MICROS as u32,
        ATTACK_BAND_TAIL_MICROS as u32,
        ATTACK_BAND_HEAD_POINTS as u32,
        ATTACK_BAND_TAIL_POINTS as u32,
        LINEAR_CROSSING,
        FIRST_PEAK_TIE,
        HALF_OPEN_SPAN,
        ALGORITHM_REVISION,
    ] {
        bytes.extend_from_slice(&value.to_le_bytes());
    }
    // Stable f64 wire order: base centre, octave ratio, Nyquist clamp, presence floor,
    // rise threshold, lower/upper ATT crossing, REL crossing and plotting floor.
    for value in [
        BASE_CENTRE_HZ,
        2.0,
        NYQUIST_FRACTION,
        f64::from(ATTACK_BAND_PRESENCE_FLOOR_DBFS),
        f64::from(ATTACK_BAND_RISE_DB),
        ARRIVAL_AMPLITUDE_RATIO,
        ATTACK_UPPER_AMPLITUDE_RATIO,
        RELEASE_AMPLITUDE_RATIO,
        f64::from(crate::ATTACK_LEVEL_FLOOR_DBFS),
    ] {
        bytes.extend_from_slice(&value.to_bits().to_le_bytes());
    }
    bytes.extend_from_slice(
        b"RBJ-DF1-Butterworth-HP-LP;centre-unity;RMS-nearest-period-centred;\
        settle-4-period;peak-first;linear-crossing;half-open;next-hit-exclusive;\
        audio-end-needs-peak-and-RMS-tail;grid-cell-midpoint;algorithm-1",
    );
    bytes
}

pub fn band_semantic_hash() -> [u8; 32] {
    static HASH: OnceLock<[u8; 32]> = OnceLock::new();
    *HASH.get_or_init(|| Sha256::digest(band_semantic_bytes()).into())
}

#[cfg(test)]
#[path = "attack_band_semantics_tests.rs"]
mod tests;
