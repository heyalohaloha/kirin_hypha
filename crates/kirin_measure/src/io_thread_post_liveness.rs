//! POST Record liveness diagnostics and stopped-session addressing.

use super::PRE_LIVENESS_STALE_SECS;
use crate::pairing_scope::LatchedPre;
use crate::record::RecordStateMachine;
use crate::record_signal::{self, SignalStatus, ACK_TIMEOUT_SECONDS};
use crate::storage::StoragePaths;
use std::fs;
use std::path::Path;
use std::sync::{Arc, Mutex};
use std::time::SystemTime;

// ── ACK タイムアウト監視（G-60-02 / B-7）──────────────────────────────────

pub(super) fn poll_ack_timeout(
    project_hash: &str,
    instance_id: &str,
    record_sm: &Arc<RecordStateMachine>,
    pair_label: &Arc<Mutex<String>>,
    paired_pre_target: &Arc<Mutex<Option<String>>>,
) {
    let base = match StoragePaths::default_platform() {
        Ok(paths) => paths.plugin_data_dir(),
        Err(_) => return,
    };
    poll_ack_timeout_with_base(
        &base,
        project_hash,
        instance_id,
        record_sm,
        pair_label,
        paired_pre_target,
        chrono::Utc::now(),
    );
}

/// Legacy test helper. Runtime liveness uses the exact `LatchedPre::pre_json` path and never scans
/// project directories.
/// B-024 Group A / Gap-2: kirin_root 配下の全 project_hash を横断 scan して
/// `*/{pre_iid}/pre.json` の中で最新 mtime を返す。
///
/// cdylib 隔離下で PRE/POST の `project_hash` が乖離するため、POST 自身の
/// `project_hash` だけを見ても PRE pre.json は見つからない。`pre_discovery::
/// discover_active_pre_dir` と同じ走査方針を採用 (project_dir 全件 + 当該
/// instance_id 直結 read)。
///
/// R-28 機能的沈黙: 各エラー (read_dir 不能 / metadata 不能 / modified 不能) は
/// 当該 dir/file のみ skip。全件失敗 / 不在なら None。
#[cfg(test)]
pub(super) fn find_pre_json_mtime(kirin_root: &Path, pre_iid: &str) -> Option<SystemTime> {
    if pre_iid.is_empty() {
        return None;
    }
    // B-128 reopen / G-115-376: `pre_iid` は他 instance pre.json の content instance_id
    // (record_signal.rs:569 → pairing latch → `paired_pre_target` / 本 file:104 doc) 由来で、
    // content 由来 component を `.join()` する **唯一** の production path builder。within-base
    // DiD invariant (G-115-368) の例外であり、read-only でも path-unsafe 値 (`..` / 絶対 / 区切り /
    // 制御文字 / overlength / `_q_`) を join すると base 外の存在・mtime を観測する **mtime オラクル**
    // になる。よって `.join()` 前に within-base wall (`is_path_safe_component` = guard_path_component
    // が内部で使う同一述語) を通し、unsafe は **stat せず** pairing no-match (None) で返す。
    // 書込 builder の `guard_path_component` と違い quarantine 名で stat し続けず・event も出さない:
    // 本経路は利用者意図操作と非紐づきの read probe ゆえ R-28 surface 不要 (toast/event なし)。
    if !crate::path_identity::is_path_safe_component(pre_iid) {
        return None;
    }
    let project_entries = fs::read_dir(kirin_root).ok()?;
    let mut latest: Option<SystemTime> = None;
    for project_entry in project_entries.flatten() {
        let project_dir = project_entry.path();
        if !project_dir.is_dir() {
            continue;
        }
        let pre_json = project_dir.join(pre_iid).join("pre.json");
        let meta = match fs::metadata(&pre_json) {
            Ok(m) => m,
            Err(_) => continue,
        };
        if !meta.is_file() {
            continue;
        }
        let mtime = match meta.modified() {
            Ok(t) => t,
            Err(_) => continue,
        };
        latest = Some(match latest {
            Some(prev) if prev > mtime => prev,
            _ => mtime,
        });
    }
    latest
}

/// B-024 Group A / Gap-2: POST 側 PRE 死活監視 sub-tick の本体。
///
/// `record_sm` が Record 中で、かつ `paired_pre_target` が `Some(pre_iid)` のとき:
///   1. `find_pre_json_mtime(kirin_root, pre_iid)` で PRE pre.json の最新 mtime を取得
///   2. `now - mtime > PRE_LIVENESS_STALE_SECS` (60 秒 / G-50-33) または mtime 不在を検出
///   3. 検出時も Record は維持する。stem/offline export 後は DAW が process 更新を止めるため、
///      ここで `exit_record_full` すると Keep が利用者の Stop 前に消える。
#[cfg(test)]
pub(super) fn poll_pre_liveness(
    kirin_root: &Path,
    project_hash: &str,
    self_post_iid: &str,
    record_sm: &Arc<RecordStateMachine>,
    pair_label: &Arc<Mutex<String>>,
    paired_pre_target: &Arc<Mutex<Option<String>>>,
) {
    if !record_sm.is_recording() {
        return;
    }
    let Some(pre_iid) = paired_pre_target.lock().ok().and_then(|g| g.clone()) else {
        return;
    };
    let plugin_data_root = match StoragePaths::default_platform() {
        Ok(paths) => paths.plugin_data_dir(),
        Err(_) => return,
    };
    poll_pre_liveness_at(
        kirin_root,
        &plugin_data_root,
        project_hash,
        self_post_iid,
        &pre_iid,
        record_sm,
        pair_label,
        paired_pre_target,
        SystemTime::now(),
    );
}

