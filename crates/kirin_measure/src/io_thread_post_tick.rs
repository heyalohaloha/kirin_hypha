//! One POST observation tick and exact-latch display resolution.

use std::fs;
use std::path::Path;
use std::sync::atomic::AtomicU8;
use std::sync::{Arc, Mutex};
use std::time::Instant;

use super::{
    compute_delta_for_pre_file, load_signal_state, read_pre_at, resolve_delta_for_non_active_post,
    resolve_delta_for_store, select_target_pre_for_arm_for_post_project_in_session,
    serialize_post_json_minimal_with_daw_owner_and_pair_instance,
    serialize_post_json_with_daw_owner_and_pair_instance, DeltaMode, DeltaResult, LatchedPre,
    MeasureResult, PostDiscoveryState, SignalState,
};

/// 1 ループの処理本体。
///
/// # B-021 Phase 1A: filesystem-discovery の優先順位
///
/// Pair未確定時だけ `discovery` が名前候補を1秒間隔で解決する。いったん exact PRE を
/// latchした後は、その1本の `pre.json` だけを読み、再走査しない。
///
/// `instance_dir` (POST 自身の post.json 書込先) は変更しない。POST 自身の
/// `project_uuid` で構築された path のままで、検出された PRE dir とは独立。
#[allow(clippy::too_many_arguments)]
/// B-108: latched-idle 表示値（Stale + 全Δ None + `last_active` クリア / 凍結値なし）。
fn delta_latched_idle() -> (DeltaResult, bool, Option<SignalState>) {
    (
        DeltaResult {
            mode: DeltaMode::Stale,
            ..Default::default()
        },
        true,
        None,
    )
}

/// B-108: 未ラッチ・ペアなし表示値（NoPre / `last_active` は resolve_delta_for_store がクリア）。
fn delta_no_pre() -> (DeltaResult, bool, Option<SignalState>) {
    (
        DeltaResult {
            mode: DeltaMode::NoPre,
            ..Default::default()
        },
        false,
        None,
    )
}

/// B-231: ラッチ先 PRE が明示 Bypassed。pair は維持し、表示は POST 単独へ戻す。
fn delta_pre_bypassed() -> (DeltaResult, bool, Option<SignalState>) {
    (
        DeltaResult {
            mode: DeltaMode::Bypassed,
            ..Default::default()
        },
        true,
        Some(SignalState::Bypassed),
    )
}

/// Pair binding remains authoritative while the paired PRE process is inactive. POST renders its
/// own absolute metrics until the same PRE instance resumes, then Δ resumes without re-pairing.
fn delta_pre_inactive() -> (DeltaResult, bool, Option<SignalState>) {
    (
        DeltaResult {
            mode: DeltaMode::PreInactive,
            ..Default::default()
        },
        true,
        Some(SignalState::Inactive),
    )
}

/// B-108: ラッチ意味論で表示Δを決める単一実装（`run_tick` の POST=Active 表示経路が呼ぶ）。
///
/// 戻り `(delta, store_directly, pre_signal_state)`:
/// - `store_directly = true` は **PREから差分を作れないがpairを維持する状態**。Stale は全Δ None、
///   PreInactive / Bypassed は POST 単独表示へ切り替える。いずれも `run_tick` は
///   `resolve_delta_for_store` を経由せずそのまま格納し、古い凍結Δの復活を防ぐ。
/// - `false` は従来どおり `resolve_delta_for_store`（Active は last_active 保存、active-pair の
///   fs-lag Stale は B-048 凍結保持、NoPre は last_active クリア）。
///
/// ラッチ規律（B-108）:
/// - Record 中はラッチ凍結（アンラッチ/再選定しない / W-284 self_check-skip と同型）。
///   PRE pre.json の stale/missing は表示上だけ latched-idle にし、Record は Stop / idle timeout
///   まで保持する。
/// - Watch 中: pair 名変更/クリアで即アンラッチ。ラッチ先 pre.json を直読するが、実消滅
///   （不在/stale>TTL/rename）は解除理由にしない。明示 pair 名が残る限り、ラッチ済み instance を
///   権威にして muted Δ/--- を返す。未ラッチ時だけ Arm ゲート（B-104）で初回解決する。
///   同名2台目が現れてもラッチ済みなら再選定しない。
#[cfg(test)]
pub(super) fn compute_latched_display(
    kirin_root: &Path,
    pair_pre_name: &str,
    post: &MeasureResult,
    pair_opt: Option<&str>,
    recording: bool,
    latched: &Mutex<Option<LatchedPre>>,
) -> Result<(DeltaResult, bool, Option<SignalState>), String> {
    compute_latched_display_for_post_project(
        kirin_root,
        pair_pre_name,
        "",
        "",
        post,
        pair_opt,
        recording,
        true,
        latched,
    )
}

