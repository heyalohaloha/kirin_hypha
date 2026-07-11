# Hypha 不変条件表 — 2026-06-27

安定化計画 `hypha_stability_plan_2026-06-26.md` Step 1 の正本。
PRE/POST の状態遷移と pairing / identity / record / shell parity の不変条件を 1 か所に集約し、
各不変条件に対応する**実在テスト名**を紐づける。

## 使い方

- 不具合報告を受けたら、まずこの表で「**どの不変条件の違反か**」を特定する。
- 修正時は、該当不変条件の「紐づくテスト」を起点に、**増やすべきテスト**を決める。
- Record / TRACE の修正では、壊れた後の安全網ではなく、壊れない実測構造を優先する。
- 「要追加」と書かれた行は、不変条件はあるが単体テストが未紐づけ＝次にテストを足すべき箇所。
- 不変条件↔テスト名は 2026-06-27 に実在確認済み。テストを改名・削除するときは本表も更新する。

## 0. Measurement Truth Doctrine（INV-T）

Hypha は利用者に制限や複雑な操作を課さず、普通に計測した結果を実測データとして綺麗に表示する。
内部実装はその簡単さを支えるために厳密でなければならない。設計判断は次の順序を守る。

1. 欠損を作らない構造を作る。
2. PRE/POST の対、時計、完全性を事後推定ではなく session 固有の事実として保存する。
3. 実測されていない値を実測として補完・昇格しない。
4. 不完全なデータは publish 前に止め、理由を診断として残す。
5. 診断は将来欠損を作らないための内部根拠であり、利用者体験の主役にしない。

| ID | 不変条件 | 紐づくテスト |
|----|----------|--------------|
| INV-T1 | `record_expected/current.json` は claim で消費しない。session ごとに不変 snapshot を持ち、後続 bounce / STEM export が current を上書きしても既存 session の metadata は付け替わらない | `claimed_session_returns_immutable_snapshot_after_current_overwrite` / `refresh_pair_record_metadata_claims_expected_wav_after_signal_disappears` |
| INV-T2 | TRACE の LUFS-M / True Peak / Crest は 3軸すべて揃った時だけ実測 frame として扱う。部分欠損を代数的に埋めて complete 扱いしない | `measured_observed_partial_core_is_not_completed_for_trace_slot` / `partial_core_trace_samples_are_recorded_as_incomplete_slots` |
| INV-T3 | paired publish は PRE/POST 両側の session/link/sample rate/time axis/native duration/48k duration/trace grid が一致した時だけ通常 shelf に出す。片側だけ完全、または pair として不整合な Record は通常 TRACE 候補に出さない | `pair_finalize_quarantines_missing_trace_slots` / `pair_finalize_quarantines_duration_mismatched_pair` / `pair_finalize_quarantines_sample_rate_mismatched_pair` / `pair_finalize_quarantines_derived_48k_frame_count_mismatch` / `pair_finalize_quarantines_zero_frames_even_with_trace_diagnostics` |
| INV-T4 | PRE は partner 情報を失っても、自分の active writer scope から正当な All Stop を受けて writer を閉じる。Record を orphan にせず `.pair_pending` へ進める | `active_writer_all_stop_stops_pre_without_partner` / `active_writer_all_stop_ignores_broadcast_before_record_start` / `active_writer_all_stop_ignores_other_host_process` |
| INV-T5 | 通常 TRACE shelf に publish できるのは sample-count-ready な実測ペアだけ。TRACE frame 数と 100ms grid は `BounceTake` の duration/sample rate から逆算して検算する。`expected_wav` は補強情報であり、Hypha 内部の `wav_clock_native` / `render_clock_native` が完全な場合は missing expected で fallback 化しない。一方で expected が存在する場合の duration/sample rate 不一致、欠損、推定、expected_wav 由来を名乗る metadata 不在、startup recovery は通常 shelf へ出さず診断へ隔離する | `pair_finalize_commits_render_clock_without_expected_as_complete_trace` / `pair_finalize_quarantines_render_clock_shorter_than_expected_wav` / `pair_finalize_quarantines_short_trace_grid_even_when_internally_continuous` / `pair_finalize_quarantines_expected_wav_source_without_expected_metadata` / `expected_wav_duration_frames_ignore_late_trace_span` / `high_density_all_silent_trace_does_not_invent_missing_grid_slots` / `recover_orphan_tmps_recovers_valid_tmp` |
| INV-T6 | pair の同一 wall-clock / sample count は内容位置一致の代用にしない。Hypha は完成長より後ろに実際に届いた render context を全て保持し、LUFS-M と TP が同じ一意 lag を支持したら pair finalize 内で canonical WAV axis の全 frame を再構成してから通常 shelf へ出す。固定 latency 上限、consumer 補正、利用者の再操作を前提にしない。短い・一定・metric 間不一致の素材は推測しない | `aligned_pair_keeps_zero_offset` / `one_slot_compensation_preroll_is_detected` / `captured_tail_produces_full_canonical_wav_axis` / `pair_finalize_uses_tail_context_and_publishes_canonical_frames` / `short_or_constant_pair_is_not_guessed` / `metrics_disagreeing_on_lag_are_not_shifted` / `close_preserves_full_render_context_beyond_wav_for_pair_canonicalization` |

