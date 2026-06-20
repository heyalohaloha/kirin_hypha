//! ISO 532-1 Nonlinear Temporal Decay
//!
//! Simulates the nonlinear temporal decay of the hearing system.
//! Applied to core loudness BEFORE calc_slopes ( order).
//!
//! 24x virtual upsampling with 2-state (uo, u2) system.
//! Time constants: t_short=5ms, t_long=15ms, t_var=75ms
//!
//! Kirin Hypha 移植版（Lens `native/src/psychoacoustic/nonlinear_decay.rs` からアルゴリズム同一移植）。
//! Reference:  loudness_zwtv/_nonlinear_decay.py

use super::tables::*;

/// Compute nonlinear decay on core loudness matrix.
///
/// # Arguments
/// * `core` - Core loudness [frame][21 bands] from core_loudness::compute
///
/// # Returns
/// Decayed core loudness [frame][21 bands]
pub fn compute(core: &[[f64; N_CORE]]) -> Vec<[f64; N_CORE]> {
    let n_frames = core.len();
    if n_frames == 0 {
        return vec![];
    }

    // Pre-compute decay coefficients
    let sample_rate = INTERNAL_FS as f64; // 2000 Hz
    let delta_t = 1.0 / (sample_rate * NL_ITER as f64);
    let p = (NL_T_VAR + NL_T_LONG) / (NL_T_VAR * NL_T_SHORT);
    let q = 1.0 / (NL_T_SHORT * NL_T_VAR);
    let disc = (p * p / 4.0 - q).sqrt();
    let lambda_1 = -p / 2.0 + disc;
    let lambda_2 = -p / 2.0 - disc;
    let den = NL_T_VAR * (lambda_1 - lambda_2);
    let e1 = (lambda_1 * delta_t).exp();
    let e2 = (lambda_2 * delta_t).exp();

    let b = [
        (e1 - e2) / den,                                                             // B[0]
        ((NL_T_VAR * lambda_2 + 1.0) * e1 - (NL_T_VAR * lambda_1 + 1.0) * e2) / den, // B[1]
        ((NL_T_VAR * lambda_1 + 1.0) * e1 - (NL_T_VAR * lambda_2 + 1.0) * e2) / den, // B[2]
        (NL_T_VAR * lambda_1 + 1.0) * (NL_T_VAR * lambda_2 + 1.0) * (e1 - e2) / den, // B[3]
        (-delta_t / NL_T_LONG).exp(),                                                // B[4]
        (-delta_t / NL_T_VAR).exp(),                                                 // B[5]
    ];

    let mut result = vec![[0.0f64; N_CORE]; n_frames];

    // Process each band independently
    for band in 0..N_CORE {
        // Extract this band's time series
        let input: Vec<f64> = core.iter().map(|f| f[band]).collect();

        // Compute deltas between consecutive frames
        let mut delta = vec![0.0f64; n_frames];
        for i in 0..n_frames - 1 {
            delta[i] = (input[i + 1] - input[i]) / NL_ITER as f64;
        }
        // Last delta = (0 - last) / NL_ITER ( rolls and sets last to 0)
        delta[n_frames - 1] = -input[n_frames - 1] / NL_ITER as f64;

        // Virtual upsampling: create NL_ITER sub-samples per frame
        // ui_delta[frame * NL_ITER + sub] = input[frame] + sub * delta[frame]
        let total_sub = n_frames * NL_ITER;
        let mut ui = vec![0.0f64; total_sub];
        for f in 0..n_frames {
            for sub in 0..NL_ITER {
                ui[f * NL_ITER + sub] = input[f] + sub as f64 * delta[f];
            }
        }

        // Initialize uo and u2 arrays
        let mut uo = ui.clone();
        let mut u2 = vec![0.0f64; total_sub];

        // Initialize first element of u2
        if input[0] >= 1e-5 {
            u2[0] = input[0] * (1.0 - b[5]);
        }

        // Process each sub-sample
        for col in 1..total_sub {
            let ui_val = ui[col];
            let uo_prev = uo[col - 1];
            let u2_prev = u2[col - 1];

            // Default: uo = ui (attack/no change)
            // uo[col] already = ui[col] from clone

            // Case 1: uo_prev > u2_prev AND decay path >= ui
            let uo2_decay = uo_prev * b[2] - u2_prev * b[3];
            if uo_prev > u2_prev && uo2_decay >= ui_val {
                uo[col] = uo2_decay;
            }

            // Case 2: uo_prev <= u2_prev AND simple decay >= ui
            let uo2_simple = uo_prev * b[4];
            if uo_prev <= u2_prev && uo2_simple >= ui_val {
                uo[col] = uo2_simple;
            }

            // u2 defaults to uo
            u2[col] = uo[col];

            // u2 case: two-state decay path
            let u22 = uo_prev * b[0] - u2_prev * b[1];
            if ui_val < uo_prev && uo_prev > u2_prev && u22 <= uo[col] {
                u2[col] = u22;
            }

            // u2 case: attack with variable time constant
            let u2_attack = (u2_prev - ui_val) * b[5] + ui_val;
            let near_zero = (ui_val - uo_prev).abs() < 1e-5;
            if ui_val >= uo_prev && !(near_zero && uo[col] <= u2_prev) {
                u2[col] = u2_attack;
            }
        }

        // Extract output: take first sub-sample of each frame
        for f in 0..n_frames {
            result[f][band] = uo[f * NL_ITER];
        }
    }

    result
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_constant_input_unchanged() {
        // Constant core loudness should pass through approximately unchanged
        let n = 100;
        let mut core = vec![[0.0f64; N_CORE]; n];
        for frame in core.iter_mut().take(n) {
            frame[8] = 5.0; // Constant 5 sone/Bark in band 8
        }
        let result = compute(&core);
        // After settling, output should approach input
        let last = result[n - 1][8];
        assert!(
            (last - 5.0).abs() < 0.5,
            "Constant input should approach 5.0, got {last}"
        );
    }

    #[test]
    fn test_empty_input() {
        let result = compute(&[]);
        assert!(result.is_empty());
    }
}
