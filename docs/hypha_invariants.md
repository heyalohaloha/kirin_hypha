# Hypha 不変条件表 — 2026-06-27

安定化計画 `hypha_stability_plan_2026-06-26.md` Step 1 の正本。
PRE/POST の状態遷移と pairing / identity / record / shell parity の不変条件を 1 か所に集約し、
各不変条件に対応する**実在テスト名**を紐づける。

## 使い方

- 不具合報告を受けたら、まずこの表で「**どの不変条件の違反か**」を特定する。
- 修正時は、該当不変条件の「紐づくテスト」を起点に、**増やすべきテスト**を決める。
- 「要追加」と書かれた行は、不変条件はあるが単体テストが未紐づけ＝次にテストを足すべき箇所。
- 不変条件↔テスト名は 2026-06-27 に実在確認済み。テストを改名・削除するときは本表も更新する。

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
| INV-I3 | prepareToPlay 同一フォーマット再呼び出しは engine 再利用、releaseResources は Record を落とさない（offline bounce 耐性） | `juce_prepare_to_play_reuses_record_engine_for_same_format` / `juce_release_resources_does_not_drop_record_state` |

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
