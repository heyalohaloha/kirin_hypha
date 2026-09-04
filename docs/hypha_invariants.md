# Hypha 不変条件表 — 2026-06-27

安定化計画 `hypha_stability_plan_2026-06-26.md` Step 1 の正本。
PRE/POST の状態遷移と pairing / identity / record / shell parity の不変条件を 1 か所に集約し、
各不変条件に対応する**実在テスト名**を紐づける。

## PRE Display配送（INV-PD）

| ID | 不変条件 | 紐づくテスト |
|----|----------|--------------|
| INV-PD1 | PRE DisplayはPRE targetだけへlinkし、TRACE、POST、Watch、Record、Keep、Pairingを参照しない | `shell_parity` PRE Display source boundary / `pre_display_ui_contract` |
| INV-PD2 | audio threadだけがproject clockを書き、reader missは最後の完全snapshotを保持する | `KirinHyphaUiContractTests` ClockTap / `pre_display_runtime_test` ClockReaderState |
| INV-PD3 | GuideはEndまたは置換まで保持し、presence／ack lease失効や再生停止では消さない | `pre_display_runtime_test` repository clear / retained projection |
| INV-PD4 | 既存reader互換のpresence v1.0を維持し、独立capability v1.0がreceipt対応を宣言する。acknowledgement v1.2はprocess／project／session／Guideの完全identityをinstance別に返し、acceptedだけが投影状態を持ち、rejectedは固定public codeだけを持つ。ack不在presenceは受信失敗へ昇格しない | `pre_display_runtime_test` capability／receipt contract / OS `capabilityContract.test.cjs` / `acknowledgementContract.test.cjs` |
| INV-PD5 | `content_hash`はproducer identityとして保持し、runtimeを跨いだJSON再serializeでは検証しない。consumerはactive pointerの`artifact_sha256`でGuide artifactのbyte完全性を検証し、不一致時は旧Guideを保持してrejected receiptを返す | `pre_display_runtime_test` cross-boundary fixture／repository rejection / OS `crossBoundaryFixture.test.cjs` |
| INV-PD6 | presence／capability／ack leaseは現在時刻から10秒を超える未来をliveと認めず、破損したprivate cache leaseを継続再読込しない | `pre_display_runtime_test` safe lease write / OS `presenceContract.test.cjs` / `acknowledgementContract.test.cjs` / `store.test.cjs` |
| INV-PD7 | INSPECTは現在activeのFactをHELDより優先し、同一状態内だけでfocusを優先する | `pre_display_runtime_test` active versus focused held |
| INV-PD8 | PREのGuideは対象区間内だけ内部`sectionActive`を立てて2行を静的なflora色へ変え、区間外とINSPECT HELDでは通常色へ戻す。点滅、グロー、alpha animationを使わず、POSTとreceipt schemaに表示状態を持ち込まない | `KirinHyphaUiContractTests` PRE tone / `pre_display_runtime_test` active／HELD boundary / `shell_parity` PRE-only wiring |

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
| INV-T5 | 通常 TRACE shelf に publish できるのは、Drop WAVのhash/sample rate/sample countと結合済みで、PRE/POSTの100ms native slotが全件一致する実測ペアだけ。Drop前のrender clockは保持するがconsumer-visibleにはせず、DropがWAV sample 0..Nを供給した時点でproducerが完成させる。欠損slot・推定frame・duration/sample rate不一致は通常shelfへ出さない | `pair_finalize_keeps_render_clock_pending_until_drop_supplies_wav_axis` / `late_expected_reconcile_promotes_pending_render_clock_pair_to_wav_boundaries` / `pair_finalize_quarantines_render_clock_shorter_than_expected_wav` / `pair_finalize_quarantines_expected_wav_source_without_expected_metadata` / `expected_wav_duration_frames_ignore_late_trace_span` / `high_density_all_silent_trace_does_not_invent_missing_grid_slots` |
| INV-T6 | pairのwall-clock・曲線形状・LUFS/TP相関は位置合わせに使わない。producerが保持した同一host content sample位置またはBWF time referenceだけからWAV sample 0原点を決め、PRE/POSTの同一native slotを公開する。consumer側のshift/補間は位置を変えない | `missing_bext_uses_the_exact_render_start_not_a_dense_tail` / `missing_bext_plan_uses_producer_render_range_with_different_metric_shapes` / `pair_contract_requires_shared_slots_even_when_instance_diagnostics_differ` / `public_wav_axis_does_not_reintroduce_the_absolute_bwf_offset` / `pair_contract_rejects_shifted_or_non_dense_wav_grids` |
| INV-T7 | 1回のKeep/All Keepは1つのimmutable capture generationを作る。`pair_key == record_session_id`、`channel_key == persisted PRE instance_id`で、1世代内のpair/channelはともに一意。表示名は可変・未知でも有効で、任意のsemantic `channel_role`だけが自動STEM/root結合を制御する | `generation_separates_stable_channel_identity_from_optional_role` / `generation_rejects_two_posts_targeting_the_same_pre` / `ready_commit_requires_exact_pre_and_post_writer_claims` |

