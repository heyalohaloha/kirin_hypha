//! Display-only PSB: twenty equal 1.2 Bark groups over the complete 0–24 Bark
//! specific-loudness field. These shares are NOT the persisted Record PSB.

pub const PSB_GROUPS: usize = 20;
pub type PsbShares = [f64; PSB_GROUPS];

#[derive(Clone, Copy, Debug)]
pub struct DisplayObservation {
    pub sharpness: f64,
    /// Unnormalised specific-loudness contributions, combined across channels first.
    pub specific: PsbShares,
}

impl DisplayObservation {
    pub fn shares(&self) -> Option<PsbShares> {
        if self.specific.iter().any(|v| !v.is_finite() || *v < 0.0) {
            return None;
        }
        let total: f64 = self.specific.iter().sum();
        if !total.is_finite() || total <= 1e-12 {
            return None;
        }
        Some(self.specific.map(|value| value / total))
    }
}

pub fn valid_shares(shares: &PsbShares) -> bool {
    shares
        .iter()
        .all(|v| v.is_finite() && (0.0..=1.0).contains(v))
        && (shares.iter().sum::<f64>() - 1.0).abs() <= 1e-9
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn shares_normalise_without_fabricating_silence_or_invalid_values() {
        let mut observation = DisplayObservation {
            sharpness: 1.0,
            specific: [2.0; 20],
        };
        assert_eq!(observation.shares(), Some([0.05; 20]));
        assert!(valid_shares(&observation.shares().unwrap()));
        observation.specific = [0.0; 20];
        assert!(observation.shares().is_none());
        for invalid in [f64::NAN, f64::INFINITY, -0.1] {
            observation.specific[0] = invalid;
            assert!(observation.shares().is_none());
        }
        assert!(!valid_shares(&[0.0; 20]));
        assert!(!valid_shares(&[0.04; 20]));
    }
}
