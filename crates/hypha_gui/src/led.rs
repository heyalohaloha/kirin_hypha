//! 5状態 LED — Watch LED のロジック（ T-6）。
//!
//! 状態決定は純粋関数 `derive_led_state` に集約。時間依存の色計算は
//! `led_color(state, time_secs)` で行う。egui の描画コードとは独立して
//! ユニットテスト可能な構造。

use crate::palette::{COL_FLORA, COL_LED_BLUE, COL_LED_GREEN, COL_LED_GREY, COL_LED_YELLOW};
use kirin_measure::SignalState;
use nih_plug_egui::egui::Color32;

/// Watch LED の 6 状態。
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum LedState {
    /// 計測スレッド稼働中だが信号なし / バイパス → 灰（静）
    Idle,
    /// Watch 計測中（Active 信号あり、Record なし）→ 青（3秒周期呼吸）
    WatchBreathing,
    /// OS 側から preset 提案が届いている（Watch 中）→ amber 緩やかパルス（COL_FLORA）
    PresetAvailable,
    /// Record 待機（POST が PRE からの ACK を待っている）→ 緑（淡・静）
    RecordStandby,
    /// Record 計測中（ACK 済・両方 Active）→ 緑（1.5秒周期脈動）
    RecordActive,
    /// Measure Thread 停止（Audio Thread が再起動を試みている）→ 黄（静）
    Error,
}

/// LED 状態を 5 つの入力から導出する純粋関数。
///
/// 優先順位: Error > RecordActive > RecordStandby > PresetAvailable > WatchBreathing > Idle
///
/// - `measure_alive`: Measure Thread が稼働中か（false → Error）
/// - `signal_state`: Audio Thread の宣言状態
/// - `recording`: 自クレート（PRE / POST）の Record モードが有効か
/// - `record_acknowledged`: Record 信号が ACK 済か（POST 側で意味を持つ。
///   PRE は常に true を渡してよい）
/// - `preset_available`: preset/*.json が 1 件以上存在するか
///   （POST IO Thread の poller が更新。Record 中は抑制される）
pub fn derive_led_state(
    measure_alive: bool,
    signal_state: SignalState,
    recording: bool,
    record_acknowledged: bool,
    preset_available: bool,
) -> LedState {
    if !measure_alive {
        return LedState::Error;
    }
    if recording {
        // ACK 未取得 → Standby（POST が PRE を待っている状態）
        if !record_acknowledged {
            return LedState::RecordStandby;
        }
        // ACK 済 + Active → Active 脈動
        if signal_state == SignalState::Active {
            return LedState::RecordActive;
        }
        // ACK 済だが信号が止まった → Standby に戻す
        return LedState::RecordStandby;
    }
    if preset_available && signal_state == SignalState::Active {
        return LedState::PresetAvailable;
    }
    if signal_state == SignalState::Active {
        return LedState::WatchBreathing;
    }
    LedState::Idle
}

/// 状態 + 経過秒数から描画色を計算する。
///
/// 呼吸 / 脈動アニメーションは輝度を sin で変調する。
/// 位相は `time_secs` に対して安定（start 基準の相対時刻ならウィンドウを開いても変わらない）。
pub fn led_color(state: LedState, time_secs: f64) -> Color32 {
    match state {
        LedState::Idle => COL_LED_GREY,
        LedState::Error => COL_LED_YELLOW,
        LedState::RecordStandby => dim(COL_LED_GREEN, 0.45),
        LedState::WatchBreathing => breathe(COL_LED_BLUE, time_secs, 3.0, 0.6, 1.0),
        LedState::RecordActive => breathe(COL_LED_GREEN, time_secs, 1.5, 0.7, 1.0),
        // Q3 P1: amber 緩やかパルス（2.5s 周期・0.55–1.0）。COL_FLORA を使用。
        LedState::PresetAvailable => breathe(COL_FLORA, time_secs, 2.5, 0.55, 1.0),
    }
}

/// 固定輝度で色を暗くする（呼吸なし）。`factor` は 0.0–1.0。
fn dim(c: Color32, factor: f64) -> Color32 {
    let f = factor.clamp(0.0, 1.0);
    let r = (c.r() as f64 * f).round() as u8;
    let g = (c.g() as f64 * f).round() as u8;
    let b = (c.b() as f64 * f).round() as u8;
    Color32::from_rgb(r, g, b)
}

/// sin 呼吸を色に適用する。
///
/// - `period_secs`: 呼吸 1 周期の秒数
/// - `min_factor`, `max_factor`: 明度の最小 / 最大（0.0–1.0）
fn breathe(
    c: Color32,
    time_secs: f64,
    period_secs: f64,
    min_factor: f64,
    max_factor: f64,
) -> Color32 {
    let two_pi = std::f64::consts::TAU;
    let phase = (time_secs / period_secs) * two_pi;
    let sin_01 = (phase.sin() + 1.0) * 0.5; // 0.0–1.0
    let f = min_factor + (max_factor - min_factor) * sin_01;
    dim(c, f)
}