### 正本ロジックの所在

| 領域 | ファイル |
|------|----------|
| Pairing / discovery | `crates/kirin_measure/src/pairing_scope.rs` / `post_candidates.rs` / `pre_candidates.rs` |
| 表示 / Delta / ラッチ | `crates/kirin_measure/src/io_thread_post.rs`（`compute_latched_display_for_post_project`）/ `delta.rs` |
| Record / reservation | `crates/kirin_measure/src/reservation.rs` / `record_signal.rs` |
| All Keep / All Stop | `crates/kirin_measure/src/all_keep_signal.rs` / `all_stop_signal.rs` |
| License gating | `crates/kirin_measure/src/license.rs` |
| Shell parity / RT 安全 | `xtask/src/shell_parity.rs` / `xtask/src/rt_safety.rs` / `crates/kirin_hypha_ffi/src/lib.rs` |
| 復元順 (restore) | `crates/kirin_hypha_ffi/tests/pairing_candidates.rs`（`#[ignore]`） |

---

## 1. PRE 状態 × 観点 統一表（POST から見た PRE）

| PRE 状態 | 表示(Δ) | Keep / Arm | Delta 算出 | Record | All Keep |
|----------|---------|------------|------------|--------|----------|
| Active + fresh + 一意 + 名前一致 | Active（Δ表示） | 可 | 算出 | 可（Os） | 対象 |
| Inactive + fresh（停止/無音） | latched-idle = Stale（Δ非表示） | **可**（Arm 許可） | 非算出（再生で Active 化） | ラッチ凍結維持 | 対象（ラッチ先） |
| Bypassed | 除外 | 不可（候補除外） | なし | 不可 | 除外 |
| Stale（t > TTL 10s） | アンラッチ → NoPre | 不可 | なし | 実消滅は 60s poll が exit | 除外 |
| 同名複数（曖昧・未ラッチ） | NoPre（沈黙） | 不可（None） | なし | 不可 | — |
| 同名複数（ラッチ済み） | ラッチ先を維持 | ラッチ先 instance 不変 | ラッチ先で算出 | 凍結 | ラッチ先 |
| 不在 | NoPre | 不可 | なし | 実消滅で exit | — |

> 「表示」は `select_target_pre`（require_active=true）、「Keep/Arm」は `select_target_pre_for_arm`
> （require_active=false）。両者とも **一意・fresh・名前一致・非 Bypassed** が共通前提。
> 停止中の PRE を Keep でき、再生後に Δ が出る（Inactive 行）のが Step2 5c の核心不変条件。

---

## 2. Pairing / discovery（INV-P）