#[allow(clippy::too_many_arguments)]
fn compute_latched_display_for_post_project(
    kirin_root: &Path,
    pair_pre_name: &str,
    post_project_hash: &str,
    post_daw_session_id: &str,
    post: &MeasureResult,
    _pair_opt: Option<&str>,
    recording: bool,
    allow_unlatched_resolution: bool,
    latched: &Mutex<Option<LatchedPre>>,
) -> Result<(DeltaResult, bool, Option<SignalState>), String> {
    let current = latched.lock().ok().and_then(|g| g.clone());

    if recording {
        // Record 中: ラッチ凍結（再解決/アンラッチしない）。
        let Some(l) = current else {
            return Ok(delta_no_pre()); // ラッチ無しで Record（理論上稀）→ 従来 NoPre。
        };
        return match read_pre_at(&l.pre_json) {
            Some(st) if st.fresh && st.active => {
                let (d, ss) = compute_delta_for_pre_file(&l.pre_json, post)?;
                Ok((d, false, ss))
            }
            Some(st) if st.signal_state == Some(SignalState::Bypassed) => Ok(delta_pre_bypassed()),
            Some(st) if st.signal_state == Some(SignalState::Inactive) => Ok(delta_pre_inactive()),
            // 一時 idle / stale / missing → latched-idle 表示。missing 単独では Record を閉じない。
            _ => Ok(delta_latched_idle()),
        };
    }

    // Watch 中。
    // (1) 名前変更/クリア → 即アンラッチ。
    let keep = current
        .as_ref()
        .is_some_and(|l| l.name == pair_pre_name && !l.instance_id.is_empty());
    if current.is_some() && !keep {
        if let Ok(mut g) = latched.lock() {
            *g = None;
        }
    }

    // (2) ラッチ維持中。Watch lease・snapshot freshnessは計測可否だけを決め、ユーザーが
    // 確定した exact binding は変更しない。Record中は上のfreeze分岐が先に返るため不変。
    if keep {
        let l = current.expect("keep implies current is Some");
        // A saved exact locator first waits for a current-process owner at that same path. The
        // previous DAW process deliberately leaves its JSON behind with a released lease; treating
        // that residue as a new deletion would discard the saved pair and re-enter name discovery.
        if l.readiness == crate::LatchedPreReadiness::RestoredWaiting
            && !crate::pairing_scope::confirm_restored_latch_runtime(latched)
        {
            return Ok(delta_latched_idle());
        }
        match read_pre_at(&l.pre_json) {
            // fresh + active → 通常 Δ。名前の一時不一致では解除しない。
            Some(st) if st.fresh && st.active => {
                let (d, ss) = compute_delta_for_pre_file(&l.pre_json, post)?;
                return Ok((d, false, ss));
            }
            // 明示 OFF は pair 維持のまま POST 単独表示に戻す。
            Some(st) if st.signal_state == Some(SignalState::Bypassed) => {
                return Ok(delta_pre_bypassed());
            }
            // PRE process inactive: keep exact binding, show POST absolute until it resumes.
            Some(st) if st.signal_state == Some(SignalState::Inactive) => {
                return Ok(delta_pre_inactive());
            }
            // stopped writer / stale / missing / rename → ラッチ維持のまま muted Δ/---。
            _ => return Ok(delta_latched_idle()),
        }
    }

    // (3) 未ラッチ（含む直前アンラッチ）→ pair 名があれば Arm ゲートで初回/再ラッチ。
    if pair_pre_name.is_empty() {
        return Ok(delta_no_pre());
    }
    if !allow_unlatched_resolution {
        return Ok(delta_no_pre());
    }
    match select_target_pre_for_arm_for_post_project_in_session(
        kirin_root,
        pair_pre_name,
        post_project_hash,
        post_daw_session_id,
    ) {
        Some(sel) => {
            let pre_json = sel.pre_json.clone();
            let project_dir = sel.project_dir.clone();
            let daw_session_id = sel.daw_session_id.clone();
            if let Ok(mut g) = latched.lock() {
                *g = Some(LatchedPre {
                    name: pair_pre_name.to_string(),
                    instance_id: sel.instance_id,
                    project_dir: project_dir.clone(),
                    pre_json: pre_json.clone(),
                    daw_session_id,
                    host_process_id: sel.host_process_id,
                    readiness: crate::LatchedPreReadiness::Confirmed,
                });
            }
            // 初回ラッチ直後の同 tick 表示。
            match read_pre_at(&pre_json) {
                Some(st) if st.fresh && st.active => {
                    let (d, ss) = compute_delta_for_pre_file(&pre_json, post)?;
                    Ok((d, false, ss))
                }
                Some(st) if st.signal_state == Some(SignalState::Bypassed) => {
                    Ok(delta_pre_bypassed())
                }
                Some(st) if st.signal_state == Some(SignalState::Inactive) => {
                    Ok(delta_pre_inactive())
                }
                _ => Ok(delta_latched_idle()),
            }
        }
        // 0 件 or 曖昧(2+) → 沈黙（NoPre）。
        None => Ok(delta_no_pre()),
    }
}

