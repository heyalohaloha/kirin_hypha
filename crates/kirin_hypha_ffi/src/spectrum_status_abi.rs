//! Additive status tags share the existing one-byte C ABI field.
use super::*;
pub const KIRIN_SPECTRUM_OUTPUT_LATENCY_UNAVAILABLE: u8 = 6;
pub(super) fn spectrum_status_to_abi(status: SpectrumViewStatus) -> u8 {
    match status {
        SpectrumViewStatus::Hidden => KIRIN_SPECTRUM_HIDDEN,
        SpectrumViewStatus::NoPair => KIRIN_SPECTRUM_NO_PAIR,
        SpectrumViewStatus::WarmingUp => KIRIN_SPECTRUM_WARMING_UP,
        SpectrumViewStatus::Active => KIRIN_SPECTRUM_ACTIVE,
        SpectrumViewStatus::Unavailable => KIRIN_SPECTRUM_UNAVAILABLE,
        SpectrumViewStatus::InUse => KIRIN_SPECTRUM_IN_USE,
        SpectrumViewStatus::OutputLatencyUnavailable => KIRIN_SPECTRUM_OUTPUT_LATENCY_UNAVAILABLE,
    }
}