### 正本ロジックの所在

| 領域 | ファイル |
|------|----------|
| Pairing / discovery | `crates/kirin_measure/src/pairing_scope.rs` / `post_candidates.rs` / `pre_candidates.rs` |
| 表示 / Delta / ラッチ | `crates/kirin_measure/src/io_thread_post.rs`（`compute_latched_display_for_post_project`）/ `delta.rs` |
| Record / reservation | `crates/kirin_measure/src/reservation.rs` / `record_signal.rs` |
| All Keep / All Stop | `crates/kirin_measure/src/capture_generation_tx.rs` / `all_keep_signal.rs` / `all_stop_signal.rs` |
| License gating | `crates/kirin_measure/src/license.rs` |
| Shell parity / RT 安全 | `xtask/src/shell_parity.rs` / `xtask/src/rt_safety.rs` / `crates/kirin_hypha_ffi/src/lib.rs` |
| 復元順 (restore) | `crates/kirin_hypha_ffi/tests/pairing_candidates.rs`（`#[ignore]`） |

---

## 1. PRE 状態 × 観点 統一表（POST から見た PRE）

| PRE 状態 | 表示(Δ) | Keep / Arm | Delta 算出 | Record | All Keep |
|----------|---------|------------|------------|--------|----------|
| Active + fresh + 一意 + 名前一致 | Active（Δ表示） | 可 | 算出 | 可（Os） | 対象 |
| Inactive + fresh（POSTはActive） | `PreInactive` = muted Δ枠。exact pairは保持 | **可**（Arm 許可） | 非算出（PRE再開で同pairのΔへ復帰） | ラッチ凍結維持 | 対象（ラッチ先） |
| Bypassed | POST絶対値 + `ABS`。既存exact pairは保持 | 不可（新規候補から除外） | なし | 不可 | 除外 |
| Stale（t > TTL 10s） | ラッチ維持 + muted Δ/--- | 不可 | なし | 実消滅はlease終了でexit | 除外 |
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
| INV-P10 | 保存済みDAW stateはpair名とexact PRE locator（`project_hash` + `instance_id`）を同一snapshotで保持する。再起動時はregistryを再走査せず固定`{pre_project}/{pre_instance}/pre.json`だけを待つ。前プロセスのlease切れ残骸では解除せず、同じpath・instance・現hostのlive ownerを確認してから通常latchへ昇格する。exact fieldのない旧stateだけ一意名reconnectを使う | `saved_exact_pre_reconstructs_one_fixed_waiting_path_without_discovery` / `restored_exact_latch_waits_for_pre_loaded_later_without_name_rescan` / `restored_latch_blocks_name_fallback_until_exact_current_runtime_is_proven` / `saved_daw_state_restores_the_exact_pre_without_registry_rescan` |

---

## 3. 表示 / Delta / ラッチ（INV-D）