/// `poll_pre_liveness` の純粋ロジック版 (テスト容易性のため `now` と `plugin_data_root`
/// を注入)。Production は `poll_pre_liveness` を経由する。
#[allow(clippy::too_many_arguments)]
#[cfg(test)]
pub(super) fn poll_pre_liveness_at(
    kirin_root: &Path,
    _plugin_data_root: &Path,
    _project_hash: &str,
    self_post_iid: &str,
    pre_iid: &str,
    _record_sm: &Arc<RecordStateMachine>,
    _pair_label: &Arc<Mutex<String>>,
    _paired_pre_target: &Arc<Mutex<Option<String>>>,
    now: SystemTime,
) {
    let stale = match find_pre_json_mtime(kirin_root, pre_iid) {
        Some(mtime) => match now.duration_since(mtime) {
            Ok(d) => d.as_secs() > PRE_LIVENESS_STALE_SECS,
            Err(_) => false, // future mtime (clock skew): fresh 扱い
        },
        None => true, // pre.json 不在 = PRE 既に消失
    };
    if !stale {
        return;
    }
    log::warn!(
        "[POST liveness] PRE pre.json stale > {}s — keeping record armed (partner_pre_iid={}, post_iid={})",
        PRE_LIVENESS_STALE_SECS,
        pre_iid,
        self_post_iid
    );
}

/// Record liveness diagnostic on one producer-selected PRE path. This never changes Record state:
/// offline bounce can legitimately stop Watch updates, and only Stop/Drop/idle timeout own the
/// Record lifecycle.
pub(super) fn poll_latched_pre_liveness(
    self_post_iid: &str,
    record_sm: &Arc<RecordStateMachine>,
    latched: &Mutex<Option<LatchedPre>>,
) {
    if !record_sm.is_recording() {
        return;
    }
    let Some(pre) = latched.lock().ok().and_then(|binding| binding.clone()) else {
        return;
    };
    let stale = match fs::metadata(&pre.pre_json)
        .ok()
        .filter(|metadata| metadata.is_file())
        .and_then(|metadata| metadata.modified().ok())
    {
        Some(mtime) => SystemTime::now()
            .duration_since(mtime)
            .map(|age| age.as_secs() > PRE_LIVENESS_STALE_SECS)
            .unwrap_or(false),
        None => true,
    };
    if stale {
        log::warn!(
            "[POST liveness] exact PRE pre.json stale > {}s — keeping record armed (partner_pre_iid={}, post_iid={})",
            PRE_LIVENESS_STALE_SECS,
            pre.instance_id,
            self_post_iid
        );
    }
}

pub(super) fn poll_ack_timeout_with_base(
    base: &Path,
    project_hash: &str,
    instance_id: &str,
    _record_sm: &Arc<RecordStateMachine>,
    _pair_label: &Arc<Mutex<String>>,
    _paired_pre_target: &Arc<Mutex<Option<String>>>,
    now: chrono::DateTime<chrono::Utc>,
) {
    let Some(signal) = record_signal::read_signal(base, project_hash, instance_id) else {
        return;
    };
    if signal.status != SignalStatus::Pending {
        return;
    }
    if !record_signal::is_timed_out(&signal, now, ACK_TIMEOUT_SECONDS) {
        return;
    }
    log::warn!(
        "[IOThread POST] ACK timeout ({}s) — keeping Record armed",
        ACK_TIMEOUT_SECONDS
    );
}

pub(super) fn release_record_reservation(
    base: &Path,
    project_hash: &str,
    post_iid: &str,
    paired_pre_target: &Arc<Mutex<Option<String>>>,
    reason: &str,
) {
    let released_pre = paired_pre_target.lock().ok().and_then(|g| g.clone());
    if let Some(pre) = released_pre.as_deref() {
        crate::reservation::release_pairing(base, project_hash, pre, post_iid);
        log::info!(
            "[IOThread POST] reservation released: reason={} pre={} post={}",
            reason,
            pre,
            post_iid
        );
    }
}

/// Build the POST GUI label from the authoritative PRE name or short instance identity.
pub fn format_pair_label(paired_pre_name: &str, target_id: &str) -> String {
    if !paired_pre_name.is_empty() {
        format!("pair: {}", paired_pre_name)
    } else {
        let short: String = target_id.chars().take(8).collect();
        format!("pair: {}", short)
    }
}
