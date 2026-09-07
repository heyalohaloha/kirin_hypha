//! POST watch snapshot JSON codec.

use crate::{MeasureResult, SignalState};

/// POST JSON v2 フォーマット（Active 時。SS-5 + SS-6）。bus フィールドは削除済（A-3 修正後）。
///
/// # B-027 段階 3-B α-7-1: `pair_pre_name` field 追加
/// 同 project_hash 内の他 POST から read される (cross-instance 公開機構 / Q-A7 採用案 A)。
/// 旧 schema (本変更前 plugin) との互換は read 側 `PostTmpJson` の `#[serde(default)]`
/// で保証される (record_signal::RecordSignal.paired_pre_name と同位相)。
pub fn serialize_post_json(
    instance_id: &str,
    state: SignalState,
    pre_signal_state: Option<SignalState>,
    result: &MeasureResult,
    pair_pre_name: &str,
    pair_claimed_at: f64,
) -> String {
    serialize_post_json_with_daw(
        instance_id,
        state,
        pre_signal_state,
        result,
        pair_pre_name,
        pair_claimed_at,
        "",
    )
}

pub(super) fn serialize_post_json_with_daw(
    instance_id: &str,
    state: SignalState,
    pre_signal_state: Option<SignalState>,
    result: &MeasureResult,
    pair_pre_name: &str,
    pair_claimed_at: f64,
    daw_session_id: &str,
) -> String {
    serialize_post_json_with_daw_and_owner(
        instance_id,
        state,
        pre_signal_state,
        result,
        pair_pre_name,
        pair_claimed_at,
        daw_session_id,
        "",
    )
}

#[allow(clippy::too_many_arguments)]
pub(super) fn serialize_post_json_with_daw_and_owner(
    instance_id: &str,
    state: SignalState,
    pre_signal_state: Option<SignalState>,
    result: &MeasureResult,
    pair_pre_name: &str,
    pair_claimed_at: f64,
    daw_session_id: &str,
    watch_owner_id: &str,
) -> String {
    serialize_post_json_with_daw_owner_and_pair_instance(
        instance_id,
        state,
        pre_signal_state,
        result,
        pair_pre_name,
        pair_claimed_at,
        daw_session_id,
        watch_owner_id,
        "",
    )
}