| ID | 不変条件 | 紐づくテスト |
|----|----------|--------------|
| INV-D1 | ラッチ維持中は選定済み pre.json を直読し Δ 算出（再スキャンで別 PRE を拾わない） | `read_pre_at_fresh_active` / `resolve_arm_target_uses_latch_over_ambiguous` |
| INV-D2 | POSTがActiveのままpaired PREだけInactiveになった場合、pairを外さず`PreInactive`へ遷移する。停止・無音・stale・一時不在を明示OFFと推測せず、muted Δ枠を維持する | `latch_inactive_switches_to_post_absolute_without_releasing_pair` / `paired_pre_off_is_absolute_while_inactive_and_stale_preserve_delta_layout` |
| INV-D3 | 停止→再生（inactive→active）でラッチ維持のまま live Δ(Active) | `latch_inactive_then_active_yields_live_delta`（B-194） |
| INV-D4 | pair 名変更/クリアで即アンラッチ→NoPre | `latch_name_change_unlatches` |
| INV-D5 | pre.json 実消滅でアンラッチ、同名 fresh 再出現で自動再ラッチ | `latch_delete_then_relatch` |
| INV-D6 | ラッチ先 t > TTL(10s)だけではpairを外さず、同exact instanceの一時停止として扱う | `latch_stale_beyond_ttl_keeps_pair_latched` |
| INV-D7 | pair 未指定で instance 1件は pass-through、2件以上は曖昧で NoPre | `single_instance_pass_through_when_no_pair` / `no_pre_dir_returns_no_pre_mode` |
| INV-D8 | POST非Active時は計測fieldを含まない最小post.jsonだけを書く。Inactive + exact pairはpairと直前ΔをStale保持し、Inactive + pairなし／BypassedはDelta=default(NoPre)へ消去する | `inv_d8_inactive_exact_pair_retains_frozen_delta_and_writes_minimal_json` / `inv_d8_inactive_without_pair_clears_delta_and_writes_minimal_json` / `inv_d8_bypassed_clears_delta_and_writes_minimal_json` |
| INV-D9 | POST Perceptual Δは同一schema/sample rate/100ms aperture/state epoch/channel定義/channel数/output-presentation endpointのPRE/POST Sharpnessだけを差分化する。Phase Dとresamplerは共通epochで一度だけresetし、以後は連続状態を保つ。欠測を補間せず、raw Δをclipしない | `exact_endpoint_epoch_and_aperture_are_required_for_difference` / `perceptual_pair_arms_one_future_epoch_and_joins_only_continuous_state` / `perceptual_discontinuity_clears_history_and_requires_a_new_shared_epoch` / `continuous_post_minus_pre_matches_mosqito_at_every_100ms_endpoint` |
| INV-D10 | paired PREの確定`Bypassed`だけがpairを保持したままPOST絶対値へ戻し、右上を淡い紫のASCII `ABS`にする。`PreInactive` / Stale / NoPre / poll失敗ではOFFを推測しない | `paired_pre_off_is_absolute_while_inactive_and_stale_preserve_delta_layout` / `juce_shell/tests/ui_contract_test.cpp` / `juce_shell/tests/ui_render_contract_test.cpp` |

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
| INV-R7 | Record TRACEは100msごとの実測slotだけを保持する。96kHz / 15.000sはWAV原点0に対して100〜15000ms（9600〜1,440,000 samples）の150点。先頭原点はreferenceであり偽の0ms metric frameを作らず、Measure tail欠損を無音frameで補完しない | `ninety_six_khz_fifteen_second_record_has_wav_duration_and_continuous_silent_trace` / `high_density_all_silent_trace_does_not_invent_missing_grid_slots` / `sparse_all_silent_trace_does_not_backfill_grid_slots` |
| INV-R8 | PRE/POST Record は record_signal の `session_id` を plugin_data の `record_session_id` に写し、同じ PairRecordSession として後段で照合できる | `signal_roundtrip_preserves_all_fields` / `resolve_record_session_id_reads_post_signal` / `record_session_id_serializes_only_when_set` |
| INV-R9 | Record TRACE queue がある現行経路では、queue が空の tick でも wall-clock snapshot を frames[] に混ぜない。Record frames[] は audio-time TRACE と同じ原点・同じ時計だけで作る | `run_record_tick_does_not_mix_wall_clock_when_trace_queue_is_empty` / `run_record_tick_legacy_without_trace_queue_uses_live_snapshot` / `pre_writes_records_plugin_data_json`（`#[ignore]`） |

---

## 5. All Keep / All Stop（INV-A）