// ── tests ────────────────────────────────────────────────────────────────

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn measure_dead_always_error() {
        // measure_alive=false は他の入力を無視して Error
        for &sig in &[
            SignalState::Active,
            SignalState::Bypassed,
            SignalState::Inactive,
        ] {
            for &rec in &[true, false] {
                for &ack in &[true, false] {
                    for &pa in &[true, false] {
                        assert_eq!(derive_led_state(false, sig, rec, ack, pa), LedState::Error,);
                    }
                }
            }
        }
    }

    #[test]
    fn recording_not_ack_is_standby() {
        // Record 要求済だが ACK 未取得 → Standby（信号状態に関わらず）
        for &sig in &[
            SignalState::Active,
            SignalState::Bypassed,
            SignalState::Inactive,
        ] {
            assert_eq!(
                derive_led_state(true, sig, true, false, false),
                LedState::RecordStandby
            );
        }
    }

    #[test]
    fn recording_ack_active_is_record_active() {
        assert_eq!(
            derive_led_state(true, SignalState::Active, true, true, false),
            LedState::RecordActive
        );
    }

    #[test]
    fn recording_ack_non_active_falls_to_standby() {
        // ACK 済でも信号が止まれば Standby に戻す
        assert_eq!(
            derive_led_state(true, SignalState::Bypassed, true, true, false),
            LedState::RecordStandby
        );
        assert_eq!(
            derive_led_state(true, SignalState::Inactive, true, true, false),
            LedState::RecordStandby
        );
    }

    #[test]
    fn watch_breathing_when_active_no_record() {
        assert_eq!(
            derive_led_state(true, SignalState::Active, false, true, false),
            LedState::WatchBreathing
        );
    }

    #[test]
    fn idle_when_non_active_no_record() {
        assert_eq!(
            derive_led_state(true, SignalState::Bypassed, false, true, false),
            LedState::Idle
        );
        assert_eq!(
            derive_led_state(true, SignalState::Inactive, false, true, false),
            LedState::Idle
        );
    }

    #[test]
    fn static_states_are_time_independent() {
        for s in [LedState::Idle, LedState::Error, LedState::RecordStandby] {
            let c0 = led_color(s, 0.0);
            let c1 = led_color(s, 12.345);
            let c2 = led_color(s, 9876.54);
            assert_eq!(c0, c1);
            assert_eq!(c1, c2);
        }
    }

    #[test]
    fn breathing_state_varies_with_time() {
        // 青呼吸は周期 3秒。0 と 0.75 で輝度が変わる
        let a = led_color(LedState::WatchBreathing, 0.0);
        let b = led_color(LedState::WatchBreathing, 0.75);
        assert_ne!(a, b, "0s and 0.75s should differ within the 3s cycle");
    }

    #[test]
    fn breathing_stays_within_clamp_range() {
        // 呼吸で min_factor (0.6) 未満に下がらない
        let base = COL_LED_BLUE;
        let min_b = (base.b() as f64 * 0.6).round() as u8;
        for i in 0..120 {
            let t = i as f64 * 0.05;
            let c = led_color(LedState::WatchBreathing, t);
            assert!(
                c.b() >= min_b.saturating_sub(1),
                "blue channel {} below min {} at t={}",
                c.b(),
                min_b,
                t
            );
        }
    }

    // ── PresetAvailable（サブ3-C-1 / Q3 P1）──────────────────────────────

    #[test]
    fn preset_available_only_when_active_and_not_recording() {
        // Active + 非 Record + preset=true → PresetAvailable
        assert_eq!(
            derive_led_state(true, SignalState::Active, false, true, true),
            LedState::PresetAvailable
        );
        // preset=false のときは Watch に戻る
        assert_eq!(
            derive_led_state(true, SignalState::Active, false, true, false),
            LedState::WatchBreathing
        );
    }

    #[test]
    fn preset_does_not_override_record() {
        // recording=true のときは preset フラグを無視（優先度: Record > Preset）
        assert_eq!(
            derive_led_state(true, SignalState::Active, true, true, true),
            LedState::RecordActive
        );
        assert_eq!(
            derive_led_state(true, SignalState::Active, true, false, true),
            LedState::RecordStandby
        );
    }

    #[test]
    fn preset_suppressed_when_signal_not_active() {
        // Bypassed / Inactive のときは preset=true でも Idle（信号無しで amber は出さない）
        for &sig in &[SignalState::Bypassed, SignalState::Inactive] {
            assert_eq!(
                derive_led_state(true, sig, false, true, true),
                LedState::Idle
            );
        }
    }

    #[test]
    fn preset_available_color_pulses_with_time() {
        // amber パルスは周期 2.5s。0 と 0.625 で輝度が変わる
        let a = led_color(LedState::PresetAvailable, 0.0);
        let b = led_color(LedState::PresetAvailable, 0.625);
        assert_ne!(a, b, "0s and 0.625s should differ within the 2.5s cycle");
    }

    #[test]
    fn preset_available_color_uses_flora_hue() {
        // breathe は明度を min_factor..=max_factor に線形マップするため
        // 各チャネルは base * factor の範囲に収まる。COL_FLORA (0xD4, 0xA0, 0x43)。
        let base = crate::palette::COL_FLORA;
        for i in 0..100 {
            let t = i as f64 * 0.05;
            let c = led_color(LedState::PresetAvailable, t);
            // 各チャネルは base の 0.55..=1.00 範囲内（±1 の丸め誤差を許容）
            let min_r = (base.r() as f64 * 0.55).round() as u8;
            assert!(
                c.r() >= min_r.saturating_sub(1),
                "r={} min_r={}",
                c.r(),
                min_r
            );
            assert!(c.r() <= base.r(), "r={} base_r={}", c.r(), base.r());
        }
    }

    #[test]
    fn recording_priority_stays_above_preset() {
        // 記録中は preset を完全に抑制（amber がちらつかない）
        for &ack in &[true, false] {
            for &pa in &[true, false] {
                let got = derive_led_state(true, SignalState::Active, true, ack, pa);
                assert!(
                    matches!(got, LedState::RecordActive | LedState::RecordStandby),
                    "got={:?} ack={} pa={}",
                    got,
                    ack,
                    pa
                );
            }
        }
    }
}
