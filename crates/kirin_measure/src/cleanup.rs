//!
//! POST 側の Record→Watch 遷移は editor 側 (Stop / Keep 失敗) と IO Thread 側
//! (PRE liveness stale / ACK timeout / write threshold exceeded) の **5 経路**
//! で発火するが、共有状態 (`record_sm` + `pair_label` + `paired_pre_target`) の
//! cleanup 対称性が構造的に保証されないと B-024 G-115-56 同種 (test 2 POST
//! pair: 999999) の再発を招く。
//!
//! 本モジュールの [`exit_record_full`] は 3 ステップを 1 単位に束ね、全 5 経路で
//! **同一実装**を通過させて構造的契約 「Watch 状態 ⟹ pair_label 空 /
//! paired_pre_target = None」 を一点生成にする。
//!
//! - editor 側 (`hypha_post::editor::trigger_stop_internal` /
//!   `trigger_keep_internal` 失敗) は本ヘルパー経由
//! - IO Thread 側 (`io_thread_post::poll_pre_liveness_at` /
//!   `poll_ack_timeout_with_base` / `handle_exit_reason`) も本ヘルパー経由

use std::sync::{Arc, Mutex};

use crate::record::RecordStateMachine;

/// pair_label をクリア (Watch 復帰時 / Record 失敗時).
///
/// poison 時は機能的沈黙 (R-28). 表示用 string なので致命ではない.
/// `Option<String>` の paired_pre_target 側は [`exit_record_full`] 内で
pub fn clear_pair_label(pair_label: &Arc<Mutex<String>>) {
    if let Ok(mut g) = pair_label.lock() {
        g.clear();
    }
}

///
/// 以下 3 ステップを 1 単位で実行する:
///   1. `record_sm.exit_record()` — 状態機械を Watch に戻す
///   2. `clear_pair_label(pair_label)` — POST GUI の pair 表示を空文字化
///   3. `*paired_pre_target = None` — v1.2 (a) post.json 復元キーをクリア
///
/// editor 側 (`trigger_stop_internal` / `trigger_keep_internal` 失敗) と
/// IO Thread 側 (`run_record_tick` 連続失敗 / `poll_pre_liveness_at` PRE stale /
/// `poll_ack_timeout_with_base` ACK timeout) の **全 5 経路で同一呼出**.
///
/// # 構造的契約
/// "Watch 状態 ⟹ pair_label 空 / paired_pre_target = None".
/// この契約は本ヘルパーの 3 ステップでのみ成立する.
///
/// # Mutex poison
/// `paired_pre_target` の poison は `expect("paired_pre_target poisoned")` で
/// 即時 panic させる. POST 側 IO Thread 内 panic は Watchdog で再起動される
/// (3 層隔離 / Audio Thread 不影響). editor 側 panic は cdylib 内で完結し
/// DAW 本体には伝播しない (G-115-64 仕様).
pub fn exit_record_full(
    record_sm: &RecordStateMachine,
    pair_label: &Arc<Mutex<String>>,
    paired_pre_target: &Arc<Mutex<Option<String>>>,
) {
    record_sm.exit_record();
    clear_pair_label(pair_label);
    *paired_pre_target
        .lock()
        .expect("paired_pre_target poisoned") = None;
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::identity::License;
    use crate::record::{RecordState, RecordStateMachine};

    #[allow(clippy::type_complexity)]
    fn fresh_state() -> (
        Arc<RecordStateMachine>,
        Arc<Mutex<String>>,
        Arc<Mutex<Option<String>>>,
    ) {
        let sm = Arc::new(RecordStateMachine::new());
        sm.try_enter_record(License::Os).unwrap();
        let label = Arc::new(Mutex::new("pair: deadbeef".to_string()));
        let target = Arc::new(Mutex::new(Some("pre-iid-xxx".to_string())));
        (sm, label, target)
    }

    /// 構造的契約: Record 状態 + pair/target 設定済 で `exit_record_full` を呼ぶと
    /// 3 ステップ全て遂行され "Watch 状態 ⟹ pair_label 空 / paired_pre_target = None"
    /// に到達する.
    #[test]
    fn exit_record_full_clears_all_three_states() {
        let (sm, label, target) = fresh_state();
        assert_eq!(sm.current(), RecordState::Record);

        exit_record_full(&sm, &label, &target);

        assert_eq!(sm.current(), RecordState::Watch, "exit_record() must run");
        assert!(label.lock().unwrap().is_empty(), "pair_label must be empty");
        assert!(
            target.lock().unwrap().is_none(),
            "paired_pre_target must be None"
        );
    }

    /// 冪等性: 既に Watch / 空文字 / None でも何も壊さない.
    #[test]
    fn exit_record_full_is_idempotent_on_clean_state() {
        let sm = Arc::new(RecordStateMachine::new());
        let label = Arc::new(Mutex::new(String::new()));
        let target = Arc::new(Mutex::new(None));
        // 状態既に Watch.
        exit_record_full(&sm, &label, &target);
        assert_eq!(sm.current(), RecordState::Watch);
        assert!(label.lock().unwrap().is_empty());
        assert!(target.lock().unwrap().is_none());
        // 2 回目も無害.
        exit_record_full(&sm, &label, &target);
        assert_eq!(sm.current(), RecordState::Watch);
    }

    /// `clear_pair_label` 単体: 既存値を空文字化する.
    #[test]
    fn clear_pair_label_empties_string() {
        let label = Arc::new(Mutex::new("pair: PRE_xxxx".to_string()));
        clear_pair_label(&label);
        assert!(label.lock().unwrap().is_empty());
    }

    /// `clear_pair_label` poison 耐性: poison 時は機能的沈黙 (panic しない).
    #[test]
    fn clear_pair_label_silent_on_poison() {
        let label: Arc<Mutex<String>> = Arc::new(Mutex::new("preexisting".to_string()));
        let label_clone = Arc::clone(&label);
        let _ = std::thread::spawn(move || {
            let _g = label_clone.lock().unwrap();
            panic!("intentional poison for test");
        })
        .join();
        assert!(label.is_poisoned());
        // panic せず素通しすること (R-28 機能的沈黙).
        clear_pair_label(&label);
    }

    /// `paired_pre_target` poison は `expect` で panic する (G-115-64 仕様).
    /// 3 層隔離下では Watchdog 再起動で回復する設計.
    #[test]
    #[should_panic(expected = "paired_pre_target poisoned")]
    fn exit_record_full_panics_on_paired_pre_target_poison() {
        let sm = Arc::new(RecordStateMachine::new());
        let label = Arc::new(Mutex::new(String::new()));
        let target: Arc<Mutex<Option<String>>> = Arc::new(Mutex::new(Some("x".to_string())));
        let target_clone = Arc::clone(&target);
        let _ = std::thread::spawn(move || {
            let _g = target_clone.lock().unwrap();
            panic!("intentional poison for test");
        })
        .join();
        assert!(target.is_poisoned());
        // ここで panic することを期待.
        exit_record_full(&sm, &label, &target);
    }
}
