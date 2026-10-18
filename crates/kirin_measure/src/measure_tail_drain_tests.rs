//! B-132 (G-115-382): drain-completion barrier 感度確証。

use super::{
    drain_ring_into_session, trusted_pre_roll_epoch, CaptureChunkPlan, ChannelLayout,
    DrainRingSession, MeasureSampleConsumer,
};
use crate::engine::MeasureEngine;
use crate::phase_d::channels::PhaseDChannelStream;
use crate::phase_d::tables::FieldType;
use crate::record_take::{CaptureClockSource, RecordTakeTracker};
use std::sync::{Arc, Mutex};

const SR: u32 = 48_000;

/// interleaved stereo f32 を `n_frames` 分・振幅 `amp` の 1kHz 正弦で生成。
fn sine_stereo(n_frames: usize, amp: f32) -> Vec<f32> {
    let mut v = Vec::with_capacity(n_frames * 2);
    for i in 0..n_frames {
        let s = amp * (2.0 * std::f32::consts::PI * 1000.0 * (i as f32) / SR as f32).sin();
        v.push(s); // L
        v.push(s); // R
    }
    v
}

fn to_f64(f32s: &[f32]) -> Vec<f64> {
    f32s.iter().map(|&s| s as f64).collect()
}

/// 感度確証（barrier OFF vs ON）: 録音末尾に **より大きい peak を持つ tail** を残した状態で
/// transport-stop 相当を起こす。
/// - OFF（旧挙動 = ring を finalize せず破棄）: max_true_peak が body だけになり full と乖離（abs≠0 / 取りこぼし再現）。
/// - ON（drain_ring_into_session = barrier）: tail を engine に算入して finalize → full と一致（abs<1e-6）。
///
/// これは「変えるのは finalize の消費サンプル集合（tail 算入）」であり per-sample DSP 不変
/// （engine.push/finalize は同一）であることの直接確証（G-115-381②(i)/(iii)）。
#[test]
fn drain_includes_tail_off_undercounts_on_matches_full() {
    // body: 1.0s @ 0.1（静か）/ tail: 0.5s @ 0.5（大きい = peak は tail にある）
    let body = sine_stereo(SR as usize, 0.1);
    let tail = sine_stereo(SR as usize / 2, 0.5);
    let body_f64 = to_f64(&body);
    let tail_f64 = to_f64(&tail); // ON 側 drain と同じ f32→f64 経路を full にも適用（精度対称）

    // full 参照: body + tail を直接 push。
    let mut eng_full = MeasureEngine::new(SR, ChannelLayout::stereo()).unwrap();
    let _ = eng_full.push(&body_f64);
    let _ = eng_full.push(&tail_f64);
    let full = eng_full.finalize();

    // OFF（旧 discard）: body のみ。tail は破棄され finalize に入らない。
    let mut eng_off = MeasureEngine::new(SR, ChannelLayout::stereo()).unwrap();
    let _ = eng_off.push(&body_f64);
    let off = eng_off.finalize();

    // ON（barrier）: body push 済 + tail を ring に入れて drain。
    let mut eng_on = MeasureEngine::new(SR, ChannelLayout::stereo()).unwrap();
    let _ = eng_on.push(&body_f64);
    let mut trace_engine = MeasureEngine::new(SR, ChannelLayout::stereo()).unwrap();
    let (mut prod, cons) = rtrb::RingBuffer::<f32>::new(tail.len() + 16);
    for &s in &tail {
        prod.push(s).unwrap();
    }
    let mut cons = MeasureSampleConsumer::watch(cons);
    let ss: Arc<Mutex<Option<crate::engine::SessionSummary>>> = Arc::new(Mutex::new(None));
    let session = crate::MeterSession::new(SR, ChannelLayout::stereo()).unwrap();
    let meter_session = Arc::new(Mutex::new(session));
    let mut chunk = Vec::new();
    let mut resampled = Vec::new();
    let mut resampler = None;
    let mut native_frames_total = SR as u64;
    let mut consumed_samples = 0_u64;
    let mut phase_d = PhaseDChannelStream::new(FieldType::Free, 2);
    let mut phase_d_slots = Vec::new();
    let mut latest_pd = None;
    let mut record_core_pending = std::collections::VecDeque::new();
    let mut record_phase_pending = std::collections::VecDeque::new();
    let tracker = RecordTakeTracker::new();
    let mut last_capture_position_end = None;
    let ok = drain_ring_into_session(
        DrainRingSession {
            consumer: &mut cons,
            resampler: &mut resampler,
            trace_engine: &mut trace_engine,
            summary_engine: &mut eng_on,
            session_summary: &ss,
            meter_session: Some(&meter_session),
            meter_session_publication: None,
            meter_active: true,
            chunk_f64: &mut chunk,
            resampled_buf: &mut resampled,
            phase_d: &mut phase_d,
            phase_d_slot_pending: &mut phase_d_slots,
            latest_pd: &mut latest_pd,
            record_core_pending: &mut record_core_pending,
            record_phase_pending: &mut record_phase_pending,
            sample_rate: SR,
            n_channels: 2,
            native_frames_total: &mut native_frames_total,
            consumed_samples: &mut consumed_samples,
            record_take_tracker: &tracker,
            last_capture_position_end: &mut last_capture_position_end,
            finalize_capture: false,
            record_sm: None,
            take_epoch: None,
        },
        None,
    );
    assert!(ok, "drain must succeed");
    assert_eq!(
        meter_session.lock().unwrap().snapshot().active_frames,
        (tail.len() / 2) as u64,
        "Record close tail must reach the independent Meter Session exactly once"
    );
    let on = (*ss.lock().unwrap()).expect("session_summary written by drain");

    let fp = full.max_true_peak.expect("full tp");
    let offp = off.max_true_peak.expect("off tp");
    let onp = on.max_true_peak.expect("on tp");

    // OFF: tail の peak を取りこぼして max_true_peak が大きく過小（0.5→0.1 amp で ~14 dB）。
    assert!(
        (fp - offp).abs() > 1.0,
        "OFF must undercount max_true_peak (full={fp:.4} off={offp:.4})"
    );
    // ON（barrier）: tail 算入で full と一致（chunk 順序差 ~1e-12 のみ）。
    assert!(
        (fp - onp).abs() < 1e-6,
        "ON (drain) must match full within 1e-6 (full={fp:.9} on={onp:.9})"
    );
}

