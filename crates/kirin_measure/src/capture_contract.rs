//! Immutable producer limits shared by every Keep entry point.
//!
//! Pair count and readiness timeout are control-plane facts only. Audio retention must never be
//! derived from this wall-clock timeout: offline render advances audio time independently.

use std::time::Duration;

/// Maximum number of PRE/POST pairs in one atomic capture generation.
pub const MAX_CAPTURE_PAIRS: usize = 12;

/// An entry point gives up when no further producer of the generation becomes ready for this
/// long. A slow host that is still bringing producers up keeps the wait open.
pub const CAPTURE_PRODUCER_READY_TIMEOUT: Duration = Duration::from_secs(3);

/// However steadily producers keep becoming ready, the whole wait ends here.
pub const CAPTURE_PRODUCER_READY_LIMIT: Duration = Duration::from_secs(10);

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn capture_pair_limit_is_one_shared_product_contract() {
        assert_eq!(MAX_CAPTURE_PAIRS, 12);
        assert_eq!(CAPTURE_PRODUCER_READY_TIMEOUT, Duration::from_secs(3));
        assert_eq!(CAPTURE_PRODUCER_READY_LIMIT, Duration::from_secs(10));
    }
}
