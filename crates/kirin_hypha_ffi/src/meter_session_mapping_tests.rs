use super::channel_abi::CHANNEL_ROLE_NONE_ABI;
use super::meter_session_ffi::to_c_meter_session;
use super::*;
use kirin_measure::channel_layout::{ChannelLayout, ChannelRole, LayoutId, MAX_ABI_CHANNELS};
use kirin_measure::BalanceState;

#[test]
fn snapshot_layout_and_mapping_are_stable() {
    let current = MeasureResult {
        lufs_m: Some(-14.2),
        lufs_s: Some(-14.8),
        true_peak: Some(-1.1),
        ..MeasureResult::default()
    };
    let snapshot = MeterSessionSnapshot {
        generation: 3,
        state: MeterSessionState::Paused,
        layout: ChannelLayout::stereo(),
        measurement_epoch: 7,
        sample_rate: 48_000,
        active_frames: 96_123,
        observed_frames: 96_000,
        current,
        max_lufs_m: Some(-10.6),
        maximum: MeasureResult::default(),
        summary: SessionSummary {
            lufs_i: Some(-15.0),
            lra: Some(4.2),
            max_true_peak: Some(-0.8),
            layout: None,
        },
        plr: Some(14.2),
        stereo: kirin_measure::StereoMeterSnapshot {
            channels: 2,
            sample_peak_dbfs: std::array::from_fn(|slot| {
                [Some(-1.0), Some(-2.0)].get(slot).copied().flatten()
            }),
            sample_peak_hold_dbfs: std::array::from_fn(|slot| {
                [Some(-0.5), Some(-1.5)].get(slot).copied().flatten()
            }),
            true_peak_dbtp: std::array::from_fn(|slot| {
                [Some(-0.8), Some(-1.8)].get(slot).copied().flatten()
            }),
            instant_true_peak_dbtp: std::array::from_fn(|slot| {
                [Some(-0.9), Some(-1.9)].get(slot).copied().flatten()
            }),
            max_true_peak_dbtp: std::array::from_fn(|slot| {
                [Some(-0.3), Some(-1.3)].get(slot).copied().flatten()
            }),
            vu_dbfs: std::array::from_fn(|slot| {
                [Some(-18.0), Some(-20.0)].get(slot).copied().flatten()
            }),
            clip_events: std::array::from_fn(|slot| [2, 1].get(slot).copied().unwrap_or(0)),
            clip_latched: std::array::from_fn(|slot| {
                [true, false].get(slot).copied().unwrap_or(false)
            }),
            balance_db: Some(0.75),
            balance_state: BalanceState::Numeric,
            correlation: Some(0.91),
            field_density: {
                let mut density = [0; STEREO_FIELD_BINS];
                density[312] = 211;
                density
            },
            field_observation_count: 30,
            mono_sum_db: [Some(-0.75); kirin_measure::mono_sum::MONO_SUM_BAND_COUNT],
            mono_sum_approximate_below_hz: 30.0,
        },
    };
    let mapped = to_c_meter_session(&snapshot);
    assert_eq!(mapped.state, KIRIN_METER_SESSION_PAUSED);
    assert_eq!(mapped.active_frames, 96_123);
    assert_eq!(mapped.observed_frames, 96_000);
    assert_eq!(mapped.lufs_m, -14.2);
    assert_eq!(mapped.max_lufs_m, -10.6);
    assert_eq!(mapped.lufs_s, -14.8);
    assert_eq!(mapped.lufs_i, -15.0);
    assert_eq!(mapped.lra, 4.2);
    assert_eq!(mapped.true_peak, -1.1);
    assert_eq!(mapped.max_true_peak, -0.8);
    assert_eq!(mapped.plr, 14.2);
    assert_eq!(mapped.channels, 2);
    assert_eq!(mapped.layout_id, LayoutId::Stereo.to_abi());
    assert_eq!(mapped.measurement_epoch, 7);
    assert_eq!(mapped.channel_positions[0], ChannelRole::Left.to_abi());
    assert_eq!(mapped.channel_positions[1], ChannelRole::Right.to_abi());
    for slot in 2..MAX_ABI_CHANNELS {
        for value in [
            mapped.sample_peak_dbfs[slot],
            mapped.sample_peak_hold_dbfs[slot],
            mapped.channel_true_peak_dbtp[slot],
            mapped.channel_max_true_peak_dbtp[slot],
            mapped.channel_vu_dbfs[slot],
            mapped.channel_instant_true_peak_dbtp[slot],
        ] {
            assert!(value.is_nan(), "slot {slot}");
        }
        assert_eq!(mapped.channel_positions[slot], CHANNEL_ROLE_NONE_ABI);
        assert_eq!(mapped.clip_events[slot], 0, "slot {slot}");
        assert_eq!(mapped.channel_clip_latched[slot], 0, "slot {slot}");
    }
    assert_eq!(mapped.balance_state, KIRIN_BALANCE_NUMERIC);
    assert_eq!(mapped.sample_peak_dbfs[..2], [-1.0, -2.0]);
    assert_eq!(mapped.channel_vu_dbfs[..2], [-18.0, -20.0]);
    assert_eq!(mapped.channel_instant_true_peak_dbtp[..2], [-0.9, -1.9]);
    assert_eq!(mapped.mono_sum_band_count, KIRIN_MONO_SUM_BAND_COUNT as u8);
    assert_eq!(mapped.mono_sum_reserved, [0; 3]);
    assert_eq!(mapped.mono_sum_approximate_below_hz, 30.0);
    assert!(mapped.mono_sum_db.iter().all(|value| *value == -0.75));

    let mut partial = snapshot.clone();
    partial.stereo.mono_sum_db[0] = None;
    partial.stereo.mono_sum_db[31] = None;
    let mapped = to_c_meter_session(&partial);
    assert_eq!(mapped.mono_sum_band_count, KIRIN_MONO_SUM_BAND_COUNT as u8);
    assert!(mapped.mono_sum_db[0].is_nan());
    assert!(mapped.mono_sum_db[31].is_nan());
    assert_eq!(mapped.mono_sum_db[1], -0.75);

    let mut absent = snapshot.clone();
    absent.stereo.mono_sum_db = [None; kirin_measure::mono_sum::MONO_SUM_BAND_COUNT];
    absent.stereo.mono_sum_approximate_below_hz = 0.0;
    let mapped = to_c_meter_session(&absent);
    assert_eq!(mapped.mono_sum_band_count, 0);
    assert!(mapped.mono_sum_db.iter().all(|value| value.is_nan()));
    assert_eq!(mapped.clip_events[..2], [2, 1]);
    assert_eq!(mapped.channel_clip_latched[..2], [1, 0]);
    assert_eq!(mapped.balance_db, 0.75);
    assert_eq!(mapped.correlation, 0.91);
    assert_eq!(mapped.field_size, KIRIN_STEREO_FIELD_SIZE);
    assert_eq!(mapped.field_observation_count, 30);
    assert_eq!(mapped.field_density[312], 211);

    let mut surround = snapshot;
    surround.layout = ChannelLayout::by_id(LayoutId::Surround5_1);
    surround.stereo.channels = 6;
    surround.stereo.sample_peak_dbfs =
        std::array::from_fn(|slot| (slot < 6).then_some(-1.0 - slot as f64));
    surround.stereo.true_peak_dbtp =
        std::array::from_fn(|slot| (slot < 6).then_some(-2.0 - slot as f64));
    surround.stereo.clip_events =
        std::array::from_fn(|slot| if slot < 6 { (slot + 1) as u64 } else { 0 });
    surround.stereo.balance_db = None;
    surround.stereo.balance_state = BalanceState::Unavailable;
    surround.stereo.correlation = None;
    surround.stereo.field_observation_count = 0;
    let mapped = to_c_meter_session(&surround);
    assert_eq!(mapped.channels, 6);
    assert_eq!(mapped.layout_id, LayoutId::Surround5_1.to_abi());
    assert_eq!(
        mapped.channel_positions[..6],
        [
            ChannelRole::Left.to_abi(),
            ChannelRole::Right.to_abi(),
            ChannelRole::Centre.to_abi(),
            ChannelRole::Lfe.to_abi(),
            ChannelRole::LeftSurround.to_abi(),
            ChannelRole::RightSurround.to_abi(),
        ]
    );
    assert_eq!(
        mapped.sample_peak_dbfs[..6],
        [-1.0, -2.0, -3.0, -4.0, -5.0, -6.0]
    );
    assert_eq!(
        mapped.channel_true_peak_dbtp[..6],
        [-2.0, -3.0, -4.0, -5.0, -6.0, -7.0]
    );
    assert_eq!(mapped.clip_events[..6], [1, 2, 3, 4, 5, 6]);
    assert_eq!(mapped.balance_state, KIRIN_BALANCE_UNAVAILABLE);
    assert!(mapped.balance_db.is_nan());
    assert!(mapped.correlation.is_nan());
}