/// clean path（steady-no-residual）: ring が空のとき drain は engine の現値を
/// そのまま finalize して session_summary に書くだけで、値を乱さない（parity 不乱の保証）。
#[test]
fn drain_empty_ring_preserves_finalize_value() {
    let body = sine_stereo(SR as usize, 0.2);
    let body_f64 = to_f64(&body);
    let mut eng = MeasureEngine::new(SR, ChannelLayout::stereo()).unwrap();
    let _ = eng.push(&body_f64);
    let mut trace_engine = MeasureEngine::new(SR, ChannelLayout::stereo()).unwrap();
    let reference = eng.finalize();

    let (_prod, cons) = rtrb::RingBuffer::<f32>::new(16); // 空
    let mut cons = MeasureSampleConsumer::watch(cons);
    let ss: Arc<Mutex<Option<crate::engine::SessionSummary>>> = Arc::new(Mutex::new(None));
    let mut chunk = Vec::new();
    let mut resampled = Vec::new();
    let mut resampler = None;
    let mut native_frames_total = SR as u64;
    let mut consumed_samples = 0_u64;
    let mut phase_d = PhaseDChannelStream::new(FieldType::Free, 2);
    let mut phase_d_slots = Vec::new();
    let mut latest_pd = None;
    let mut record_core_pending = std::collections::VecDeque::new();
    let mut record_phase_pending = std::collections::VecDeque::new();
    let tracker = RecordTakeTracker::new();
    let mut last_capture_position_end = None;
    let ok = drain_ring_into_session(
        DrainRingSession {
            consumer: &mut cons,
            resampler: &mut resampler,
            trace_engine: &mut trace_engine,
            summary_engine: &mut eng,
            session_summary: &ss,
            meter_session: None,
            meter_session_publication: None,
            meter_active: false,
            chunk_f64: &mut chunk,
            resampled_buf: &mut resampled,
            phase_d: &mut phase_d,
            phase_d_slot_pending: &mut phase_d_slots,
            latest_pd: &mut latest_pd,
            record_core_pending: &mut record_core_pending,
            record_phase_pending: &mut record_phase_pending,
            sample_rate: SR,
            n_channels: 2,
            native_frames_total: &mut native_frames_total,
            consumed_samples: &mut consumed_samples,
            record_take_tracker: &tracker,
            last_capture_position_end: &mut last_capture_position_end,
            finalize_capture: false,
            record_sm: None,
            take_epoch: None,
        },
        None,
    );
    assert!(ok);
    let drained = (*ss.lock().unwrap()).unwrap();
    assert!(
        (reference.max_true_peak.unwrap() - drained.max_true_peak.unwrap()).abs() < 1e-9,
        "empty-ring drain must not disturb max_true_peak"
    );
    assert!(
        (reference.lufs_i.unwrap() - drained.lufs_i.unwrap()).abs() < 1e-9,
        "empty-ring drain must not disturb lufs_i"
    );
}

