//! Immutable producer limits shared by every Keep entry point.
//!
//! Pair count and readiness timeout are control-plane facts only. Audio retention must never be
//! derived from this wall-clock timeout: offline render advances audio time independently.

use std::time::Duration;

/// Maximum number of PRE/POST pairs in one atomic capture generation.
pub const MAX_CAPTURE_PAIRS: usize = 12;

/// Longest time an entry point waits for every producer to acknowledge one generation.
pub const CAPTURE_PRODUCER_READY_TIMEOUT: Duration = Duration::from_secs(3);

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn capture_pair_limit_is_one_shared_product_contract() {
        assert_eq!(MAX_CAPTURE_PAIRS, 12);
        assert_eq!(CAPTURE_PRODUCER_READY_TIMEOUT, Duration::from_secs(3));
    }
}
