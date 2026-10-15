//! PRE IO worker wiring and restart ownership. Audio processing remains in the engine.
use super::*;

impl KirinHyphaEngine {
    /// PRE の plugin_data 書込（Watch pre.json + Record frames/PSB）を有効化する（B-057 3b）。
    ///
    /// `kirin_measure::spawn_io_thread_pre`（io_thread_pre.rs:179）を engine 既存の共有
    /// 状態（record_sm / measure_result / signal_state / session_summary）に繋いで起動する。
    /// io_thread ロジック自体は kirin_measure のまま（呼ぶだけ）。filesystem 書込は全て
    /// その Rust スレッド内に閉じる（FFI は spawn と識別子注入のみ・B2 分離原則）。
    ///
    /// 前提・割り切り（3b）:
    /// - `set_license` の現在値と IO worker は同じ [`LiveLicense`] を共有する。
    ///   enable 後の Kirin OS 認識・降格も engine 再生成なしで反映される。
    /// - `instance_id` は `Uuid::new_v4` 生成（永続は 3c）。`project_uuid` / `daw_session_id`
    ///   は FFI 側で role identity として解決する。空/legacy session は runtime daw を空のまま
    ///   host fallback に委ね、保存済み document の非空 `daw_session_uuid` は B-301 の session group
    ///   で分離する。解決値は `set_project_uuid` で kirin_measure セルへも反映し、path のルートになる。
    /// - 2 度目以降の呼出は no-op（冪等）。
    ///
    /// PRE-POST discovery は POST 不在のとき inert（record_signal が無く ack 対象なし /
    /// io_thread_pre.rs:294-312）。pairing 実働は 3d。
    pub fn enable_pre_writes(&self) {
        let mut slot = match self.io_thread.lock() {
            Ok(g) => g,
            Err(_) => return,
        };
        if slot.is_some() {
            return; // 冪等: 既に PRE/POST io_thread 起動済み。
        }
        // B-067/F3: この engine の書込 role を PRE に確定（add_annotation が使う / 単一 role）。
        if let Ok(mut r) = self.write_role.lock() {
            *r = Some(PluginDataRole::Pre);
        }

        // 識別子: instance_id は set_identity 復元値、未設定はここで生成（3b フォールバック）。
        // project_uuid / daw_session_id は下記 role identity 解決で確定し、
        // identity に書き戻して get_identity が JUCE chunk へ返せるようにする。
        let (iid_str, name_str, project_hash, daw_uuid) = {
            let mut id = match self.identity.lock() {
                Ok(g) => g,
                Err(_) => return,
            };
            if !id.claim_for_writes("pre") {
                return;
            }
            // B-106/B-301/B-302: chunk 復元値（id.*）を candidate に渡し、空/legacy session は
            // runtime daw を空にして host fallback、非空 session は saved-document group で解決する。
            // 解決値を identity に書き戻して chunk 永続。kirin_measure cell への反映（set_*）は現状維持。
            let (resolved_project, resolved_daw) =
                resolve_pre_identity(&id.project_uuid, &id.daw_session_uuid);
            id.project_uuid = resolved_project.clone();
            id.project_hash = resolved_project.clone();
            id.daw_session_uuid = resolved_daw.clone();
            store_resolved_identity_cells(
                &self.project_hash_cell,
                &self.daw_session_id_cell,
                &resolved_project,
                &resolved_daw,
            );
            set_project_uuid(resolved_project);
            set_daw_session_id(resolved_daw);
            (
                id.instance_id.clone(),
                id.name.clone(),
                id.project_hash.clone(),
                id.daw_session_uuid.clone(),
            )
        };

        let instance_id = Arc::new(RwLock::new(iid_str));
        // io_thread_pre が内部で set/管理する共有フラグ（FFI は false で生成して渡すだけ）。
        let recording = Arc::new(AtomicBool::new(false));
        // B-054: record_acknowledged は engine と共有（PRE Keeping バナー / RecordActive LED が poll）。
        let record_acknowledged = Arc::clone(&self.record_acknowledged);
        // B-054: PRE 名は engine.pre_name と Arc 共有（set_pair_target と完全対称・enable 後 live）。
        // 空のときのみ identity.name で seed（pre-enable の set_pre_name を温存）。空のままなら
        // pre.json には空 name が書かれる（io_thread_pre は加工しない / instance_id 先頭8字は表示専用）。
        if let Ok(mut pn) = self.pre_name.write() {
            if pn.is_empty() {
                *pn = name_str;
            }
        }
        let name = Arc::clone(&self.pre_name);
        // B-118 Phase 3 (③): engine 保持の Arc を共有（io が書き JUCE getter が読む / 世代跨ぎ継続）。
        let record_error_message = Arc::clone(&self.record_error_message);
        // A: enable 時点の license をスナップショット（immutable）。
        let license = self.license.clone();

        // B-118: io spawn を restart-closure に包む（初回 spawn も watchdog 再起動も同一経路）。
        // 継続性（最重要）: 共有状態 Arc（record_error_message / recording / record_acknowledged /
        // name / record_sm / measure_result / signal_state / session_summary / push_overflow）は
        // 同一実体を capture し、再起動後も同じ Arc を指す（closure 内での再生成・新規 Arc 化は禁止）。
        // io_shutdown のみ世代毎に新規生成する。
        let restart: RestartIoFn = {
            // `ChannelLayout` は Copy。closure へは値で渡す（`&self` を捕まえない）。
            let layout = self.layout;
            let record_sm = Arc::clone(&self.record_sm);
            let measure_result = Arc::clone(&self.measure_result);
            let signal_state = Arc::clone(&self.signal_state);
            let session_summary = Arc::clone(&self.session_summary);
            let record_trace_queue = Arc::clone(&self.record_trace_queue);
            let record_take_tracker = Arc::clone(&self.record_take_tracker);
            let record_ingress = Arc::clone(&self.record_ingress);
            let push_overflow = Arc::clone(&self.push_overflow);
            let oversized_drop = Arc::clone(&self.oversized_drop); // B-125
            let spectrum = Arc::clone(&self.spectrum);
            let meter_delta_history = self.meter_delta_history.as_ref().map(Arc::clone);
            let sample_rate = self.sample_rate;
            Box::new(move || {
                let io_shutdown = Arc::new(AtomicBool::new(false));
                let handle = spawn_io_thread_pre(
                    Arc::clone(&instance_id),
                    project_hash.clone(),
                    daw_uuid.clone(), // PRE pre.json の document 境界として出力する復元値
                    sample_rate,
                    layout,
                    Arc::clone(&record_sm),
                    Arc::clone(&recording),
                    Arc::clone(&record_acknowledged),
                    license.clone(),
                    Arc::clone(&measure_result),
                    Arc::clone(&signal_state),
                    Arc::clone(&io_shutdown),
                    Arc::clone(&name),
                    Arc::clone(&record_error_message),
                    Arc::clone(&session_summary),
                    Arc::clone(&record_trace_queue),
                    Arc::clone(&record_take_tracker),
                    Arc::clone(&record_ingress),
                    Arc::clone(&push_overflow), // B-076: per-Record dropped_samples
                    Arc::clone(&oversized_drop), // B-125: per-Record oversized block drop
                    Some(Arc::clone(&spectrum)),
                    meter_delta_history.as_ref().map(Arc::clone),
                );
                IoThreadHandle {
                    shutdown: io_shutdown,
                    handle,
                }
            })
        };

        // 初回 io spawn = closure 実行 → 共有 slot に置き watchdog が監視。restart を closure slot へ。
        *slot = Some(restart());
        if let Ok(mut rs) = self.io_restart_slot.lock() {
            *rs = Some(restart);
        }
    }
}
