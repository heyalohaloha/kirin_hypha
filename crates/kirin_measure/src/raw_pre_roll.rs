//! Bounded raw-audio history used only to bridge a Keep control-plane handoff.
//!
//! The Audio Thread still writes the existing fixed SPSC lanes. Measure Thread owns this buffer,
//! stores raw f32 samples before Watch DSP, and moves the exact timestamp-selected suffix into the
//! normal Record consumer path. Watch metrics are never copied into Record.

use std::collections::VecDeque;

#[derive(Debug)]
pub(crate) struct RawPreRollHistory {
    samples: VecDeque<f32>,
    capacity_samples: usize,
    channels: usize,
    epoch: Option<u64>,
    start_capture_frame: Option<u64>,
    end_capture_frame: Option<u64>,
}

#[derive(Debug)]
pub(crate) struct RawRecordPrefix {
    pub samples: VecDeque<f32>,
    pub start_capture_frame: u64,
    pub epoch: u64,
}

impl RawPreRollHistory {
    pub(crate) fn new(capacity_samples: usize, channels: usize) -> Self {
        let channels = channels.max(1);
        let capacity_samples = capacity_samples.max(channels) / channels * channels;
        Self {
            samples: VecDeque::with_capacity(capacity_samples),
            capacity_samples,
            channels,
            epoch: None,
            start_capture_frame: None,
            end_capture_frame: None,
        }
    }

    pub(crate) fn clear(&mut self) {
        self.samples.clear();
        self.epoch = None;
        self.start_capture_frame = None;
        self.end_capture_frame = None;
    }

    /// Append one fully admitted Measure chunk. `samples` originated as f32 and was widened to
    /// f64 for DSP; f64→f32 is therefore bit-exact for this history copy.
    pub(crate) fn append_f64(
        &mut self,
        epoch: Option<u64>,
        capture_start_frame: u64,
        samples: &[f64],
    ) -> bool {
        let Some(epoch) = epoch.filter(|epoch| *epoch > 0) else {
            self.clear();
            return false;
        };
        if samples.is_empty() || !samples.len().is_multiple_of(self.channels) {
            return false;
        }
        let frames = (samples.len() / self.channels) as u64;
        let contiguous = self.epoch == Some(epoch)
            && self.end_capture_frame == Some(capture_start_frame)
            && self.start_capture_frame.is_some();
        if !contiguous {
            self.clear();
            self.epoch = Some(epoch);
            self.start_capture_frame = Some(capture_start_frame);
            self.end_capture_frame = Some(capture_start_frame);
        }

        if samples.len() > self.capacity_samples {
            // The advertised maximum callback plus notification window makes this unreachable for
            // supported hosts. Keep only the complete aligned tail if a caller violates it.
            self.clear();
            let keep = self.capacity_samples;
            let skip = samples.len().saturating_sub(keep);
            let skipped_frames = (skip / self.channels) as u64;
            self.epoch = Some(epoch);
            self.start_capture_frame = Some(capture_start_frame.saturating_add(skipped_frames));
            self.end_capture_frame = self.start_capture_frame;
            for sample in &samples[skip..] {
                self.samples.push_back(*sample as f32);
            }
        } else {
            let required = self.samples.len().saturating_add(samples.len());
            if required > self.capacity_samples {
                let evict = required.saturating_sub(self.capacity_samples);
                let evict = evict.div_ceil(self.channels) * self.channels;
                for _ in 0..evict.min(self.samples.len()) {
                    let _ = self.samples.pop_front();
                }
                let evicted_frames = (evict / self.channels) as u64;
                self.start_capture_frame = self
                    .start_capture_frame
                    .map(|start| start.saturating_add(evicted_frames));
            }
            for sample in samples {
                self.samples.push_back(*sample as f32);
            }
        }
        self.end_capture_frame = Some(capture_start_frame.saturating_add(frames));
        true
    }

