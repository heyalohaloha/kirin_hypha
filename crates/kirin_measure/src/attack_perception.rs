//! Worker-side descriptors for one ATTACK hit.
//!
//! The descriptors are factual observations, not quality judgements. Every window starts at the
//! first sample of the about-1 ms content bin that contains the onset, so PRE and POST measured at
//! one onset read exactly the same content samples. Stereo is aggregated as mean linear power; no
//! downmix is used.
//!
//! A hit is published in two stages. Once its 30 ms head is measured it carries STRENGTH and
//! CREST; once its body, next onset and Sharpness window are final it is complete. A hit whose
//! audio stops first (the transport stopped) keeps its head values and never invents the rest.

pub const ATTACK_BIN_MICROS: u32 = 1_000;
/// Head: the first 30 ms, whose RMS is STRENGTH.
pub const ATTACK_HEAD_BINS: i64 = 30;
/// Body: the 100 ms after the head, cut at the next onset when that comes first.
pub const ATTACK_BODY_BINS: i64 = 100;
/// A body shorter than 20 ms (the next hit follows too closely) has no TRANSIENT.
pub const ATTACK_MIN_BODY_BINS: i64 = 20;
/// Sharpness reads the 100 ms from the window start.
pub const ATTACK_SHARPNESS_BINS: i64 = 100;
/// The event shape starts 20 ms before the window so the onset is visible.
pub const ATTACK_SHAPE_LEAD_BINS: i64 = 20;
pub const ATTACK_LEVEL_FLOOR_DBFS: f32 = -120.0;

/// Samples in one measurement bin: the rate's whole-sample approximation of 1 ms.
pub fn attack_bin_frames(sample_rate: u32) -> i64 {
    ((i64::from(sample_rate) * i64::from(ATTACK_BIN_MICROS) + 500_000) / 1_000_000).max(1)
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct AttackPerceptualFeatures {
    pub sample_rate: u32,
    pub channels: u8,
    pub bin_frames: u32,
    /// First sample of the bin containing the onset: the start of the head and Sharpness windows.
    pub window_start_sample: i64,
    /// Head RMS (STRENGTH).
    pub attack_rms_dbfs: f32,
    /// Largest absolute head sample of any channel.
    pub sample_peak_dbfs: f32,
    /// Head sample peak minus head RMS (CREST).
    pub crest_db: f32,
    /// False while only the head is measured: the body end is then the head end, and the body,
    /// TRANSIENT and Sharpness are `None`.
    pub complete: bool,
    /// Exclusive end of the body window; PRE and POST share it for one pair.
    pub body_end_sample: i64,
    /// Body RMS; `None` when the next onset leaves less than 20 ms of body.
    pub body_rms_dbfs: Option<f32>,
    /// Head RMS minus body RMS (TRANSIENT); present exactly when the body is.
    pub transient_db: Option<f32>,
    /// Loudness-weighted DIN 45692 Sharpness of the 100 ms from the window start, ignoring
    /// Phase D frames below 0.1 sone; `None` when nothing there is loud enough, before Phase D
    /// settles in a run, or while the hit is not complete.
    pub sharpness_acum: Option<f32>,
}

impl AttackPerceptualFeatures {
    pub fn has_valid_layout(&self) -> bool {
        let bin = i64::from(self.bin_frames);
        let head_end = self.window_start_sample + ATTACK_HEAD_BINS * bin;
        self.sample_rate > 0
            && matches!(self.channels, 1 | 2)
            && bin == attack_bin_frames(self.sample_rate)
            && self.window_start_sample.rem_euclid(bin) == 0
            && self.body_end_sample.rem_euclid(bin) == 0
            && (head_end..=head_end + ATTACK_BODY_BINS * bin).contains(&self.body_end_sample)
            && (self.complete
                || (self.body_end_sample == head_end && self.sharpness_acum.is_none()))
            && [self.attack_rms_dbfs, self.sample_peak_dbfs, self.crest_db]
                .into_iter()
                .all(f32::is_finite)
            && self.transient_db.is_some() == self.body_rms_dbfs.is_some()
            && self.body_rms_dbfs.is_some()
                == (self.body_end_sample - head_end >= ATTACK_MIN_BODY_BINS * bin)
            && self
                .body_rms_dbfs
                .into_iter()
                .chain(self.transient_db)
                .all(f32::is_finite)
            && self
                .sharpness_acum
                .is_none_or(|value| value.is_finite() && value >= 0.0)
    }

    /// True when `event_sample` lies in the first bin of these windows.
    pub fn starts_at(&self, event_sample: i64) -> bool {
        let bin = i64::from(self.bin_frames);
        bin > 0 && event_sample.div_euclid(bin) * bin == self.window_start_sample
    }
}

#[cfg(test)]
#[path = "attack_perception_tests.rs"]
mod tests;
