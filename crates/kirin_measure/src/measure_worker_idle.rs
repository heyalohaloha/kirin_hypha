//! Measurement worker idle-state predicates.
use super::*;

/// Measure Thread の通常ループ間隔。
pub(super) const LOOP_SLEEP: Duration = Duration::from_millis(100);

/// Record 中の idle poll 間隔。
///
/// Offline bounce は wall-clock より速く Audio Thread が進むため、Record 中だけ短い sleep にして
/// Audio→Measure ring の滞留を減らす。Audio Thread からの通知や lock は増やさない。
pub(super) const RECORD_LOOP_SLEEP: Duration = Duration::from_millis(1);

/// heartbeat が変化しないまま何 tick 経過したら process() 停止と判定するか。
/// 4 tick × 100 ms = 400 ms。2回連続の200 ms監視境界を満たしつつ、一時的なcallback揺れを
/// 1回分吸収する。音声callbackが継続する実際の無音は別のsilence gateが扱うため、ここで
/// 3秒保持するとtransport停止後に古い値と履歴だけが残る。
pub(super) const HEARTBEAT_STALE_TICKS: u32 = 4;
pub(super) const TICK: Duration = LOOP_SLEEP; // 100ms

/// 鮮度の live ウィンドウ = 4 tick × 100ms = 400ms。製品はこの導出値、
/// テストは `LivenessEvaluator::new` に任意 Duration を注入する。
pub fn live_window() -> Duration {
    TICK * HEARTBEAT_STALE_TICKS
}

#[inline]
pub(super) fn idle_sleep_for_record_state(is_recording: bool) -> Duration {
    if is_recording {
        RECORD_LOOP_SLEEP
    } else {
        LOOP_SLEEP
    }
}

/// heartbeat が鮮度切れになった後の表示状態。
///
/// host component が active のままなら通常の処理停止（transport停止・無音・一時停止）なので
/// Inactive。host が component 自体を deactivate した場合だけ Bypassed とする。
#[inline]
pub fn stalled_signal_state(host_component_active: bool) -> SignalState {
    if host_component_active {
        SignalState::Inactive
    } else {
        SignalState::Bypassed
    }
}

pub(super) fn should_process_phase_d(is_recording: bool) -> bool {
    is_recording
}

/// Queued PCM alone is not progress: its cursor, frame or generation proof may be pending.
pub(super) fn should_continue_measurement(processed_samples: usize, queued_samples: usize) -> bool {
    processed_samples > 0 && queued_samples > 0
}

/// SignalState controls Watch freshness and GUI visibility. Record admission is owned by the
/// Record state machine and capture clock; once Audio Thread has admitted a buffer, silence is
/// still factual audio and must pass through every Record engine. Treating Inactive as a Record
/// boundary resets Phase D and creates sparse 100 ms optional fields on quiet buses.
#[inline]
pub(super) fn should_suspend_measurement(state: SignalState, is_recording: bool) -> bool {
    !is_recording && state != SignalState::Active
}

impl RecordStateMachine {
    pub(crate) fn wait_for_measure_work(&self, is_recording: bool) {
        #[cfg(test)]
        worker_probe::idle(is_recording);
        self.note_measure_idle_wait();
        if is_recording {
            thread::sleep(idle_sleep_for_record_state(true));
        } else {
            thread::park_timeout(idle_sleep_for_record_state(false));
        }
        self.note_measure_idle_return(is_recording);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn queued_but_unreadable_pcm_waits_instead_of_spinning() {
        assert!(!should_continue_measurement(0, 32));
        assert!(!should_continue_measurement(32, 0));
        assert!(!should_continue_measurement(0, 0));
        assert!(should_continue_measurement(32, 32));
    }
}
