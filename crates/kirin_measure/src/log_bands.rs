//! The log band edges shared by every band split in the product.
//!
//! FREQ uses 256 of them and SPACE's MONO uses 32. Both have to answer "which frequencies is this
//! band" the same way, so the formula lives here rather than once per caller.

/// The low and high edge of one band, in Hz.
///
/// Bands tile the range without gaps or overlap: band `i`'s high edge is band `i + 1`'s low edge,
/// band 0 starts at `min_hz` and band `band_count - 1` ends at `max_hz`.
pub fn log_band_edges(index: usize, band_count: usize, min_hz: f32, max_hz: f32) -> (f32, f32) {
    let ratio = max_hz / min_hz;
    let edge = |offset: usize| min_hz * ratio.powf((index + offset) as f32 / band_count as f32);
    (edge(0), edge(1))
}