| ID | 不変条件 | 紐づくテスト |
|----|----------|--------------|
| INV-P1 | PRE 候補は「現在の pair 名」に依存せず全 fresh PRE を列挙する | `enumerate_active_pre_pair_candidates_flattens_all_active_pre_dirs` / `candidate_menu_enumerates_pre_candidates_independent_of_current_pair` |
| INV-P2 | target 選定は「一意の active+fresh」だけ Some。曖昧・空名・inactive・stale は None | `select_target_pre_unique_active_fresh_returns_some` / `select_target_pre_ambiguous_same_name_returns_none` / `select_target_pre_empty_name_returns_none` / `select_target_pre_inactive_excluded` / `select_target_pre_stale_t_excluded` |
| INV-P3 | 表示 == commit == keep target（single source） | `select_target_pre_display_equals_commit` |
| INV-P4 | Arm(Keep) は inactive-fresh も許可。bypassed/stale/曖昧/空名は除外 | `select_target_pre_for_arm_inactive_fresh_returns_some` / `select_target_pre_for_arm_bypassed_excluded` / `select_target_pre_for_arm_stale_t_excluded` / `select_target_pre_for_arm_ambiguous_returns_none` / `select_target_pre_for_arm_empty_name_returns_none` |
| INV-P5 | ラッチ後は同名2台目が現れても再選定しない（instance 不変） | `latch_invariant_to_second_same_name` / `resolve_arm_target_uses_latch_over_ambiguous` / `select_target_pre_for_arm_for_post_project_avoids_other_session_same_name` |
| INV-P6 | 既存 pair があっても別名2台目 PRE は候補から消えない | `post_project_with_existing_drum_still_lists_and_selects_second_mix` / `juce_candidate_abi_keeps_second_pre_visible_after_first_ready_post`（`#[ignore]`） |
| INV-P7 | POST scope 推定は pair 名が食い違うと fallback しない | `discover_pre_dirs_for_post_project_does_not_fallback_when_names_disagree` |
| INV-P8 | bypassed PRE は候補スキャンから除外。legacy(no signal_state) は active 扱い | `scan_pre_candidates_in_filters_only_bypassed` / `scan_pre_candidates_in_keeps_legacy_no_signal_state` / `scan_pre_candidates_in_keeps_active` |
| INV-P9 | 名前一致は exact・日本語対応・空名は pass-through | `filter_candidates_by_name_keeps_match` / `filter_candidates_by_name_matches_japanese` / `filter_candidates_by_name_drops_mismatch` / `filter_candidates_by_name_empty_passes_through` |

---

## 3. 表示 / Delta / ラッチ（INV-D）

| ID | 不変条件 | 紐づくテスト |
|----|----------|--------------|
| INV-D1 | ラッチ維持中は選定済み pre.json を直読し Δ 算出（再スキャンで別 PRE を拾わない） | `read_pre_at_fresh_active` / `resolve_arm_target_uses_latch_over_ambiguous` |
| INV-D2 | 停止/無音（inactive+fresh）は latched-idle=Stale。NoPre に落とさずラッチ維持 | `latch_idle_stays_stale_not_nopre` |
| INV-D3 | 停止→再生（inactive→active）でラッチ維持のまま live Δ(Active) | `latch_inactive_then_active_yields_live_delta`（B-194） |
| INV-D4 | pair 名変更/クリアで即アンラッチ→NoPre | `latch_name_change_unlatches` |
| INV-D5 | pre.json 実消滅でアンラッチ、同名 fresh 再出現で自動再ラッチ | `latch_delete_then_relatch` |
| INV-D6 | ラッチ先 t > TTL(10s) でアンラッチ | `latch_stale_beyond_ttl_unlatches` |
| INV-D7 | pair 未指定で instance 1件は pass-through、2件以上は曖昧で NoPre | `single_instance_pass_through_when_no_pair` / `no_pre_dir_returns_no_pre_mode` |
| INV-D8 | 非 Active state では Delta=default(NoPre) + 最小 post.json | 要追加（`run_tick` の `state != Active` 分岐に直接の単体テストなし） |

---

## 4. Record / reservation（INV-R）