/// Record本体ではtransport位置の不連続をまたいで集計を維持する。
/// Stop境界のtight-drainだけが最後の不連続を観測した場合も、同じセッション定義を
/// 変えてはならない。bodyを既に集計済み、jump後のtailだけをringに残して再現する。
#[test]
fn drain_position_jump_preserves_record_session_summary() {
    let body = sine_stereo(SR as usize, 0.4);
    let tail = sine_stereo(SR as usize, 0.1);
    let body_f64 = to_f64(&body);
    let tail_f64 = to_f64(&tail);

    let mut reference_engine = MeasureEngine::new(SR, ChannelLayout::stereo()).unwrap();
    let _ = reference_engine.push(&body_f64);
    let _ = reference_engine.push(&tail_f64);
    let reference = reference_engine.finalize();

    let mut summary_engine = MeasureEngine::new(SR, ChannelLayout::stereo()).unwrap();
    let _ = summary_engine.push(&body_f64);
    let mut trace_engine = MeasureEngine::new(SR, ChannelLayout::stereo()).unwrap();
    let (mut producer, consumer) = rtrb::RingBuffer::<f32>::new(tail.len() + 16);
    for &sample in &tail {
        producer.push(sample).unwrap();
    }
    let mut consumer = MeasureSampleConsumer::watch(consumer);

    let tracker = RecordTakeTracker::new();
    tracker.note_capture_window(true, 0, SR as u64);
    tracker.note_capture_window(true, (SR * 2) as i64, SR as u64);

    let session_summary: Arc<Mutex<Option<crate::engine::SessionSummary>>> =
        Arc::new(Mutex::new(None));
    let mut chunk = Vec::new();
    let mut resampled = Vec::new();
    let mut resampler = None;
    let mut native_frames_total = SR as u64;
    let mut consumed_samples = 0_u64;
    let mut last_capture_position_end = Some(SR as i64);
    let mut phase_d = PhaseDChannelStream::new(FieldType::Free, 2);
    let mut phase_d_slots = Vec::new();
    let mut latest_pd = None;
    let mut record_core_pending = std::collections::VecDeque::new();
    let mut record_phase_pending = std::collections::VecDeque::new();

    let ok = drain_ring_into_session(
        DrainRingSession {
            consumer: &mut consumer,
            resampler: &mut resampler,
            trace_engine: &mut trace_engine,
            summary_engine: &mut summary_engine,
            session_summary: &session_summary,
            meter_session: None,
            meter_session_publication: None,
            meter_active: false,
            chunk_f64: &mut chunk,
            resampled_buf: &mut resampled,
            phase_d: &mut phase_d,
            phase_d_slot_pending: &mut phase_d_slots,
            latest_pd: &mut latest_pd,
            record_core_pending: &mut record_core_pending,
            record_phase_pending: &mut record_phase_pending,
            sample_rate: SR,
            n_channels: 2,
            native_frames_total: &mut native_frames_total,
            consumed_samples: &mut consumed_samples,
            record_take_tracker: &tracker,
            last_capture_position_end: &mut last_capture_position_end,
            finalize_capture: false,
            record_sm: None,
            take_epoch: None,
        },
        None,
    );
    assert!(ok);

    let drained = session_summary
        .lock()
        .unwrap()
        .expect("session summary written");
    assert!(
        (reference.max_true_peak.unwrap() - drained.max_true_peak.unwrap()).abs() < 1e-6,
        "position jump at Stop must not discard the earlier Record peak"
    );
    assert!(
        (reference.lufs_i.unwrap() - drained.lufs_i.unwrap()).abs() < 1e-6,
        "position jump at Stop must not restart the Record integrated loudness"
    );
}

#[test]
fn unclocked_watch_audio_is_never_eligible_for_record_prefix() {
    let unclocked = CaptureChunkPlan {
        sample_count: 512,
        capture_epoch: Some(7),
        position_start_samples: None,
        position_end_samples: None,
        clock_source: CaptureClockSource::Unknown,
        ..CaptureChunkPlan::default()
    };
    assert_eq!(trusted_pre_roll_epoch(unclocked), None);

    let clocked = CaptureChunkPlan {
        position_start_samples: Some(10_000),
        position_end_samples: Some(10_256),
        clock_source: CaptureClockSource::ProjectTimeline,
        ..unclocked
    };
    assert_eq!(trusted_pre_roll_epoch(clocked), Some(7));
}