#[allow(clippy::too_many_arguments)]
pub(super) fn serialize_post_json_with_daw_owner_and_pair_instance(
    instance_id: &str,
    state: SignalState,
    pre_signal_state: Option<SignalState>,
    result: &MeasureResult,
    pair_pre_name: &str,
    pair_claimed_at: f64,
    daw_session_id: &str,
    watch_owner_id: &str,
    paired_pre_instance_id: &str,
) -> String {
    let t = chrono::Utc::now().format("%Y-%m-%dT%H:%M:%S%.3fZ");
    let pre_state_str = pre_signal_state
        .map(|s| format!(r#""{}""#, s.as_str()))
        .unwrap_or_else(|| "null".to_string());
    // B-131 (G-115-380): hand-built JSON に生補間される外部由来の文字列 field を serde で escape する。
    // 旧: 生補間で `"` `\` を含む値が不正 JSON を生成 → 他 POST の scan_post_candidates_in が parse
    // 失敗で無言 skip → pairing 消失していた R-28 欠陥。PRE serialize_pre_json と対称。正常 ASCII /
    // UUID では byte 不変（既存 wire / parity literal-id 不変）。
    //   - pair_pre_name: 利用者 GUI 入力（set_pair_target / 対 PRE の Name）。
    //   - instance_id  : restore で host 由来になりうる。gate の is_path_safe_component
    //     (path_identity.rs) は `/` `\` 制御文字は拒否するが **`"` を拒否しない** ため、`"` 入りの
    //     restore instance_id が materialize wall を素通って同一 R-28 を起こす（census で検出）。
    //     根本封止（wall 側で `"` を quarantine）は B-128 領域につき番人へ別途上申。本 commit は
    //     JSON 出力層で同種一括 escape する。
    let instance_id_json =
        serde_json::to_string(instance_id).unwrap_or_else(|_| "\"\"".to_string());
    let daw_session_id_json =
        serde_json::to_string(daw_session_id).unwrap_or_else(|_| "\"\"".to_string());
    let watch_owner_id_json =
        serde_json::to_string(watch_owner_id).unwrap_or_else(|_| "\"\"".to_string());
    let host_process_id = crate::current_host_process_id();
    let pair_pre_name_json =
        serde_json::to_string(pair_pre_name).unwrap_or_else(|_| "\"\"".to_string());
    let paired_pre_instance_id_json =
        serde_json::to_string(paired_pre_instance_id).unwrap_or_else(|_| "\"\"".to_string());
    format!(
        r#"{{"v":2,"role":"POST","instance_id":{instance_id_json},"daw_session_id":{daw_session_id},"host_process_id":{host_process_id},"watch_owner_id":{watch_owner_id},"signal_state":"{signal_state}","pre_signal_state":{pre_signal_state},"t":"{t}","pair_pre_name":{pair_pre_name},"paired_pre_instance_id":{paired_pre_instance_id},"pair_claimed_at":{pair_claimed_at},"lufs_m":{lufs_m},"lufs_s":{lufs_s},"true_peak":{true_peak},"crest":{crest},"psr":{psr}{phase_d}}}"#,
        instance_id_json = instance_id_json,
        daw_session_id = daw_session_id_json,
        host_process_id = host_process_id,
        watch_owner_id = watch_owner_id_json,
        signal_state = state.as_str(),
        pre_signal_state = pre_state_str,
        t = t,
        pair_pre_name = pair_pre_name_json,
        paired_pre_instance_id = paired_pre_instance_id_json,
        pair_claimed_at = pair_claimed_at,
        lufs_m = opt_f64(result.lufs_m),
        lufs_s = opt_f64(result.lufs_s),
        true_peak = opt_f64(result.true_peak),
        crest = opt_f64(result.crest),
        psr = opt_f64(result.psr),
        phase_d = phase_d_fragment(result),
    )
}

/// Bypassed / Inactive 時の最小 POST JSON。
///
/// B-027 段階 3-B α-7-1: `pair_pre_name` field を追加 (Bypassed/Inactive でも候補化
/// される / All Keep N 計算で参照されるため filter 照合に必要)。
/// W-281 / G-115-249: `pair_claimed_at` field 追加 (後着優先 self check 判定軸)。
#[cfg(test)]
pub(super) fn serialize_post_json_minimal(
    instance_id: &str,
    state: SignalState,
    pair_pre_name: &str,
    pair_claimed_at: f64,
) -> String {
    serialize_post_json_minimal_with_daw(instance_id, state, pair_pre_name, pair_claimed_at, "")
}

#[cfg(test)]
pub(super) fn serialize_post_json_minimal_with_daw(
    instance_id: &str,
    state: SignalState,
    pair_pre_name: &str,
    pair_claimed_at: f64,
    daw_session_id: &str,
) -> String {
    serialize_post_json_minimal_with_daw_and_owner(
        instance_id,
        state,
        pair_pre_name,
        pair_claimed_at,
        daw_session_id,
        "",
    )
}

#[cfg(test)]
pub(super) fn serialize_post_json_minimal_with_daw_and_owner(
    instance_id: &str,
    state: SignalState,
    pair_pre_name: &str,
    pair_claimed_at: f64,
    daw_session_id: &str,
    watch_owner_id: &str,
) -> String {
    serialize_post_json_minimal_with_daw_owner_and_pair_instance(
        instance_id,
        state,
        pair_pre_name,
        pair_claimed_at,
        daw_session_id,
        watch_owner_id,
        "",
    )
}

pub(super) fn serialize_post_json_minimal_with_daw_owner_and_pair_instance(
    instance_id: &str,
    state: SignalState,
    pair_pre_name: &str,
    pair_claimed_at: f64,
    daw_session_id: &str,
    watch_owner_id: &str,
    paired_pre_instance_id: &str,
) -> String {
    let t = chrono::Utc::now().format("%Y-%m-%dT%H:%M:%S%.3fZ");
    // B-131 (G-115-380): instance_id / pair_pre_name を serde で JSON escape（serialize_post_json と同一契約）。
    let instance_id_json =
        serde_json::to_string(instance_id).unwrap_or_else(|_| "\"\"".to_string());
    let daw_session_id_json =
        serde_json::to_string(daw_session_id).unwrap_or_else(|_| "\"\"".to_string());
    let watch_owner_id_json =
        serde_json::to_string(watch_owner_id).unwrap_or_else(|_| "\"\"".to_string());
    let host_process_id = crate::current_host_process_id();
    let pair_pre_name_json =
        serde_json::to_string(pair_pre_name).unwrap_or_else(|_| "\"\"".to_string());
    let paired_pre_instance_id_json =
        serde_json::to_string(paired_pre_instance_id).unwrap_or_else(|_| "\"\"".to_string());
    format!(
        r#"{{"v":2,"role":"POST","instance_id":{instance_id_json},"daw_session_id":{daw_session_id},"host_process_id":{host_process_id},"watch_owner_id":{watch_owner_id},"signal_state":"{signal_state}","t":"{t}","pair_pre_name":{pair_pre_name},"paired_pre_instance_id":{paired_pre_instance_id},"pair_claimed_at":{pair_claimed_at}}}"#,
        instance_id_json = instance_id_json,
        daw_session_id = daw_session_id_json,
        host_process_id = host_process_id,
        watch_owner_id = watch_owner_id_json,
        signal_state = state.as_str(),
        t = t,
        pair_pre_name = pair_pre_name_json,
        paired_pre_instance_id = paired_pre_instance_id_json,
        pair_claimed_at = pair_claimed_at,
    )
}

fn opt_f64(v: Option<f64>) -> String {
    match v {
        Some(x) => format!("{:.3}", x),
        None => "null".to_string(),
    }
}

use crate::measure_json::phase_d_fragment;