| ID | 不変条件 | 紐づくテスト |
|----|----------|--------------|
| INV-A1 | All Keep/Stop は broadcast JSON 経由、30s で stale。keep と stop は独立 | `is_broadcast_stale_detects_30s_threshold` / `stop_broadcast_independent_of_keep_broadcast` / `read_broadcast_roundtrip` |
| INV-A2 | JUCE All Keep は engine 権威結果を使い、12-cap を事前 reject しない | `juce_all_keep_uses_authoritative_engine_result` |
| INV-A3 | AU/VST3 が別 project/DAW ID 棚を公開しても、同一host内で明示exact PRE（削除・再作成後は一意name fallback）が見えるPOST群はready数・pair所有・All Keep/Stop到達先を共有する。再接続claimは解決後の新exact IDで公開し、別hostと不可視/曖昧PRE claimは混ぜない | `operation_group_bridges_au_vst_shelves_by_exact_visible_pre` / `operation_group_rejects_other_host_and_nonvisible_exact_claim` / `stale_exact_claim_reconnects_only_by_unique_visible_name` / `candidate_keep_status_uses_resolved_self_claim_after_pre_recreation` / `juce_candidate_abi_bridges_split_shell_claims_and_all_keep`（後者は `#[ignore]`） |
| INV-A4 | Keep / All Keep の `true` は、immutable generation roster 全員の exact PRE/POST writer が実artifact生成と初回flushを完了した後だけ返す。`preparing.json` はproducerだけが参照し、Kirin OSは`active.json`だけを参照する。commit前の失敗はtransaction Dropが同generationのsignalだけをreleaseし、exact member棚へStopを通知して旧project pointerを復元する | `ownership_is_not_writer_readiness_until_initial_flush_is_published` / `ready_commit_requires_exact_pre_and_post_writer_claims` / `abandoned_preparation_releases_its_exact_signal_and_broadcasts_to_exact_shelf` / `rollback_never_releases_a_signal_that_was_replaced_by_another_generation` / `rollback_restores_previous_project_pointer_without_changing_active_generation` / `capstone_paired_record_output_and_linkage` / `juce_candidate_abi_bridges_split_shell_claims_and_all_keep`（後2件は`#[ignore]`） |

---

## 6. Identity / restore（INV-I）

| ID | 不変条件 | 紐づくテスト |
|----|----------|--------------|
| INV-I1 | `setStateInformation` が enable 前後どちらでも PRE名/POST pair target が watch JSON に反映 | `restored_pre_name_before_enable_is_written_to_pre_watch_json` / `restored_pre_name_after_enable_updates_pre_watch_json` / `restored_pair_target_before_enable_is_written_to_post_watch_json` / `restored_pair_target_after_enable_updates_post_watch_json`（全て `#[ignore]`） |
| INV-I2 | license loose 抽出は license フィールドのみ。未知/欠落/不正は Unknown(安全側) | `loose_parses_os_with_license_field_only` / `loose_parses_trial_as_unknown` / `loose_invalid_json_falls_back_to_unknown` / `loose_missing_license_field_falls_back_to_unknown` |
| INV-I3 | prepareToPlay 同一フォーマット再呼び出しは engine 再利用、releaseResources は engine を破棄しない。offline-end edge は専用 gate に委譲し、同一 Record 世代かつ最低 1 秒の実 offline process を見た場合だけ Stop 境界として使う | `juce_prepare_to_play_reuses_record_engine_for_same_format` / `juce_release_resources_keeps_engine_alive_while_polling_offline_end` / `juce_offline_end_auto_stop_defaults_on_and_post_recording_edge_gated` |
| INV-I4 | Record 終了の権威は手動 Stop / All Stop / offline-end auto-stop / idle auto-stop。offline-end auto-stop は既定 ON だが、同一 Record 世代の実 offline process gate と最低 1 秒 gate を満たす場合だけ手動 Stop 相当 cleanup へ進める。通常再生中の Active 音声や start-side preflight / 短い offline 断片は Stop 許可に使わない | `offline_autostop_defaults_on_with_explicit_disable` / `realtime_audio_before_offline_preflight_does_not_authorize_stop` / `juce_offline_end_auto_stop_defaults_on_and_post_recording_edge_gated` / `idle_autostop_due_boundary` |
| INV-I5 | VST3 hostの明示component OFFは`IComponent::setActive(false)`専用通知で保持し、engine生成前の通知もfresh handleへ再適用する。heartbeatが3秒staleになった場合だけ`Bypassed`へ確定する。通常停止・無音・一時再構成は`Inactive`、generic `releaseResources()`は利用者OFFを発明しない。reactivate後はprocess再開まで`Inactive` | `b544_host_component_state_only_changes_stalled_signal_reason` / `vst3_component_activation_is_distinct_from_release_resources` |

---

## 7. License gating（INV-L）

