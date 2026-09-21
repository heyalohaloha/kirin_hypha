//! `KirinMeterHistoryEntry` の C ABI 定義。正本は `include/kirin_hypha_meter_history_ffi.h`。
//!
//! B-962 で `clip_event_count` を `[2]` から `[MAX_ABI_CHANNELS]` へ広げ、`measurement_epoch` を
//! 先頭へ置いた。`generation` も `run_id` も session ごとに 1 から数え直すので、区間をまたいだ
//! 継続かどうかを言えるのは `measurement_epoch` だけである（D-12）。

use kirin_measure::channel_layout::MAX_ABI_CHANNELS;

/// TIME履歴1指標の範囲。10 Hzではmin=max=mean、値なしはNaN。
#[repr(C)]
pub struct KirinMeterHistoryRange {
    pub min: f64,
    pub max: f64,
    pub mean: f64,
}

/// TIME履歴の1点。低rate層は`observation_count`個の100 ms事実を集約する。
#[repr(C)]
pub struct KirinMeterHistoryEntry {
    /// どの測定区間の点か。`generation` も `run_id` も session ごとに 1 から数え直すので、
    /// これだけが区間をまたいだ継続かどうかを言える（D-12）。
    pub measurement_epoch: u64,
    pub generation: u64,
    pub run_id: u64,
    pub first_observed_frames: u64,
    pub last_observed_frames: u64,
    pub first_timeline_endpoint_samples: i64,
    pub last_timeline_endpoint_samples: i64,
    pub observation_count: u16,
    pub resolution: u8,
    pub reserved: u8,
    /// 入力チャンネルごと。`KirinMeterSession.channels` 以降のスロットは測定を持たない。
    pub clip_event_count: [u32; MAX_ABI_CHANNELS],
    pub lufs_m: KirinMeterHistoryRange,
    pub lufs_s: KirinMeterHistoryRange,
    pub true_peak: KirinMeterHistoryRange,
    pub correlation: KirinMeterHistoryRange,
    pub plr: KirinMeterHistoryRange,
}

#[cfg(test)]
mod tests {
    use crate::to_c_history_entry;
    use crate::KIRIN_METER_HISTORY_1_HZ;
    use kirin_measure::{
        CaptureClockSource, MeterHistoryEntry, MeterHistoryRange, MeterHistoryResolution,
    };

    #[test]
    fn a_history_point_crosses_the_abi_with_its_span_and_its_measured_channels() {
        let history = to_c_history_entry(MeterHistoryEntry {
            resolution: MeterHistoryResolution::Hz1,
            measurement_epoch: 11,
            generation: 3,
            run_id: 7,
            observation_count: 10,
            first_observed_frames: 4_800,
            last_observed_frames: 48_000,
            first_timeline_endpoint_samples: Some(104_800),
            last_timeline_endpoint_samples: None,
            timeline_source: CaptureClockSource::ProjectTimeline,
            clip_event_count: [3, 1],
            lufs_m: MeterHistoryRange {
                min: Some(-16.0),
                max: Some(-13.0),
                mean: Some(-14.5),
            },
            lufs_s: MeterHistoryRange::default(),
            true_peak: MeterHistoryRange::default(),
            correlation: MeterHistoryRange::default(),
            plr: MeterHistoryRange {
                min: Some(12.0),
                max: Some(14.0),
                mean: Some(13.0),
            },
        });
        assert_eq!(history.resolution, KIRIN_METER_HISTORY_1_HZ);
        assert_eq!(history.observation_count, 10);
        assert_eq!(history.clip_event_count[..2], [3, 1]);
        assert!(history.clip_event_count[2..].iter().all(|v| *v == 0));
        assert_eq!(history.first_timeline_endpoint_samples, 104_800);
        assert_eq!(history.plr.mean, 13.0);
        assert_eq!(history.last_timeline_endpoint_samples, i64::MIN);
        assert_eq!(history.lufs_m.min, -16.0);
        assert!(history.lufs_s.mean.is_nan());
        // 区間は値の身元である。番号が同じでも区間が違えば同じ測定ではない。
        assert_eq!(history.measurement_epoch, 11);
        assert_eq!(history.generation, 3);
        assert_eq!(history.run_id, 7);
    }
}
