//! POST IO worker wiring and restart ownership. Audio processing remains in the engine.
use super::*;

impl KirinHyphaEngine {
    /// POST の plugin_data 書込（post.json の Δ・select_target_pre 経由＝厳格選定）を
    /// 有効化する（B-060 3d-a）。`enable_pre_writes` と対。同一 engine では排他（片方のみ）。
    ///
    /// `kirin_measure::spawn_io_thread_post` を engine 既存の共有状態（record_sm /
    /// measure_result / signal_state / session_summary）+ POST 固有 Arc に繋いで起動する。
    /// io_thread の run_tick が `select_target_pre`（B-059 厳格）で PRE を選び post.json に
    /// Δ を書く。**Keep/ack（write_pending）は配線しない**（trigger closures は no-op = 3d-a）。
    ///
    /// 前提・割り切り（3d-a）:
    /// - `set_license` の後に呼ぶ（io_thread の license スナップショットは PRE と同様 / 但し
    ///   POST run_tick の Δ 表示は license gate なし）。
    /// - `set_identity` 済みなら復元値、未設定は生成（PRE と同経路 / 永続は 3c）。
    /// - **pair_pre_name（対 PRE 名）= identity.name** を使う（同名の PRE と対になる規約）。
    /// - 2 度目以降の呼出は no-op（冪等）。
    pub fn enable_post_writes(&self) {
        let mut slot = match self.io_thread.lock() {
            Ok(g) => g,
            Err(_) => return,
        };
        if slot.is_some() {
            return; // 冪等。
        }
        // B-067/F3: この engine の書込 role を POST に確定（add_annotation が使う / 単一 role）。
        if let Ok(mut r) = self.write_role.lock() {
            *r = Some(PluginDataRole::Post);
        }

        let (iid_str, name_str, project_hash, daw_uuid) = {
            let mut id = match self.identity.lock() {
                Ok(g) => g,
                Err(_) => return,
            };
            if id.instance_id.is_empty() {
                id.instance_id = Uuid::new_v4().to_string();
            }
            // B-106/B-301/B-302: enable_pre_writes と同一規約。空/legacy session は runtime daw
            // を空にして host fallback、非空 session は saved-document group で解決する。
            let (resolved_project, resolved_daw) =
                resolve_post_identity(&id.project_uuid, &id.daw_session_uuid);
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

        // B-102: broadcast 受信 closure 用に enable-resolved 値を clone しておく（以降の Arc
        // move より前に確保）。project_hash / instance_id / daw は enable 後不変。
        let cb_project_hash = project_hash.clone();
        let cb_post_iid = iid_str.clone();
        let cb_daw = daw_uuid.clone();
        // Exact pair ownership belongs to the plugin engine, not a restartable IO generation.
        // The restart closure below retains this Arc until the POST engine itself is destroyed.
        let pair_owner = Arc::clone(&self.pair_owner);

        // POST 固有の共有 Arc（hypha_post params と同型）。
        let instance_id = Arc::new(RwLock::new(iid_str));
        // Saved DAW documents with distinct non-empty daw_session_uuid values get distinct
        // engine cells. Empty/legacy sessions keep runtime daw empty and use host fallback.
        let project_hash_arc = Arc::clone(&self.project_hash_cell);
        // B-054: preset_available は engine と共有（PresetAvailable LED が poll）。
        let preset_available = Arc::clone(&self.preset_available);
        // paired_pre_target は engine と共有（keep() が set → POST Record の linkage に焼く）。
        let paired_pre_target = self.pair_binding.recording_pre();
        let pair_label = Arc::new(Mutex::new(String::new()));
        let daw_session_id = Arc::clone(&self.daw_session_id_cell);
        // pair_pre_name = self.pair_target（set_pair_target 優先 / 空なら identity.name で seed）。
        // io_thread と Arc 共有 → set_pair_target の live 反映 + keep() の select と同一値。
        self.pair_binding.seed_name_if_empty(name_str);
        let pair_pre_name = self.pair_binding.desired_name();
        // B-102: broadcast 受信 → 自身の keep/stop を発火する本物の closure（egui hypha_post と
        // 同一経路 / scope = 新↔新）。closure は Box 所有の engine を借用できないため、keep()/stop()
        // と同一の共有 free 関数 resolve_and_enter_keep / resolve_and_exit_stop を捕捉 Arc + enable
        // 値で呼ぶ。license は LiveLicense を live 読み（keep と同一 gate）。args (pre/post) は
        // 各 POST が自分の pair_target を再選定するため無視する。
        let trigger_pair_resolution: kirin_measure::TriggerPairResolutionFn = {
            let record_workflow_supported = self.supports_record_workflow();
            let record_sm = Arc::clone(&self.record_sm);
            let pair_target = self.pair_binding.desired_name();
            let paired = self.pair_binding.recording_pre();
            let license = self.license.clone();
            let project_hash = cb_project_hash.clone();
            let post_iid = cb_post_iid.clone();
            let daw = cb_daw.clone();
            let latched = self.pair_binding.latched_pre();
            // B-127: broadcast 受信 keep も engine cap を通す。cap 到達通知の宛先 Arc を capture。
            let record_error_message = Arc::clone(&self.record_error_message);
            let keep_action_notice = Arc::clone(&self.keep_action_notice);
            let keep_phase = Arc::clone(&self.keep_phase);
            let keep_phase_generation_started_at_ms =
                Arc::clone(&self.keep_phase_generation_started_at_ms);
            let keep_record_generation = Arc::clone(&self.keep_record_generation);
            Arc::new(
                move |_originator: &str, _started_at: &str, generation: &CaptureGeneration| {
                    if !record_workflow_supported {
                        return false;
                    }
                    let lic = license.refresh_for_user_action();
                    resolve_and_enter_keep(
                        lic,
                        &record_sm,
                        &pair_target,
                        &paired,
                        &project_hash,
                        &post_iid,
                        &daw,
                        &latched,
                        &record_error_message,
                        &keep_action_notice,
                        &keep_phase,
                        &keep_phase_generation_started_at_ms,
                        &keep_record_generation,
                        None,
                        Some(generation),
                    )
                },
            )
        };
        let trigger_stop_resolution: kirin_measure::TriggerStopResolutionFn = {
            let record_sm = Arc::clone(&self.record_sm);
            let paired = self.pair_binding.recording_pre();
            let project_hash = cb_project_hash;
            let post_iid = cb_post_iid;
            Arc::new(move |_pre: &str, _post: &str| {
                resolve_and_exit_stop(
                    &record_sm,
                    &paired,
                    &project_hash,
                    &post_iid,
                    Some(ReleaseReason::AllStop),
                );
            })
        };
        let pair_binding_generation: kirin_measure::PairBindingGenerationFn = {
            let pair_binding = Arc::clone(&self.pair_binding);
            Arc::new(move || pair_binding.generation())
        };
        let release_pair_binding_if_current: kirin_measure::ReleasePairBindingIfCurrentFn = {
            let pair_binding = Arc::clone(&self.pair_binding);
            Arc::new(move |expected_name, expected_generation| {
                pair_binding.release_if_current(expected_name, expected_generation)
            })
        };
        // B-118 Phase 3 (③): engine 保持の Arc を共有（io が書き JUCE getter が読む / 世代跨ぎ継続）。
        let record_error_message = Arc::clone(&self.record_error_message);
        let pair_claimed_at = Arc::clone(&self.pair_claimed_at);
        let pair_release_notice = Arc::new(RwLock::new(None));
        let is_playing = Arc::new(AtomicBool::new(false));
        let live_license = self.license.clone();
        // B-118: io spawn を restart-closure に包む（初回 spawn も watchdog 再起動も同一経路）。
        // 継続性（最重要）: 共有状態 Arc（pair_label / pair_claimed_at / pair_release_notice /
        // record_error_message / paired_pre_target / pair_pre_name / trigger 群 / latched_pre / 各 self.*）
        // は同一実体を capture し再起動後も同じ Arc を指す（closure 内での再生成禁止）。io_shutdown のみ
        // 世代毎に新規生成。
        // `ChannelLayout` は Copy。closure へは値で渡す（`&self` を捕まえない）。
        let layout = self.layout;
        let restart: RestartIoFn = {
            let record_sm = Arc::clone(&self.record_sm);
            let measure_result = Arc::clone(&self.measure_result);
            let delta_result = Arc::clone(&self.delta_result);
            let signal_state = Arc::clone(&self.signal_state);
            let is_playing = Arc::clone(&is_playing);
            let session_summary = Arc::clone(&self.session_summary);
            let record_trace_queue = Arc::clone(&self.record_trace_queue);
            let record_take_tracker = Arc::clone(&self.record_take_tracker);
            let record_ingress = Arc::clone(&self.record_ingress);
            let record_mark_queue = Arc::clone(&self.record_mark_queue);
            let push_overflow = Arc::clone(&self.push_overflow);
            let oversized_drop = Arc::clone(&self.oversized_drop); // B-125
            let pair_owner = Arc::clone(&pair_owner);
            let latched_pre = self.pair_binding.latched_pre();
            let spectrum = Arc::clone(&self.spectrum);
            let meter_delta_history = self.meter_delta_history.as_ref().map(Arc::clone);
            let comparison_audition_active = self.audition.active_handle();
            let sample_rate = self.sample_rate;
            Box::new(move || {
                let io_shutdown = Arc::new(AtomicBool::new(false));
                let handle = spawn_io_thread_post(
                    Arc::clone(&instance_id),
                    Arc::clone(&project_hash_arc),
                    sample_rate,
                    layout,
                    Arc::clone(&record_sm),
                    Arc::clone(&measure_result),
                    Arc::clone(&delta_result),
                    Arc::clone(&signal_state),
                    Arc::clone(&is_playing),
                    Arc::clone(&preset_available),
                    live_license.clone(),
                    Arc::clone(&paired_pre_target),
                    Arc::clone(&io_shutdown),
                    Arc::clone(&pair_label),
                    Arc::clone(&daw_session_id),
                    Arc::clone(&pair_pre_name),
                    Arc::clone(&trigger_pair_resolution),
                    Arc::clone(&trigger_stop_resolution),
                    Arc::clone(&pair_binding_generation),
                    Arc::clone(&release_pair_binding_if_current),
                    Arc::clone(&record_error_message),
                    Arc::clone(&pair_claimed_at),
                    Arc::clone(&pair_release_notice),
                    Arc::clone(&session_summary),
                    Arc::clone(&record_trace_queue),
                    Arc::clone(&record_take_tracker),
                    Arc::clone(&record_ingress),
                    Arc::clone(&record_mark_queue),
                    Arc::clone(&push_overflow), // B-076: per-Record dropped_samples
                    Arc::clone(&oversized_drop), // B-125: per-Record oversized block drop
                    Arc::clone(&pair_owner),    // exact pair survives IO worker restart
                    Arc::clone(&latched_pre),   // B-108: display/keep 共有ラッチ
                    Some(Arc::clone(&spectrum)),
                    meter_delta_history.as_ref().map(Arc::clone),
                    Arc::clone(&comparison_audition_active),
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