#[allow(clippy::too_many_arguments)]
pub(super) fn run_tick(
    // B-059: PRE 選定は select_target_pre(kirin_root) に一本化。project_dir_hint / discovery
    // (単一最新 dir の throttle/cache) は不要化（caller は据え置きで `_` 受け）。
    _project_dir_hint: &Path,
    kirin_root: &Path,
    discovery: &mut PostDiscoveryState,
    instance_dir: &Path,
    post_file: &Path,
    instance_id: &str,
    watch_owner_id: &str,
    post_result: &Arc<Mutex<MeasureResult>>,
    delta_result: &Arc<Mutex<DeltaResult>>,
    signal_state_atom: &Arc<AtomicU8>,
    pair_pre_name: &str,
    pair_claimed_at: f64,
    post_project_hash: &str,
    daw_session_id: &str,
    // B-108: recording=Record 中はラッチ凍結（アンラッチ/再選定しない）。latched=display と
    // keep/Arm が共有する単一ラッチ（io_thread が毎 tick 維持、keep が resolve_arm_target で読む）。
    recording: bool,
    latched: &Mutex<Option<LatchedPre>>,
) -> Result<(), String> {
    let state = load_signal_state(signal_state_atom);

    fs::create_dir_all(instance_dir).map_err(|e| format!("create_dir_all: {e}"))?;

    if state != SignalState::Active {
        let mut delta_locked =
            crate::sync_recovery::lock_recover(delta_result, "POST inactive delta");
        let previous_delta = delta_locked.clone();
        *delta_locked = resolve_delta_for_non_active_post(state, pair_pre_name, &previous_delta);

        // B-027 段階 3-B α-7-1 / Step 6: pair_pre_name は閉路 1 tick の snapshot。
        // Q-A7 採用案 A (post.json schema 拡張による cross-instance 公開)。
        // W-281: pair_claimed_at も同 tick snapshot を書き出す (後着優先 self check 軸)。
        let paired_pre_instance_id = crate::paired_pre_instance_id(latched).unwrap_or_default();
        let json = serialize_post_json_minimal_with_daw_owner_and_pair_instance(
            instance_id,
            state,
            pair_pre_name,
            pair_claimed_at,
            daw_session_id,
            watch_owner_id,
            &paired_pre_instance_id,
        );
        crate::atomic_file::write_bytes_atomic(post_file, json.as_bytes())
            .map_err(|e| format!("atomic write: {e}"))?;
        return Ok(());
    }

    // B-059: 表示=commit 一本化。commit (trigger_keep_internal) と同一の
    // `select_target_pre` で PRE を選定する。pair_pre_name 空 / 同名複数 / 不在 /
    // Inactive / 古t は None (= 表示 NoPre 沈黙 = commit 拒否)。
    let pair_opt = Some(pair_pre_name).filter(|s| !s.is_empty());

    let post = crate::sync_recovery::lock_recover(post_result, "POST Watch result").clone();

    // B-108: ラッチ意味論で表示Δを決める（select_target_pre 直呼びを廃止）。一度成立した結合は
    // 無音/停止/一時鮮度揺らぎ/同名2台目では NoPre に落とさず、解除は名前変更/クリアと PRE 実消滅のみ。
    let needs_resolution = !pair_pre_name.is_empty()
        && latched
            .lock()
            .map(|binding| binding.is_none())
            .unwrap_or(true);
    let resolution_now = Instant::now();
    let allow_unlatched_resolution = !needs_resolution || discovery.should_rescan(resolution_now);
    if needs_resolution && allow_unlatched_resolution {
        // Record the bounded discovery attempt even when no PRE exists. Otherwise an unresolved
        // selector would walk the live registry on every 100 ms IO tick.
        discovery.record_scan(resolution_now, None);
    }
    let (new_delta, store_directly, pre_signal_state) = compute_latched_display_for_post_project(
        kirin_root,
        pair_pre_name,
        post_project_hash,
        daw_session_id,
        &post,
        pair_opt,
        recording,
        allow_unlatched_resolution,
        latched,
    )?;

    // last_active 規律:
    // - store_directly（latched-idle / PRE bypassed / PRE inactive）→ そのまま格納し、
    //   古い凍結Δを復活させない。pair binding 自体は別管理なので維持される。
    // - それ以外 → resolve_delta_for_store（Active 保存 / active-pair fs-lag Stale は B-048 凍結保持
    //   / NoPre は last_active クリア）。
    {
        let mut delta_locked =
            crate::sync_recovery::lock_recover(delta_result, "POST active delta");
        let prev_last_active = delta_locked.last_active.clone();
        *delta_locked = if store_directly {
            new_delta
        } else {
            resolve_delta_for_store(new_delta, prev_last_active)
        };
    }

    // B-027 段階 3-B α-7-1 / Step 6: pair_pre_name は閉路 1 tick の snapshot
    // (Q-A7 採用案 A 完成 / cross-instance 公開機構)。
    // W-281: pair_claimed_at も同 tick snapshot (後着優先 self check 判定軸)。
    let paired_pre_instance_id = crate::paired_pre_instance_id(latched).unwrap_or_default();
    let json = serialize_post_json_with_daw_owner_and_pair_instance(
        instance_id,
        state,
        pre_signal_state,
        &post,
        pair_pre_name,
        pair_claimed_at,
        daw_session_id,
        watch_owner_id,
        &paired_pre_instance_id,
    );
    crate::atomic_file::write_bytes_atomic(post_file, json.as_bytes())
        .map_err(|e| format!("atomic write: {e}"))?;

    Ok(())
}
