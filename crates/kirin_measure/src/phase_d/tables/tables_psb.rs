//! PSB-only Bark 21–24 extension from Zwicker (1961).
//!
//! ISO 532-1 defines the specific-loudness pipeline through Bark 20. The extra bands use FFT
//! linear power and must not be added to or compared directly with the Bark 1–20 sone/Bark values.

/// Number of Bark bands in the PSB output definition.
pub const N_BARK_PSB: usize = 24;

/// PSB Bark-band centre frequencies. Bark 1–20 match the ISO 532-1 table; Bark 21–24 extend it.
pub const BARK_CENTER_HZ_PSB: [f64; N_BARK_PSB] = [
    50.0, 150.0, 250.0, 350.0, 450.0, 570.0, 700.0, 840.0, 1000.0, 1170.0, 1370.0, 1600.0, 1850.0,
    2150.0, 2500.0, 2900.0, 3400.0, 4000.0, 4800.0, 5800.0, 7000.0, 8500.0, 10500.0, 13500.0,
];

/// Upper edge of each PSB Bark band in Hz, used by FFT binning.
pub const BARK_UPPER_HZ_PSB: [f64; N_BARK_PSB] = [
    100.0, 200.0, 300.0, 400.0, 510.0, 630.0, 770.0, 920.0, 1080.0, 1270.0, 1480.0, 1720.0, 2000.0,
    2320.0, 2700.0, 3150.0, 3700.0, 4400.0, 5300.0, 6400.0, 7700.0, 9500.0, 12000.0, 15500.0,
];