| ID | 不変条件 | 紐づくテスト |
|----|----------|--------------|
| INV-L1 | Record/plugin_data/preset/Keep開始は License::Os のみ。Sense/Unknown 不可（Unknown 安全側）。開始後にentitlementが変わってもStopは消さず、利用者が処理を終了できる。SpectrumのMARKは表示専用で、音声・Record・license状態を変更しない | `os_enables_record_features` / `sense_blocks_record_features` / `unknown_defaults_to_safe_side` / `verifySpectrumInteractionContract` |
| INV-L2 | GUIは可視性と実行可否を分離する。Keep／All KeepはOS未所有またはpair未選択でも消さずdisabled表示とし、実行時はRust license gateを再確認する | `post_controls_visibility_matches_rust_license_helpers` / `os_information_is_visible_for_every_unowned_license` / `post_controls_update_visibility_formula_is_pinned` / `verifyOsAccessUiContract` |
| INV-L3 | KeepはAU/VST3共通の固定スロットに表示し、pairまたはOS entitlement未成立時は非表示化せずdisabledにする | `shipped_au_and_vst3_compile_the_same_editor_processor_and_control_contract` / `verifyOsAccessUiContract` |
| INV-L4 | Kirin OS連携は未所有／所有未接続／接続済み未準備／準備完了を区別する。未所有ではREF tab全体をdisabledにする。REFのB／BlindはUI、利用者操作、Audio Thread出力で二重以上にgateし、Guide／Work接続／Capture Work添付は内部処理でもgateする。LEVEL／TIME／FREQ／SPACE、通常解析、ローカルCaptureは制限しない | `reference_has_ui_action_and_audio_thread_entitlement_gates` / `guide_and_work_capture_are_entitlement_gated_without_limiting_local_capture` / `verifyReferenceAuditionComponentContract` / `verifyOsAccessUiContract` |

---

## 8. Shell parity / RT 安全（INV-S）

