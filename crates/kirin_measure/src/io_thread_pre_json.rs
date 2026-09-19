//! `pre.json` の中身を作る。**この 1 ファイルが「PRE が何を名乗るか」を持つ。**
//!
//! B-976 で測定配置を足したときに `io_thread_pre.rs` から分けた（行数規律 / AGENTS.md）。
//! 読めること（transport）はここ、比べてよいか（measurement compatibility）は POST 側。

use super::*;

#[allow(clippy::too_many_arguments)]
pub(super) fn write_json(
    file_path: &Path,
    instance_id: &str,
    name: &str,
    daw_session_id: &str,
    watch_owner_id: &str,
    result: &Arc<Mutex<MeasureResult>>,
    signal_state: &Arc<AtomicU8>,
    layout: crate::channel_layout::ChannelLayout,
) -> Result<(), String> {
    let dir = file_path
        .parent()
        .ok_or_else(|| "pre.json path has no parent".to_string())?;
    fs::create_dir_all(dir).map_err(|e| format!("create_dir_all: {e}"))?;

    let state = load_signal_state(signal_state);

    let json = if state == SignalState::Active {
        let measure = crate::sync_recovery::lock_recover(result, "PRE write_json").clone();
        serialize_pre_json_with_daw_session_id_and_owner(
            instance_id,
            name,
            daw_session_id,
            watch_owner_id,
            state,
            &measure,
            Some(layout),
        )
    } else {
        serialize_pre_json_minimal_with_daw_session_id_and_owner(
            instance_id,
            name,
            daw_session_id,
            watch_owner_id,
            state,
        )
    };

    crate::atomic_file::write_bytes_atomic(file_path, json.as_bytes())
        .map_err(|e| format!("atomic write: {e}"))?;

    Ok(())
}

///
/// A-3 修正後: bus フィールドは削除（path に instance_id が入るため不要）。
/// B-027 段階 2: `name` field を追加 (POST `pair_pre_name` filter 用)。
/// pre.json schema バージョン (`v=2`) は据置き。読込側 (`PreTmpJson`) は
/// `#[serde(default)]` で旧 schema 互換を維持する。
pub fn serialize_pre_json(
    instance_id: &str,
    name: &str,
    state: SignalState,
    result: &MeasureResult,
) -> String {
    serialize_pre_json_with_daw_session_id(instance_id, name, "", state, result)
}

pub fn serialize_pre_json_with_daw_session_id(
    instance_id: &str,
    name: &str,
    daw_session_id: &str,
    state: SignalState,
    result: &MeasureResult,
) -> String {
    serialize_pre_json_with_daw_session_id_and_owner(
        instance_id,
        name,
        daw_session_id,
        "",
        state,
        result,
        None,
    )
}

pub(super) fn serialize_pre_json_with_daw_session_id_and_owner(
    instance_id: &str,
    name: &str,
    daw_session_id: &str,
    watch_owner_id: &str,
    state: SignalState,
    result: &MeasureResult,
    layout: Option<crate::channel_layout::ChannelLayout>,
) -> String {
    let t = chrono::Utc::now().format("%Y-%m-%dT%H:%M:%S%.3fZ");
    // B-077: name は利用者入力。serde で JSON 文字列化し " \ 制御文字・日本語を安全に escape する
    // （旧: 生補間で " \ を含む名が不正 JSON を生成 → POST parse 失敗）。正常 ASCII 名は不変。
    // B-131 (G-115-380): instance_id も同様に serde escape する。restore で host 由来になりうる値を
    // gate する is_path_safe_component (path_identity.rs) は `"` を拒否しないため、`"` 入り
    // instance_id が materialize wall を素通り不正 JSON → pairing 消失を起こす同種欠陥（census 検出）。
    // 正常 UUID では byte 不変（parity literal-id 不変）。根本封止（wall 側 `"` quarantine）は番人へ上申。
    let name_json = serde_json::to_string(name).unwrap_or_else(|_| "\"\"".to_string());
    let instance_id_json =
        serde_json::to_string(instance_id).unwrap_or_else(|_| "\"\"".to_string());
    let daw_session_id_json =
        serde_json::to_string(daw_session_id).unwrap_or_else(|_| "\"\"".to_string());
    let watch_owner_id_json =
        serde_json::to_string(watch_owner_id).unwrap_or_else(|_| "\"\"".to_string());
    let host_process_id = current_host_process_id();
    format!(
        r#"{{"v":2,"role":"PRE","instance_id":{instance_id_json},"name":{name_json},"daw_session_id":{daw_session_id_json},"host_process_id":{host_process_id},"watch_owner_id":{watch_owner_id_json},"signal_state":"{signal_state}","t":"{t}","lufs_m":{lufs_m},"lufs_s":{lufs_s},"true_peak":{true_peak},"crest":{crest},"psr":{psr}{phase_d}{layout}}}"#,
        instance_id_json = instance_id_json,
        name_json = name_json,
        daw_session_id_json = daw_session_id_json,
        host_process_id = host_process_id,
        watch_owner_id_json = watch_owner_id_json,
        signal_state = state.as_str(),
        t = t,
        lufs_m = opt_f64(result.lufs_m),
        lufs_s = opt_f64(result.lufs_s),
        true_peak = opt_f64(result.true_peak),
        crest = opt_f64(result.crest),
        psr = opt_f64(result.psr),
        phase_d = phase_d_fragment(result),
        layout = layout_provenance::layout_fragment(layout),
    )
}