    /// Move, rather than copy, the producer-positioned suffix `[epoch_start, record_origin)` into
    /// Record. The Watch span may have generation 0 while the promoted Record alias has the final
    /// generation/epoch, so source identity is proven by exact capture-frame coverage and
    /// continuity, then retagged to `record_epoch`. No curve or Watch metric participates.
    pub(crate) fn take_positioned_prefix(
        &mut self,
        epoch_start_capture_frame: u64,
        record_origin_capture_frame: u64,
        record_epoch: u64,
    ) -> Option<RawRecordPrefix> {
        if self.epoch.is_none()
            || record_epoch == 0
            || record_origin_capture_frame <= epoch_start_capture_frame
        {
            self.clear();
            return None;
        }
        let history_start = self.start_capture_frame?;
        let history_end = self.end_capture_frame?;
        if history_start > epoch_start_capture_frame || history_end < record_origin_capture_frame {
            self.clear();
            return None;
        }

        let front_frames = epoch_start_capture_frame.saturating_sub(history_start) as usize;
        let back_frames = history_end.saturating_sub(record_origin_capture_frame) as usize;
        for _ in 0..front_frames.saturating_mul(self.channels) {
            let _ = self.samples.pop_front();
        }
        for _ in 0..back_frames.saturating_mul(self.channels) {
            let _ = self.samples.pop_back();
        }
        let samples = std::mem::take(&mut self.samples);
        self.epoch = None;
        self.start_capture_frame = None;
        self.end_capture_frame = None;
        (!samples.is_empty()).then_some(RawRecordPrefix {
            samples,
            start_capture_frame: epoch_start_capture_frame,
            epoch: record_epoch,
        })
    }

    /// Reuse the allocation moved into a completed prefix; no new full-capacity allocation is
    /// made when the next Watch session starts.
    pub(crate) fn reclaim_prefix_storage(&mut self, mut samples: VecDeque<f32>) {
        samples.clear();
        self.samples = samples;
        self.epoch = None;
        self.start_capture_frame = None;
        self.end_capture_frame = None;
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn exact_epoch_suffix_is_moved_without_copying_another_history() {
        let mut history = RawPreRollHistory::new(16, 2);
        let raw = (0..12).map(f64::from).collect::<Vec<_>>();
        assert!(history.append_f64(Some(7), 100, &raw));
        let mut prefix = history
            .take_positioned_prefix(102, 105, 11)
            .expect("three exact frames");
        assert_eq!(prefix.start_capture_frame, 102);
        assert_eq!(prefix.epoch, 11);
        assert_eq!(
            prefix.samples.drain(..).collect::<Vec<_>>(),
            vec![4., 5., 6., 7., 8., 9.]
        );
    }

    #[test]
    fn epoch_change_drops_old_watch_audio_instead_of_joining_passes() {
        let mut history = RawPreRollHistory::new(16, 2);
        assert!(history.append_f64(Some(1), 0, &[1.0, 1.0, 2.0, 2.0]));
        assert!(history.append_f64(Some(2), 2, &[3.0, 3.0, 4.0, 4.0]));
        let prefix = history.take_positioned_prefix(2, 4, 20).unwrap();
        assert_eq!(
            prefix.samples.into_iter().collect::<Vec<_>>(),
            vec![3.0, 3.0, 4.0, 4.0]
        );
    }

    #[test]
    fn circular_eviction_remains_frame_aligned() {
        let mut history = RawPreRollHistory::new(8, 2);
        assert!(history.append_f64(Some(3), 10, &[0.0, 1.0, 2.0, 3.0, 4.0, 5.0]));
        assert!(history.append_f64(Some(3), 13, &[6.0, 7.0, 8.0, 9.0]));
        let prefix = history.take_positioned_prefix(11, 15, 30).unwrap();
        assert_eq!(
            prefix.samples.into_iter().collect::<Vec<_>>(),
            vec![2., 3., 4., 5., 6., 7., 8., 9.]
        );
    }

    #[test]
    fn missing_epoch_start_refuses_an_inexact_prefix() {
        let mut history = RawPreRollHistory::new(4, 1);
        assert!(history.append_f64(Some(9), 0, &[0.0, 1.0, 2.0, 3.0, 4.0]));
        assert!(history.take_positioned_prefix(0, 5, 90).is_none());
    }

    #[test]
    fn delayed_keep_ack_replays_exact_7676_sample_prefix_into_promoted_epoch() {
        const DELAY_FRAMES: usize = 7_676;
        let mut history = RawPreRollHistory::new((DELAY_FRAMES + 64) * 2, 2);
        let raw = (0..DELAY_FRAMES * 2)
            .map(|sample| (sample as f32 / 32_768.0) as f64)
            .collect::<Vec<_>>();
        assert!(history.append_f64(Some(4), 50_000, &raw));
        let prefix = history
            .take_positioned_prefix(50_000, 50_000 + DELAY_FRAMES as u64, 5)
            .expect("the complete timestamped ACK gap is retained");
        assert_eq!(prefix.start_capture_frame, 50_000);
        assert_eq!(prefix.epoch, 5);
        assert_eq!(prefix.samples.len(), DELAY_FRAMES * 2);
        assert_eq!(prefix.samples.front().copied(), Some(0.0));
        assert_eq!(
            prefix.samples.back().copied(),
            Some((DELAY_FRAMES * 2 - 1) as f32 / 32_768.0)
        );
    }
}