| ID | 不変条件 | 紐づくテスト |
|----|----------|--------------|
| INV-R1 | Record 中はラッチ凍結（名前変更でもアンラッチしない）。実消滅は 60s poll が exit | `latch_frozen_during_record` |
| INV-R2 | pairing 予約は hard link で排他。同 pairing 二重は AlreadyReserved | `concurrent_reserve_exactly_one_wins` / `reserve_then_same_pairing_is_already_reserved` / `reserve_pairing` |
| INV-R3 | Drop/stop/timeout/cleanup で予約解放、再予約可能 | `release_allows_re_reserve` / `b127_reservation_released_on_drop`（`#[ignore]`） |
| INV-R4 | Keep は sweep→reserve→count>MAX(12) の権威的 cap。stale orphan は cap 前に sweep | `b127_keep_sweeps_stale_orphan_frames_before_cap`（`#[ignore]`） / `sweep_stale_reservations` / `thirteenth_rejected_under_concurrent_sweep` |
| INV-R5 | sweep は in-progress/claimed/fresh/marker 付きを保護、stale orphan のみ回収 | `sweep_does_not_delete_in_progress_or_claimed_reservation` / `sweep_grace_protects_recent_unparseable_frame` / `sweep_reclaims_orphan_but_protects_fresh_and_marker_backed` / `sweep_reclaims_mtime_stale_unparseable_frame` |
| INV-R6 | record_signal は pending→acknowledged→released。30s timeout で released | `full_post_to_pre_handshake_sequence` / `mark_acknowledged_updates_status_and_returns_true` / `mark_released_updates_status` / `timeout_fires_strictly_after_30s` / `sweep_stale_pending_removes_only_dead_pending` |
| INV-R7 | Record TRACE は Measure 観測だけに依存せず、RecordTakeTracker の native clock で終了境界まで 100ms grid を閉じる。96kHz / 15.000s は 0〜15000ms の 151 点になり、Measure tail 欠落は無音 frame として時間軸を残す | `record_take_clock_closes_96k_15s_trace_grid` / `record_take_tracker_snapshot_closes_missing_measure_tail` / `recorded_96k_15s_silent_trace_is_sample_count_ready_with_continuous_frames` |
| INV-R8 | PRE/POST Record は record_signal の `session_id` を plugin_data の `record_session_id` に写し、同じ PairRecordSession として後段で照合できる | `signal_roundtrip_preserves_all_fields` / `resolve_record_session_id_reads_post_signal` / `record_session_id_serializes_only_when_set` |
| INV-R9 | Record TRACE queue がある現行経路では、queue が空の tick でも wall-clock snapshot を frames[] に混ぜない。Record frames[] は audio-time TRACE と同じ原点・同じ時計だけで作る | `run_record_tick_does_not_mix_wall_clock_when_trace_queue_is_empty` / `run_record_tick_legacy_without_trace_queue_uses_live_snapshot` / `pre_writes_records_plugin_data_json`（`#[ignore]`） |

---

## 5. All Keep / All Stop（INV-A）

| ID | 不変条件 | 紐づくテスト |
|----|----------|--------------|
| INV-A1 | All Keep/Stop は broadcast JSON 経由、30s で stale。keep と stop は独立 | `is_broadcast_stale_detects_30s_threshold` / `stop_broadcast_independent_of_keep_broadcast` / `read_broadcast_roundtrip` |
| INV-A2 | JUCE All Keep は engine 権威結果を使い、12-cap を事前 reject しない | `juce_all_keep_uses_authoritative_engine_result` |

---

## 6. Identity / restore（INV-I）

