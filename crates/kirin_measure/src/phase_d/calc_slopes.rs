//! ISO 532-1 Specific Loudness Calculation (calc_slopes)
//!
//! Converts 21 core loudness bands → 240 specific loudness bins (0.1 Bark)
//! + total loudness N via frequency masking slopes.
//!
//! Algorithm: Zwicker & Fastl (1991) BASIC program, scalar port.
//! Verified against MoSQITo v1.2.1:
//!   1kHz 94dB: N bit-identical, N_spec bit-identical
//!   Pink 60dB: N bit-identical, N_spec max_diff=0.003
//!
//! Kirin Hypha 移植版（Lens `native/src/psychoacoustic/calc_slopes.rs` からアルゴリズム同一移植）。
//! Reference: MoSQITo loudness_zwst/_calc_slopes.py
//! Original: Zwicker & Fastl, J.A.S.J.(E) 12, 1 (1991)

use super::tables::*;

/// Result of specific loudness calculation
pub struct SlopesResult {
    /// Total loudness N(t) for each frame (sone)
    pub n_total: Vec<f64>,
    /// Specific loudness N'(t,z) [frame][240 bins at 0.1 Bark] (sone/Bark)
    pub n_specific: Vec<[f64; N_SPEC_BINS]>,
}

/// A slope segment for N_specific reconstruction
struct Segment {
    z_start: f64,
    z_end: f64,
    n_start: f64,
    slope: f64,
    is_flat: bool,
}

/// Find RNS index: count how many RNS thresholds the value is below (strict <)
#[inline]
fn find_rns_index(value: f64) -> usize {
    let mut count = 0usize;
    for &r in RNS.iter() {
        if value < r { count += 1; }
    }
    count.min(17)
}

/// Find RNS index with <= comparison
#[inline]
fn find_rns_index_eq(value: f64) -> usize {
    let mut count = 0usize;
    for &r in RNS.iter() {
        if value <= r { count += 1; }
    }
    count.min(17)
}

/// Compute specific loudness for all frames.
pub fn compute(core_decayed: &[[f64; N_CORE]]) -> SlopesResult {
    let n_frames = core_decayed.len();
    let mut n_total = vec![0.0f64; n_frames];
    let mut n_specific = vec![[0.0f64; N_SPEC_BINS]; n_frames];

    for t in 0..n_frames {
        let (n, ns) = compute_frame(&core_decayed[t]);
        n_total[t] = n;
        n_specific[t] = ns;
    }

    SlopesResult { n_total, n_specific }
}

/// Compute specific loudness for a single frame using Zwicker algorithm.
fn compute_frame(nm: &[f64; N_CORE]) -> (f64, [f64; N_SPEC_BINS]) {
    let mut segments: Vec<Segment> = Vec::with_capacity(64);
    let mut n_total = 0.0f64;
    let mut z1 = 0.0f64;
    let mut n1 = 0.0f64;

    for i in 0..N_CORE {
        let ig = i;
        let zup_i = ZUP[i];
        let nm_i = nm[i];

        if round8(n1) > round8(nm_i) {
            // === SLOPE CASE: n1 decays toward nm_i ===
            // Initial slope segment
            let j = find_rns_index(n1);
            let mut n2 = RNS[j].max(nm_i);
            let mut sl = usl_value(j, ig).max(1e-10);
            let mut dz = (n1 - n2) / sl;
            let mut z2 = z1 + dz;
            if z2 > zup_i {
                z2 = zup_i;
                dz = z2 - z1;
                n2 = n1 - dz * sl;
            }
            n_total += dz * (n1 + n2) / 2.0;
            segments.push(Segment {
                z_start: z1, z_end: z2, n_start: n1, slope: sl, is_flat: false,
            });

            // Continue slope while z2 < zup_i
            let mut safety = 0u32;
            while round8(z2) < round8(zup_i) && safety < 100 {
                safety += 1;
                let prev_z2 = z2;
                n1 = n2;
                z1 = z2;
                let j2 = find_rns_index_eq(n1);
                n2 = RNS[j2].max(nm_i);
                sl = usl_value(j2, ig).max(1e-10);
                dz = (n1 - n2) / sl;
                z2 = z1 + dz;
                if z2 > zup_i {
                    z2 = zup_i;
                    dz = z2 - z1;
                    n2 = n1 - dz * sl;
                }

                // KEY FIX: When slope exhausted (n1 ~ nm_i, dz ~ 0),
                // fill remaining zone flat at nm_i
                if dz < 1e-10 {
                    let remaining = zup_i - z1;
                    n_total += nm_i * remaining;
                    segments.push(Segment {
                        z_start: z1, z_end: zup_i,
                        n_start: nm_i, slope: 0.0, is_flat: true,
                    });
                    n2 = nm_i;
                    z2 = zup_i;
                    break;
                }

                n_total += dz * (n1 + n2) / 2.0;
                segments.push(Segment {
                    z_start: z1, z_end: z2, n_start: n1, slope: sl, is_flat: false,
                });

                if z2 <= prev_z2 + 1e-10 {
                    break; // No progress safety
                }
            }

            n1 = n2;
            z1 = z2;
        } else {
            // === FLAT CASE: n1 <= nm_i ===
            n_total += nm_i * (zup_i - z1);
            segments.push(Segment {
                z_start: z1, z_end: zup_i,
                n_start: nm_i, slope: 0.0, is_flat: true,
            });
            n1 = nm_i;
            z1 = zup_i;
        }
    }

    // Fill N_specific from recorded segments
    let mut n_spec = [0.0f64; N_SPEC_BINS];
    for (b, ns) in n_spec.iter_mut().enumerate() {
        let z_b = (b as f64 + 1.0) * 0.1; // 0.1, 0.2, ..., 24.0 Bark
        for seg in segments.iter() {
            if z_b > seg.z_start && z_b <= seg.z_end + 1e-10 {
                *ns = if seg.is_flat {
                    seg.n_start
                } else {
                    (seg.n_start - (z_b - seg.z_start) * seg.slope).max(0.0)
                };
                break;
            }
        }
    }

    // Post-process N: ISO 532-1 rounding
    n_total = n_total.max(0.0);
    if n_total <= 16.0 {
        n_total = (n_total * 1000.0 + 0.5).floor() / 1000.0;
    } else {
        n_total = (n_total * 100.0 + 0.5).floor() / 100.0;
    }

    (n_total, n_spec)
}

/// Round to 8 decimal places (matching MoSQITo comparison precision)
#[inline]
fn round8(x: f64) -> f64 {
    (x * 1e8).round() / 1e8
}
