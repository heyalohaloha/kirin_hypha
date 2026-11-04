//! Which part of the spectrum one analysis covers.
//!
//! LOW is the same analysis with a longer aperture, not a second analyzer: a low note needs more
//! time than the 85 ms window to be told apart from its neighbour. It replaces FULL while it is
//! selected, so one POST still runs a single Spectrum at a time, and it keeps the band count, the
//! cadence and the PRE/POST pairing rules.

use crate::spectrum::{SPECTRUM_MAX_HZ, SPECTRUM_MIN_HZ, SPECTRUM_WINDOW_SIZE};

/// The LOW aperture at the 48 kHz reference: four times FULL, about 341 ms.
pub(super) const SPECTRUM_LOW_WINDOW_SIZE: usize = 16_384;
pub const SPECTRUM_LOW_MIN_HZ: f32 = 20.0;
pub const SPECTRUM_LOW_MAX_HZ: f32 = 300.0;

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
#[repr(u8)]
pub enum SpectrumScope {
    #[default]
    Full = 0,
    Low = 1,
}

impl SpectrumScope {
    pub(crate) fn window_size(self) -> usize {
        match self {
            Self::Full => SPECTRUM_WINDOW_SIZE,
            Self::Low => SPECTRUM_LOW_WINDOW_SIZE,
        }
    }

    pub(crate) fn min_hz(self) -> f32 {
        match self {
            Self::Full => SPECTRUM_MIN_HZ,
            Self::Low => SPECTRUM_LOW_MIN_HZ,
        }
    }

    pub(crate) fn max_hz(self) -> f32 {
        match self {
            Self::Full => SPECTRUM_MAX_HZ,
            Self::Low => SPECTRUM_LOW_MAX_HZ,
        }
    }
}

impl TryFrom<u8> for SpectrumScope {
    type Error = ();

    fn try_from(value: u8) -> Result<Self, Self::Error> {
        match value {
            0 => Ok(Self::Full),
            1 => Ok(Self::Low),
            _ => Err(()),
        }
    }
}

#[cfg(test)]
#[path = "spectrum_scope_tests.rs"]
mod tests;