| ID | 不変条件 | 紐づくテスト |
|----|----------|--------------|
| INV-I1 | `setStateInformation` が enable 前後どちらでも PRE名/POST pair target が watch JSON に反映 | `restored_pre_name_before_enable_is_written_to_pre_watch_json` / `restored_pre_name_after_enable_updates_pre_watch_json` / `restored_pair_target_before_enable_is_written_to_post_watch_json` / `restored_pair_target_after_enable_updates_post_watch_json`（全て `#[ignore]`） |
| INV-I2 | license loose 抽出は license フィールドのみ。未知/欠落/不正は Unknown(安全側) | `loose_parses_os_with_license_field_only` / `loose_parses_trial_as_unknown` / `loose_invalid_json_falls_back_to_unknown` / `loose_missing_license_field_falls_back_to_unknown` |
| INV-I3 | prepareToPlay 同一フォーマット再呼び出しは engine 再利用、releaseResources は engine を破棄しない。offline-end edge は専用 gate に委譲し、同一 Record 世代かつ最低 1 秒の実 offline process を見た場合だけ Stop 境界として使う | `juce_prepare_to_play_reuses_record_engine_for_same_format` / `juce_release_resources_keeps_engine_alive_while_polling_offline_end` / `juce_offline_end_auto_stop_defaults_on_and_post_recording_edge_gated` |
| INV-I4 | Record 終了の権威は手動 Stop / All Stop / offline-end auto-stop / idle auto-stop。offline-end auto-stop は既定 ON だが、同一 Record 世代の実 offline process gate と最低 1 秒 gate を満たす場合だけ手動 Stop 相当 cleanup へ進める。通常再生中の Active 音声や start-side preflight / 短い offline 断片は Stop 許可に使わない | `offline_autostop_defaults_on_with_explicit_disable` / `realtime_audio_before_offline_preflight_does_not_authorize_stop` / `juce_offline_end_auto_stop_defaults_on_and_post_recording_edge_gated` / `idle_autostop_due_boundary` |

---

## 7. License gating（INV-L）

| ID | 不変条件 | 紐づくテスト |
|----|----------|--------------|
| INV-L1 | Record/plugin_data/preset/Keep/Stop/Note は License::Os のみ。Sense/Unknown 不可（Unknown 安全側） | `os_enables_record_features` / `sense_blocks_record_features` / `unknown_defaults_to_safe_side` |
| INV-L2 | GUI ボタン可視性は Rust license ヘルパと C++ `PostControls::update` が値レベルで一致（os=(code==0)） | `post_controls_visibility_matches_rust_license_helpers`（B-195） / `sense_hint_visibility_is_sense_only_and_exclusive_with_keep` / `post_controls_update_visibility_formula_is_pinned` |
| INV-L3 | Keep ボタンは選択済み pair 名がある時のみ表示（`!recording && os && pairNonEmpty`） | `post_controls_keep_visibility_depends_on_selected_pair` |

---

## 8. Shell parity / RT 安全（INV-S）

| ID | 不変条件 | 紐づくテスト |
|----|----------|--------------|
| INV-S1 | 候補メニューは pair 名非依存で PRE 候補を列挙 | `candidate_menu_enumerates_pre_candidates_independent_of_current_pair` |
| INV-S2 | 候補選択は processor と入力欄へ即反映 | `candidate_selection_commits_pair_name_to_processor_and_field` |
| INV-S3 | JUCE Keep は 12-cap を事前 reject せず FFI reserve→count>MAX が権威。失敗は FFI error message を出す | `juce_keep_does_not_pre_reject_at_twelve_reservations` |
| INV-S4 | POST Record 表示は host inactive でも 6軸+N+Sharp を保持してから signal fallback | `juce_post_record_display_keeps_six_metrics_before_signal_fallback` |
| INV-S5 | RT 安全 — processBlock が呼べる C ABI は allowlist、push_samples に fs/alloc/blocking lock 再混入なし | `process_block_calls_only_rt_safe_ffi_surface` / `ffi_push_samples_core_avoids_io_allocation_and_blocking_locks` / `audio_thread_c_abi_wrappers_remain_thin` |

---

## ゲート別の走らせ方

| ゲート | コマンド | 対象不変条件 |
|--------|----------|--------------|
| 通常 | `cargo test --workspace` | INV-P/D/R/A/I2/L（unit）/ INV-S（xtask 静的） |
| FFI ignored | `cargo test -p kirin_hypha_ffi --test parity -- --ignored --test-threads=1` ＋ `--test pairing_candidates` | INV-P6 / INV-I1 / INV-R3,R4（`#[ignore]` 分） |
| clippy | `cargo clippy --workspace --all-targets --exclude baseview --exclude egui-baseview -- -D warnings` | 品質基準 |

> 未紐づけ（要追加）: INV-D8。新しい pairing/表示の不具合を直すときは、本表に行を足し、
> 対応テストを同時に書く（テスト名が決まらない修正は不変条件が曖昧なサイン）。