| ID | 不変条件 | 紐づくテスト |
|----|----------|--------------|
| INV-S1 | 候補メニューは pair 名非依存で PRE 候補を列挙 | `candidate_menu_enumerates_pre_candidates_independent_of_current_pair` |
| INV-S2 | 候補選択は processor と入力欄へ即反映 | `candidate_selection_commits_pair_name_to_processor_and_field` |
| INV-S3 | JUCE Keep は 12-cap を事前 reject せず FFI reserve→count>MAX が権威。失敗は FFI error message を出す | `juce_keep_does_not_pre_reject_at_twelve_reservations` |
| INV-S4 | POST Record表示は世代付きsnapshotを使い、host inactive／finalize／Stop後も6セルを保持する。Max TP/Iは絶対値、選択M/S・PSR・Crest・SharpだけがΔ対象 | `juce_post_record_display_keeps_six_metrics_before_signal_fallback` |
| INV-S5 | RT 安全 — processBlock が呼べる C ABI は allowlist、push_samples に fs/alloc/blocking lock 再混入なし | `process_block_calls_only_rt_safe_ffi_surface` / `ffi_push_samples_core_avoids_io_allocation_and_blocking_locks` / `audio_thread_c_abi_wrappers_remain_thin` |
| INV-S6 | 出荷AU/VST3は同一JUCE processor/editor/control sourceと単一`HyphaUiContract`をcompileする。300×200矩形、全bounds、font family/size、palette、Watch current/MAX 6セル、Record 6セル、Keep/Stop表記をbundle build前にpure C++ testで固定する | `shipped_au_and_vst3_compile_the_same_editor_processor_and_control_contract` / `juce_shell/tests/ui_contract_test.cpp` |
| INV-S7 | POSTのoptional analysisはMeters=全停止、ATTACK=paired event、FREQ=FFT、SHARP=paired Sharpness、LIVE=local POST絶対値のinstance内排他状態。DAW process内のstable kernel leaseは2枠で、3個目は実際のlease保持pairを一行の`Both slots in use — Mix, Vocal`で表示して解析しない。2名を検証できない場合は`Both slots in use`へfail closedし、推測名や閉じる命令は出さない。ATTACK/FREQ/SHARP/LIVE切替は同じ枠を保持し、利用者がMETERSへ戻すかeditorを閉じたときだけ解放して待機中が自動取得する。TIMEはObservatoryで`HISTORY / ATTACK / SHARP / LIVE`を直接選択し、Compactでは一つのcycle controlへ畳む。FREQは一階層目のdomainに保つ。切替tooltipはASCII sentenceで固定し、Audio Threadは既存のlock-free copy以上を行わない | `exactly_two_post_analysis_runtimes_are_active_per_process_lease` / `analysis_owner_names_are_bounded_utf8_and_null_terminated` / `juce_shell/tests/ui_render_contract_test.cpp` / `verifyTimePageNavigationContract` / `perceptual_mode_is_exclusive_and_publishes_exact_100ms_apertures` / `post_absolute_timeline_needs_no_pair_and_never_creates_a_pre_request` / `disabled_runtime_does_not_start_worker_or_accept_audio` / `process_block_calls_only_rt_safe_ffi_surface` / `verifyPerceptualHistoryContract` |
| INV-S8 | SHARPはRust側の最新64実測点を非破壊batchで取得し、UI遅延で欠測を作らない。UIは60点/六秒、curve 5 Hz、数値 2 Hz。カーブは時間方向に同じ濃度。塗りの濃度は履歴の先頭・時刻・停止状態ではなく0線からの距離だけで決まり、正負とも0で薄く表示端へ向かうほど濃くなる。最新の実測点だけをドットで示す。真の欠測時は補間せず、前の表示runを捨てて新しく始める | `perceptual_join_recovers_every_exact_endpoint_after_a_delayed_presentation_tick` / `repeated_sixteen_frame_exchange_windows_accumulate_without_rewinding` / `verifyPerceptualHistoryContract` / `verifyPerceptualRenderingContract` |
| INV-S9 | optional Analysis専用exchange workerは試行完了ではなくrequest / readiness / PRE snapshotの実publishだけを進捗とする。通常IOの10 Hz serviceで約0.8秒publishが無ければ同じcoordinatorがnon-blocking exact tickを再試行し、1.5秒request lease失効前にFREQ/SHARPを救済する。Windowsの短命なAnalysis exchangeだけはpagefile-backed named shared mappingを使い、30 Hz経路をfilesystem filterのcreate/rename遅延から隔離する。macOSはatomic file transportを維持し、Watch / Record / `plugin_data`のfile contractは両OSとも変更しない。shared slotは二面化し、generation commit前とwriter競合中は直前の完全な値を保持して途中値を読まない。完了時点でlease切れのpublishはlive progressに数えない。lease内の一時失敗は最後のexact表示を保持するが、同じ古いendpointで期限を延長せず、期限を越えて補間・延長しない。各instanceで同時に走るanalysis modeは1個だけで、mutex poisonはsession resetまたは保持データから回復する | `named_mapping_round_trips_each_slot_without_files` / `contended_update_retains_the_last_complete_payload` / `reader_never_accepts_a_partially_published_payload` / `filesystem_stalls_never_hold_post_or_pre_session_locks` / `normal_io_supervisor_renews_freq_and_sharp_requests_when_worker_stops_progressing` / `failed_worker_attempts_do_not_hide_a_stalled_request_from_the_supervisor` / `transient_exchange_gap_holds_freq_and_sharp_presentations_until_the_lease_boundary` / `repeated_stale_sharpness_endpoint_does_not_extend_the_gap_hold` / `supervisor_rescues_before_the_request_lease_can_expire` / `poisoned_post_session_resets_instead_of_permanently_stalling_analysis` / `poisoned_pre_session_resets_instead_of_permanently_stalling_analysis` |
| INV-S10 | LIVEはPOST単独のLUFS-M／直近400ms True Peak／Sharpnessを同一100ms presentation endpointで保持する。Rust最大64点、表示6秒、source 10 Hz、curve 5 Hz、数値2 Hz。3指標は独立固定scaleで、差分・採点・警告色・時間平滑・PRE requestを持たない。疎な素材でLUFS-M／TPが測定floor未満のexact apertureは正本と数値欄で未定義のまま保持し、curveだけを固定scale下端に描く。数値欄の未定義tokenはWindows code pageに依存しないASCII `--`。短いforward欠測では前後のexact点とsample時刻を保持し、値・frameを補間せずstrokeだけを接続する。1点はdot表示、2点以上は各隣接点を実線分として結び、3系列すべてのplot内実画素をrender testで固定する。一時warmingは直前のverified fieldを消さない。後方transport、非互換format、6秒以上の断絶だけが新run | `absolute_mode_publishes_three_post_facts_on_one_exact_timeline` / `timeline_retains_exact_points_across_a_short_forward_gap` / `timeline_starts_a_new_run_on_backwards_transport_or_six_second_gap` / `sparse_source_keeps_exact_time_when_loudness_and_peak_are_below_floor` / `absolute_mode_retains_short_forward_gap_but_clears_backwards_and_mode_change` / `post_absolute_timeline_needs_no_pair_and_never_creates_a_pre_request` / `verifyAbsoluteTimelineContract` |
| INV-S11 | FREQ Focus Trailの継続性は二層。POSTは算出済みexact差分だけを固定8frame ringへ保持して短いUI stallをbatch回収する。ringを越えた欠測でもUIは六秒内の有効点とsample endpoint位置を維持する。work-surfaceのstrokeは周囲のexact点だけを結んでWindowsの描画欠けに見せず、欠測時刻はgap metadataのままで値を追加しない。PRE/POST双方の最新実測endpointが直前の表示endpointより下へ移ったtransport境界では、両workerが1 cadenceずれていても最新のexact交点から新runを始める。無音・一時warming・短いIO欠測・後方loopでは利用者が置いたMARKとFocus Trailの帯域lockを保持し、履歴だけを次のexact runから再開する。MARK／帯域lockを自動解除できるのはpair、sample rate、FFT layout、channel mode、pageの変更など観察定義が成立しなくなる場合だけで、通常の消去は利用者の明示操作に従う。片側だけの逆行、定義変更、六秒以上の断絶は従来どおりfail-closed。追加FFT、動的成長、filesystem poll、Audio Thread処理は禁止 | `fixed_eight_frame_window_recovers_short_ui_stalls_without_reanalysis` / `confirmed_backwards_transport_boundary_restarts_the_freq_timeline` / `staggered_backwards_transport_workers_restart_freq_at_their_exact_intersection` / `one_sided_lower_freq_result_cannot_move_the_presentation_backwards` / `verifySpectrumFocusTrailContract` / `verifySpectrumInteractionContract` |
| INV-S12 | FREQはhost rateをresampleせず、48k基準4096sampleの時間長でapertureを丸め、2倍以上の最小power-of-two FFTを使う。48kの4096/8192と結果は不変。schema/sample rate/aperture/FFTの全一致なしにPRE/POSTをjoinしない。3周期未満（約35Hz以下）は値を隠さず、周波数readoutだけASCII `~`で近似を示し、hoverで意味を説明する。curve／hover／MARK／Focus Trailは同じ`(index+0.5)/256` band centre定義を使う。MARKは現在の全帯域Δを固定し、大画面FREQの未lock Focus Trail領域は帯域clickで六秒Δ追跡を開始できることを表示する。CompactとPOST絶対Spectrumには同案内を増やさない。全tooltipは各editor sizeの内側で折り返し・再配置し、DAWへはみ出さない。Audio Threadは既存のlock-free copy以外を増やさない | `host_rate_layout_keeps_one_observation_time_without_resampling` / `host_rate_delta_remains_exact_for_identity_and_scalar_gain` / `host_rate_layout_metadata_is_exact_and_mismatch_fails_closed` / `high_rate_runtime_publishes_the_time_normalized_layout_without_drops` / `host_rate_fft_layout_keeps_four_x_pair_headroom` / `verifySpectrumPresentationContract` / `verifySpectrumInteractionContract` / `verifySpectrumFocusTrailContract` / `juce_shell/tests/ui_render_contract_test.cpp` |
| INV-S13 | `Show hover help`は初期ONの利用者共通表示設定。POST menuの明示操作だけが変更し、同じDAWで開いているPRE/POSTへ反映し次回起動にも保持する。OFFはTooltipWindowだけを閉じ、FREQ hover readout、click lock、Focus Trail、MARK、計測、Audio Threadを変えない。保存失敗時は操作したinstanceだけ即時反映し一時設定であることを通知する | `juce_shell/tests/ui_render_contract_test.cpp` / `hover_help_is_one_user_preference_without_touching_measurement_state` |
| INV-S14 | ATTACKはPOST限定・on-demandの独立routeで、同一exact content eventの符号付きPOST−PRE Onset Fluxと30ms Crest／Sample Peak、既存100ms Sharpnessだけを表示する。楽器分類、採点、推奨、相関shift、補間を行わない。旧`AnalysisViewMode`値0/1/2、既存FFI layout／transportを変更せず、専用request／payload／namespaceを単一二枠lease coordinatorへ接続する。欠測、PRE-only、POST-only、無音を区別し、Audio Threadの仕事を増やさない。独立holdout、exact lookahead、48k P95 50ms／max 75ms、二枠192k drop 0、macOS／Windows実機の全gateを通るまでroute、request、worker、lease acquisitionをdefault OFFとする | `docs/transient_delta_design.md` / transient analyzer・alignment・transport tests / ATTACK UI render and accessibility contracts |
| INV-S15 | 常設Meter SessionはRecord／Keep／Watch pass／pairing／Guide／editor lifetimeから独立し、Active音声だけで進む。Inactive／Bypassedは保持したままPauseし、明示Resetだけがgenerationを進めて統計を破棄する。current、10ms規格解析のMax M、session、L/R Peak／TP、3秒BAL／CORRは同じ100ms公開境界を持つ。clipはチャンネル別`abs(sample) >= 1.0`の連続runを1 eventとし、100ms境界で分割しない。Measure worker再起動とRecord pre-roll replayでResetや二重算入を起こさず、Audio Threadには新しいlock／alloc／copyを追加しない | `starts_empty_and_only_active_audio_advances_time` / `maximum_momentary_is_an_official_session_fact_until_explicit_reset` / `integrated_lra_peak_and_plr_share_one_session_fact` / `in_phase_inverse_and_balance_share_exact_three_second_window` / `one_sided_and_mono_are_explicit_not_invented_numeric_stereo` / `clip_events_are_channel_specific_contiguous_runs_across_observations` / `live_measure_worker_advances_pauses_and_resets_independent_session` / `drain_includes_tail_off_undercounts_on_matches_full` / `b118_measure_restart_recovers_via_watchdog` / `process_block_calls_only_rt_safe_ffi_surface` |
| INV-S16 | TIME履歴はM／S／TP／PLR／CORRの同一100ms事実を10Hz=10分、1Hz=2時間、0.1Hz=24時間の固定容量で保持する。I／MaxTPから作るPLRも各100ms境界で確定する。10Hzはexact値、低rateはmin／max／meanとfirst／last endpointを持つ集約値として区別する。DAW presentation sample endpointとrun IDを保持し、一観測がtransport jumpをまたぐ場合は虚偽の時刻を付けずそのhistory点だけskipする。host座標不在でもsession相対履歴は継続する | `product_capacities_match_ten_minutes_two_hours_and_twenty_four_hours` / `one_second_bucket_keeps_min_max_mean_and_exact_endpoints` / `run_change_flushes_partial_bucket_instead_of_joining_a_seek` / `arbitrary_callback_chunks_map_one_exact_observation_endpoint` / `observation_crossing_a_transport_jump_is_not_given_a_false_endpoint` / `history_retains_exact_and_multi_resolution_facts_while_editor_is_absent` / `verifyTimeHistoryContract` |
| INV-S17 | TIME Δはexact PREの現runtime ownerと専用`meter_history.json`を照合し、同一sample rate・presentation source・一意sample endpointの100ms点だけをPOST−PREする。PRE直近32点／POST直近64点の固定窓、pair/runtime/reset境界で履歴を破棄し、欠測・重複endpoint・transport jumpを補間せず別runにする。集約はexact Δの後に行い、PRE/POST集約値同士を減算しない。Audio Threadは変更しない | `atomic_publication_and_exact_target_join_work_end_to_end` / `joins_only_the_same_unique_presentation_endpoint` / `repeated_or_missing_endpoints_never_create_a_delta_fact` / `pair_change_discards_history_instead_of_blending_sources` / `delta_history_abi_is_post_only_and_empty_is_a_valid_fact` / `verifyTimeHistoryContract` |
| INV-S18 | Observatory UIはlive MeterSession計算lockを直接pollせず、Measure Threadが100ms完了境界後に独立publicationへ置いたimmutable snapshotだけを読む。publicationまたはWatchの一時poll missでは直前の完全frame/Crestを保持し、`---`へ書き換えない。Inactive、Bypassed、明示Reset/Emptyの成立snapshotだけが表示状態を変える | `publication_exposes_only_complete_replacements_and_never_waits_for_writer` / `live_measure_worker_advances_pauses_and_resets_independent_session` / `verifyObservatoryViewContract` |

---

## ゲート別の走らせ方

| ゲート | コマンド | 対象不変条件 |
|--------|----------|--------------|
| 出荷source正本 | `bash scripts/test_release_source.sh` | `kirin_measure` / FFI / 共通JUCE殻静的契約 / ignored 20+5件 / release-owned clippy |
| workspace互換 | `cargo test --workspace` | 旧nih-plug殻を含む非出荷workspaceの退行確認（出荷AU/VST3 parityの根拠には使わない） |
| FFI ignored（個別） | `cargo test -p kirin_hypha_ffi --test parity -- --ignored --test-threads=1` ＋ `--test pairing_candidates` | INV-P6 / INV-I1 / INV-R3,R4 / INV-A4（20件+5件を実測固定） |
| clippy（個別） | `cargo clippy -p kirin_measure -p kirin_hypha_ffi -p xtask --all-targets --locked -- -D warnings` | 出荷owned code品質基準 |

> 新しい pairing/表示の不具合を直すときは、本表に行を足し、対応テストを同時に書く
> （テスト名が決まらない修正は不変条件が曖昧なサイン）。
