//! Record エントリ時の排他制御 — G-50-10 / G-50-33（A-3 修正後 / B-027 段階 3-A 緩和）。
//!
//! # 排他ルール (B-027 段階 3-A / G-115-47)
//! 同一 `project_hash` 内で **最大 [`MAX_ACTIVE_PER_PROJECT`] = 12 件** の active
//! Record セッションを許容する。Daisuke 確認 (2026-05-04)。12 件以上で Conflict。
//!
//! 旧実装 (B-027 段階 2 まで) は「1 active で即 Conflict」だったが、Stage 3-A で
//! 多 PRE / 多 POST 環境を正式サポートするため count ベースに緩和。
//!
//! # チェック手順
//! 1. `{base}/{project_hash}/` 配下のサブディレクトリを列挙（`record_signal/` /
//!    `preset/` などの予約名は除外）
//! 2. 各 `{instance_id}/pre/` と `{instance_id}/post/` 配下の `*.json` を読み、
//!    `status="active"` かつ `heartbeat` が現在から 60 秒以内のファイルを **数える**
//! 3. 合計が `MAX_ACTIVE_PER_PROJECT` 以上で Conflict（最後に観測した active を holder に）
//!
//! # クラッシュ残骸
//! `heartbeat > 60 秒古い` ファイルはクラッシュ残骸として扱い、**削除せず放置**。
//! OS 側で 90 日超自動アーカイブ（F-1）。Hypha 側は削除しない。

use chrono::{DateTime, Utc};
use std::collections::HashSet;
use std::fs;
use std::path::{Path, PathBuf};

use crate::all_keep_signal::ALL_KEEP_SIGNAL_SUBDIR;
use crate::all_stop_signal::ALL_STOP_SIGNAL_SUBDIR;
use crate::plugin_data::{PluginDataFile, Role, Status};
use crate::record_signal::SIGNALS_SUBDIR;
use crate::preset::PRESET_SUBDIR;
use crate::reservation::{self, RESERVATION_SUBDIR};

/// heartbeat 新鮮判定の閾値（秒）。これ以内なら排他保持、超過でクラッシュ残骸扱い。
pub const STALE_SECONDS: i64 = 60;

/// 同一 `project_hash` 内に同時存在を許す **distinct pairing** の上限（G-115-364）。
///
/// B-127 是正: 旧実装は active marker を個別に数え PRE+POST 合算 12（=6 pair）で Conflict と
/// していた（pair 単位と marker 単位の不一致 bug）。現在は active+fresh marker を pairing key
/// （`(pre_iid, post_iid)` 正規化 / 双方向一致なら 2 marker=1 pairing・片側 None/不一致/相手
/// stale は各 lone=1）に畳み込み、`record_reservation/` の有効 reservation も同じ pairing key
/// 集合へ入れて **distinct pairing 数**を数える。12 pairing 以上で Conflict（13 ペア目を hard
/// reject）。値は 12 のまま（Daisuke 2026-05-04 確定）だが意味は「12 pairs」。
pub const MAX_ACTIVE_PER_PROJECT: usize = 12;

/// 排他チェック結果。
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum ExclusionResult {
    /// 排他違反なし。Record 遷移可。
    Ok,
    /// 同一 project_hash 内に Record 中のファイルあり。Watch 維持。
    Conflict {
        /// 排他保持中のファイル path。GUI 警告文には出さない（デバッグ用）。
        holder: PathBuf,
        /// そのファイルの `heartbeat` 値（ISO 8601）。
        heartbeat: String,
        /// PRE / POST どちら側が排他保持中か。
        role: Role,
    },
}

impl ExclusionResult {
    pub fn is_ok(&self) -> bool {
        matches!(self, Self::Ok)
    }

    pub fn is_conflict(&self) -> bool {
        matches!(self, Self::Conflict { .. })
    }
}

/// 排他チェック本体（現在時刻を自動取得）。
///
/// `base_dir` は `~/Library/Application Support/Kirin OS/plugin_data/` のような
/// 上位ディレクトリ。テストでは任意の tmp dir を渡せる。
pub fn check_record_exclusion(base_dir: &Path, project_hash: &str) -> ExclusionResult {
    check_record_exclusion_at(base_dir, project_hash, Utc::now())
}

/// 排他チェック（現在時刻を外部注入）。テスト用。
///
/// B-027 段階 3-A: count ベース。`active_count >= MAX_ACTIVE_PER_PROJECT` で
/// Conflict。`holder` には最後に観測した active のパスを入れる（ログ向け識別、
/// GUI には出さない）。
pub fn check_record_exclusion_at(
    base_dir: &Path,
    project_hash: &str,
    now: DateTime<Utc>,
) -> ExclusionResult {
    // advisory / 既存呼び出し点の意味: **既存** distinct pairing 数が MAX 以上 = 満杯（新規 keep は
    // 13 ペア目）。`>= MAX`。engine の authoritative 強制は reservation を先に作ってから
    // `count_distinct_pairings(...) > MAX` で判定する（resolve_and_enter_keep）。
    let (slots, latest_active) = distinct_pairing_slots(base_dir, project_hash, now);
    if slots.len() >= MAX_ACTIVE_PER_PROJECT {
        // holder は debug ログ専用（GUI 非表示）。marker 由来があればそれを、無ければ
        // （reservation のみで cap 到達した稀ケース）合成 holder を返す。
        if let Some((holder, heartbeat, role)) = latest_active {
            return ExclusionResult::Conflict { holder, heartbeat, role };
        }
        return ExclusionResult::Conflict {
            holder: base_dir.join(project_hash).join(RESERVATION_SUBDIR),
            heartbeat: "(reservation)".to_string(),
            role: Role::Post,
        };
    }
    ExclusionResult::Ok
}

/// B-127 (G-115-364): 同一 project_hash 内の **distinct pairing 数**（現在時刻）。
/// engine が reservation を作った**後**にこの値が `MAX_ACTIVE_PER_PROJECT` を **超えたら**
/// 自分が 13 ペア目（= reject）と判定するために使う。
pub fn count_distinct_pairings(base_dir: &Path, project_hash: &str) -> usize {
    count_distinct_pairings_at(base_dir, project_hash, Utc::now())
}

/// [`count_distinct_pairings`] の時刻注入版（テスト用）。
pub fn count_distinct_pairings_at(base_dir: &Path, project_hash: &str, now: DateTime<Utc>) -> usize {
    distinct_pairing_slots(base_dir, project_hash, now).0.len()
}

/// active+fresh marker と有効 reservation を distinct pairing key 集合に畳み込む（B-127 中核）。
/// 同じ pairing は marker と reservation の双方があっても 1 枠 / 双方向一致した PRE+POST 2 marker も
/// 1 枠 / 片側 None・不一致・相手 stale は各 lone=1 枠。`latest_active` は Conflict ログ holder 用。
fn distinct_pairing_slots(
    base_dir: &Path,
    project_hash: &str,
    now: DateTime<Utc>,
) -> (HashSet<String>, Option<(PathBuf, String, Role)>) {
    let mut slots: HashSet<String> = HashSet::new();
    let mut latest_active: Option<(PathBuf, String, Role)> = None;
    let project_dir = base_dir.join(project_hash);
    if let Ok(instance_entries) = fs::read_dir(&project_dir) {
        for entry in instance_entries.flatten() {
            let path = entry.path();
            if !path.is_dir() {
                continue;
            }
            // 予約サブディレクトリ（record_signal / record_reservation / preset / all_keep_signal /
            // all_stop_signal）は instance_id ではない。
            let Some(name) = path.file_name().and_then(|n| n.to_str()) else {
                continue;
            };
            if name == SIGNALS_SUBDIR
                || name == RESERVATION_SUBDIR
                || name == PRESET_SUBDIR
                || name == ALL_KEEP_SIGNAL_SUBDIR
                || name == ALL_STOP_SIGNAL_SUBDIR
            {
                continue;
            }
            for role in [Role::Pre, Role::Post] {
                let role_dir = path.join(role.dir_name());
                collect_marker_keys(&role_dir, now, role, &mut slots, &mut latest_active);
            }
        }
    }
    // 有効 reservation（active marker 出現前の窓を保持する枠）も同じ pairing key 集合へ。
    for key in reservation::scan_active_reservation_keys(base_dir, project_hash, now) {
        slots.insert(key);
    }
    (slots, latest_active)
}

/// 単一の `pre/` または `post/` ディレクトリを走査し、active+fresh marker の pairing key を
/// `slots` に挿入する（B-127: 個別 count ではなく pairing key 集合へ畳み込む）。
///
/// 双方向一致（POST `paired_pre`==PRE.iid かつ PRE `paired_post`==POST.iid）の 2 marker は同一
/// pairing key（`(pre_iid, post_iid)`）を生成し集合内で 1 枠に重複排除される。片側 None / 不一致 /
/// 相手 stale は各々 lone key（`lone-post:` / `lone-pre:`）となり個別に 1 枠（G-115-364 裁定 1/2）。
fn collect_marker_keys(
    dir: &Path,
    now: DateTime<Utc>,
    role: Role,
    slots: &mut HashSet<String>,
    latest_active: &mut Option<(PathBuf, String, Role)>,
) {
    let entries = match fs::read_dir(dir) {
        Ok(e) => e,
        Err(_) => return,
    };
    for entry in entries.flatten() {
        let path = entry.path();
        if !is_record_json(&path) {
            continue;
        }
        if let Some(file) = try_read_plugin_data(&path) {
            if file.status == Status::Active
                && is_heartbeat_fresh(&file.heartbeat, now, STALE_SECONDS)
            {
                slots.insert(marker_pairing_key(role, &file));
                *latest_active = Some((path, file.heartbeat, role));
            }
        }
    }
}

/// active marker から pairing key を作る。POST は相手 PRE の instance_id（`paired_pre_instance_id`）、
/// PRE は相手 POST の instance_id（`paired_post_instance_id`）と自身の instance_id を `(pre, post)`
/// 順に正規化（[`reservation::pairing_key`] と同一規則 = reservation/marker/双方向で同 key に収束）。
/// 相手 None は lone key（marker 個別に 1 枠）。
fn marker_pairing_key(role: Role, file: &PluginDataFile) -> String {
    match role {
        Role::Post => match file.paired_pre_instance_id.as_deref() {
            Some(pre) => reservation::pairing_key(pre, &file.instance_id),
            None => format!("lone-post:{}", file.instance_id),
        },
        Role::Pre => match file.paired_post_instance_id.as_deref() {
            Some(post) => reservation::pairing_key(&file.instance_id, post),
            None => format!("lone-pre:{}", file.instance_id),
        },
    }
}

/// `*.json` のみ対象（`.tmp` は除外）。
fn is_record_json(path: &Path) -> bool {
    path.extension()
        .and_then(|e| e.to_str())
        .map(|e| e == "json")
        .unwrap_or(false)
}

/// PluginDataFile をパース。失敗時は None（破損ファイル無視）。
fn try_read_plugin_data(path: &Path) -> Option<PluginDataFile> {
    let bytes = fs::read(path).ok()?;
    serde_json::from_slice(&bytes).ok()
}

/// heartbeat ISO 文字列が `now` から `max_age_secs` 秒以内なら true。
///
/// パース失敗 / 未来時刻の場合は安全側（= stale, false）。
pub fn is_heartbeat_fresh(heartbeat_iso: &str, now: DateTime<Utc>, max_age_secs: i64) -> bool {
    let hb = match DateTime::parse_from_rfc3339(heartbeat_iso) {
        Ok(d) => d.with_timezone(&Utc),
        Err(_) => return false,
    };
    let age = now.signed_duration_since(hb).num_seconds();
    if age < 0 {
        return true;
    }
    age <= max_age_secs
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::plugin_data::{PluginDataWriter, WriterPaths};
    use std::sync::atomic::{AtomicU64, Ordering};

    fn isolated_dir() -> PathBuf {
        static COUNTER: AtomicU64 = AtomicU64::new(0);
        let n = COUNTER.fetch_add(1, Ordering::Relaxed);
        let pid = std::process::id();
        let dir = std::env::temp_dir().join(format!("kirin_exclusion_test_{pid}_{n}"));
        let _ = fs::remove_dir_all(&dir);
        fs::create_dir_all(&dir).unwrap();
        dir
    }

    /// 指定 project_hash / instance_id / role で Record ファイルを active +
    /// 任意 heartbeat で書く。
    fn write_record_file(
        base: &Path,
        project_hash: &str,
        instance_id: &str,
        role: Role,
        wall_clock_start: &str,
        heartbeat_iso: &str,
        closed: bool,
    ) -> PathBuf {
        let paths = WriterPaths::build(base, project_hash, instance_id, role, wall_clock_start);
        let mut w = PluginDataWriter::create(
            paths.clone(),
            "iid".to_string(),
            project_hash.to_string(),
            instance_id.to_string(),
            role,
            None,
            48000,
            None,
            None,
        )
        .unwrap();
        w.flush().unwrap();
        let bytes = fs::read(&paths.final_path).unwrap();
        let mut file: PluginDataFile = serde_json::from_slice(&bytes).unwrap();
        file.heartbeat = heartbeat_iso.to_string();
        if closed {
            file.status = Status::Closed;
        }
        let json = serde_json::to_vec(&file).unwrap();
        fs::write(&paths.final_path, json).unwrap();
        paths.final_path
    }

    /// B-127: paired_pre / paired_post を設定した active marker を書く（pairing key 検証用）。
    fn write_paired_record(
        base: &Path,
        project_hash: &str,
        instance_id: &str,
        role: Role,
        heartbeat_iso: &str,
        paired_pre: Option<&str>,
        paired_post: Option<&str>,
    ) {
        let paths = WriterPaths::build(base, project_hash, instance_id, role, "2026-04-17T14:32:08Z");
        let mut w = PluginDataWriter::create(
            paths.clone(),
            "iid".to_string(),
            project_hash.to_string(),
            instance_id.to_string(),
            role,
            None,
            48000,
            paired_pre.map(|s| s.to_string()),
            paired_post.map(|s| s.to_string()),
        )
        .unwrap();
        w.flush().unwrap();
        let bytes = fs::read(&paths.final_path).unwrap();
        let mut file: PluginDataFile = serde_json::from_slice(&bytes).unwrap();
        file.heartbeat = heartbeat_iso.to_string();
        let json = serde_json::to_vec(&file).unwrap();
        fs::write(&paths.final_path, json).unwrap();
    }

    /// B-127: 双方向一致した 1 pairing を書く（PRE: paired_post=post_iid / POST: paired_pre=pre_iid・
    /// 両 active+fresh）。exclusion は 2 marker を 1 pairing key に畳む。
    fn write_bidi_pairing(base: &Path, ph: &str, pre_iid: &str, post_iid: &str, now: DateTime<Utc>) {
        write_paired_record(base, ph, pre_iid, Role::Pre, &iso(0, now), None, Some(post_iid));
        write_paired_record(base, ph, post_iid, Role::Post, &iso(0, now), Some(pre_iid), None);
    }

    fn iso(secs_ago: i64, now: DateTime<Utc>) -> String {
        let t = now - chrono::Duration::seconds(secs_ago);
        t.format("%Y-%m-%dT%H:%M:%SZ").to_string()
    }

    #[test]
    fn empty_dir_returns_ok() {
        let base = isolated_dir();
        let r = check_record_exclusion(&base, "ph");
        assert_eq!(r, ExclusionResult::Ok);
    }

    #[test]
    fn missing_project_returns_ok() {
        let base = isolated_dir();
        let r = check_record_exclusion(&base, "no_such_ph");
        assert_eq!(r, ExclusionResult::Ok);
    }

    /// B-027 段階 3-A: 1 active PRE は閾値 (12) 未満なので Ok。
    /// 旧 (B-027 段階 2 まで) の "1 件で即 Conflict" 仕様から差し替え。
    #[test]
    fn single_active_pre_under_threshold_is_ok() {
        let base = isolated_dir();
        let now = Utc::now();
        write_record_file(
            &base,
            "ph",
            "iid-A",
            Role::Pre,
            "2026-04-17T14:32:08Z",
            &iso(10, now),
            false,
        );
        let r = check_record_exclusion_at(&base, "ph", now);
        assert_eq!(r, ExclusionResult::Ok, "1 active < 12 threshold");
    }

    /// B-027 段階 3-A: 1 active POST は閾値 (12) 未満なので Ok。
    #[test]
    fn single_active_post_under_threshold_is_ok() {
        let base = isolated_dir();
        let now = Utc::now();
        write_record_file(
            &base,
            "ph",
            "iid-B",
            Role::Post,
            "2026-04-17T14:32:08Z",
            &iso(59, now),
            false,
        );
        let r = check_record_exclusion_at(&base, "ph", now);
        assert_eq!(r, ExclusionResult::Ok, "1 active < 12 threshold");
    }

    #[test]
    fn boundary_60s_is_fresh_61s_is_stale() {
        let now = Utc::now();
        assert!(is_heartbeat_fresh(&iso(60, now), now, STALE_SECONDS));
        assert!(!is_heartbeat_fresh(&iso(61, now), now, STALE_SECONDS));
    }

    #[test]
    fn stale_heartbeat_does_not_block() {
        let base = isolated_dir();
        let now = Utc::now();
        write_record_file(
            &base,
            "ph",
            "iid-stale",
            Role::Pre,
            "2026-04-17T14:32:08Z",
            &iso(120, now),
            false,
        );
        let r = check_record_exclusion_at(&base, "ph", now);
        assert_eq!(r, ExclusionResult::Ok);
    }

    #[test]
    fn closed_status_never_blocks() {
        let base = isolated_dir();
        let now = Utc::now();
        write_record_file(
            &base,
            "ph",
            "iid-closed",
            Role::Pre,
            "2026-04-17T14:32:08Z",
            &iso(0, now),
            true,
        );
        let r = check_record_exclusion_at(&base, "ph", now);
        assert_eq!(r, ExclusionResult::Ok);
    }

    /// B-027 段階 3-A: 同一 project_hash 内に 1 active が居ても閾値未満なので Ok。
    /// 旧 (B-027 段階 2 まで) の "別 instance でも同 project で即 Conflict" 仕様から
    /// 差し替え。閾値到達は `twelve_active_records_conflicts` で別途検証。
    #[test]
    fn different_instance_same_project_under_threshold_is_ok() {
        let base = isolated_dir();
        let now = Utc::now();
        write_record_file(
            &base,
            "ph",
            "iid-existing",
            Role::Pre,
            "2026-04-17T14:32:08Z",
            &iso(5, now),
            false,
        );
        let r = check_record_exclusion_at(&base, "ph", now);
        assert_eq!(r, ExclusionResult::Ok, "1 active in same project still under 12 limit");
    }

    #[test]
    fn different_project_allows_record() {
        let base = isolated_dir();
        let now = Utc::now();
        write_record_file(
            &base,
            "ph1",
            "iid-A",
            Role::Pre,
            "2026-04-17T14:32:08Z",
            &iso(5, now),
            false,
        );
        let r = check_record_exclusion_at(&base, "ph2", now);
        assert_eq!(r, ExclusionResult::Ok);
    }

    #[test]
    fn record_signal_subdir_is_not_treated_as_instance() {
        let base = isolated_dir();
        let now = Utc::now();
        // record_signal/ は instance_id ではないので走査対象外
        let signal_dir = base.join("ph").join(SIGNALS_SUBDIR);
        fs::create_dir_all(&signal_dir).unwrap();
        fs::write(signal_dir.join("post-1.json"), b"{}").unwrap();
        let r = check_record_exclusion_at(&base, "ph", now);
        assert_eq!(r, ExclusionResult::Ok);
    }

    #[test]
    fn preset_subdir_is_not_treated_as_instance() {
        let base = isolated_dir();
        let now = Utc::now();
        let preset_dir = base.join("ph").join(PRESET_SUBDIR);
        fs::create_dir_all(&preset_dir).unwrap();
        fs::write(preset_dir.join("p.json"), b"{}").unwrap();
        let r = check_record_exclusion_at(&base, "ph", now);
        assert_eq!(r, ExclusionResult::Ok);
    }

    /// B-027 段階 3-B α-7-4-B: all_keep_signal/ も instance_id ではないので走査対象外。
    #[test]
    fn all_keep_signal_subdir_is_not_treated_as_instance() {
        let base = isolated_dir();
        let now = Utc::now();
        let dir = base.join("ph").join(ALL_KEEP_SIGNAL_SUBDIR);
        fs::create_dir_all(&dir).unwrap();
        fs::write(dir.join("originator-A.json"), b"{}").unwrap();
        let r = check_record_exclusion_at(&base, "ph", now);
        assert_eq!(r, ExclusionResult::Ok);
    }

    #[test]
    fn tmp_file_is_ignored() {
        let base = isolated_dir();
        let now = Utc::now();
        let dir = base.join("ph").join("iid-A").join("pre");
        fs::create_dir_all(&dir).unwrap();
        fs::write(
            dir.join("20260417T143208.json.tmp"),
            br#"{"heartbeat":"2026-04-17T14:32:08Z","status":"active"}"#,
        )
        .unwrap();
        let r = check_record_exclusion_at(&base, "ph", now);
        assert_eq!(r, ExclusionResult::Ok);
    }

    #[test]
    fn corrupt_json_is_ignored() {
        let base = isolated_dir();
        let now = Utc::now();
        let dir = base.join("ph").join("iid-A").join("pre");
        fs::create_dir_all(&dir).unwrap();
        fs::write(dir.join("corrupt.json"), b"{ not valid json").unwrap();
        let r = check_record_exclusion_at(&base, "ph", now);
        assert_eq!(r, ExclusionResult::Ok);
    }

    #[test]
    fn invalid_heartbeat_iso_is_stale() {
        let now = Utc::now();
        assert!(!is_heartbeat_fresh("not-an-iso", now, STALE_SECONDS));
        assert!(!is_heartbeat_fresh("", now, STALE_SECONDS));
    }

    #[test]
    fn future_heartbeat_is_treated_as_fresh() {
        let now = Utc::now();
        let future = (now + chrono::Duration::seconds(10))
            .format("%Y-%m-%dT%H:%M:%SZ")
            .to_string();
        assert!(is_heartbeat_fresh(&future, now, STALE_SECONDS));
    }

    #[test]
    fn crash_scenario_then_new_record_after_60s() {
        let base = isolated_dir();
        let now = Utc::now();
        write_record_file(
            &base,
            "ph",
            "iid-crashed",
            Role::Pre,
            "2026-04-17T14:32:08Z",
            &iso(75, now),
            false,
        );
        let r = check_record_exclusion_at(&base, "ph", now);
        assert_eq!(r, ExclusionResult::Ok);
    }

    // ── B-027 段階 3-A: 12-active 上限の境界テスト (G-115-47) ─────────────────

    /// `MAX_ACTIVE_PER_PROJECT` 直前 (11) は Ok。
    #[test]
    fn eleven_active_records_under_threshold_is_ok() {
        let base = isolated_dir();
        let now = Utc::now();
        for i in 0..11 {
            let role = if i % 2 == 0 { Role::Pre } else { Role::Post };
            write_record_file(
                &base,
                "ph",
                &format!("iid-{i:02}"),
                role,
                "2026-04-17T14:32:08Z",
                &iso(5, now),
                false,
            );
        }
        let r = check_record_exclusion_at(&base, "ph", now);
        assert_eq!(r, ExclusionResult::Ok, "11 < 12 → Ok");
    }

    /// B-127 是正: `MAX_ACTIVE_PER_PROJECT` = 12 **pairings** ちょうどで Conflict。
    /// 12 双方向 pairing = 24 marker（12 PRE + 12 POST）が distinct pairing 12 に畳まれて Conflict。
    #[test]
    fn twelve_active_records_conflicts() {
        let base = isolated_dir();
        let now = Utc::now();
        for i in 0..MAX_ACTIVE_PER_PROJECT {
            write_bidi_pairing(&base, "ph", &format!("pre-{i:02}"), &format!("post-{i:02}"), now);
        }
        let r = check_record_exclusion_at(&base, "ph", now);
        assert!(
            r.is_conflict(),
            "{} distinct pairings (={} markers) should conflict, got {:?}",
            MAX_ACTIVE_PER_PROJECT,
            MAX_ACTIVE_PER_PROJECT * 2,
            r,
        );
    }

    /// B-127 bug fix: 6 双方向 pairing = 12 marker は **6 pairing** に畳まれ Ok（旧実装は
    /// marker 個別計数で 12 marker = Conflict と誤判定していた / G-115-363 bug）。
    #[test]
    fn six_pairings_under_cap_is_ok() {
        let base = isolated_dir();
        let now = Utc::now();
        for i in 0..6 {
            write_bidi_pairing(&base, "ph", &format!("pre-{i:02}"), &format!("post-{i:02}"), now);
        }
        let r = check_record_exclusion_at(&base, "ph", now);
        assert_eq!(r, ExclusionResult::Ok, "6 pairings (12 markers) < 12 pairs → Ok (旧 bug は Conflict)");
    }

    /// B-127 裁定1: 双方向一致した PRE+POST 2 marker は 1 pairing に畳まれる（distinct count=1）。
    #[test]
    fn bidirectional_pair_counts_as_one() {
        let base = isolated_dir();
        let now = Utc::now();
        write_bidi_pairing(&base, "ph", "pre-A", "post-A", now);
        // 2 marker(PRE+POST) だが 1 pairing → 11 個の lone を足してちょうど 12 で Conflict。
        for i in 0..11 {
            write_paired_record(&base, "ph", &format!("lone-{i:02}"), Role::Post, &iso(0, now), None, None);
        }
        assert!(
            check_record_exclusion_at(&base, "ph", now).is_conflict(),
            "1 pairing + 11 lone = 12 → Conflict",
        );
        // bidi pairing を除けば 11 lone のみ = Ok（pairing が 1 枠であることの裏取り）。
        let base2 = isolated_dir();
        for i in 0..11 {
            write_paired_record(&base2, "ph", &format!("lone-{i:02}"), Role::Post, &iso(0, now), None, None);
        }
        assert_eq!(check_record_exclusion_at(&base2, "ph", now), ExclusionResult::Ok, "11 lone < 12");
    }

    /// B-127: 有効 reservation も distinct pairing count に含む（active marker 出現前の枠保持）。
    #[test]
    fn reservation_counts_toward_cap() {
        let base = isolated_dir();
        let now = Utc::now();
        for i in 0..11 {
            write_paired_record(&base, "ph", &format!("lone-{i:02}"), Role::Post, &iso(0, now), None, None);
        }
        // 11 lone marker のみ = Ok。
        assert_eq!(check_record_exclusion_at(&base, "ph", now), ExclusionResult::Ok);
        // + 1 reservation（別 pairing）= 12 → Conflict。
        crate::reservation::reserve_pairing_at(&base, "ph", "pre-R", "post-R", now).unwrap();
        assert!(
            check_record_exclusion_at(&base, "ph", now).is_conflict(),
            "11 marker + 1 reservation = 12 → Conflict",
        );
    }

    /// B-127: 同一 pairing の reservation と active marker は同一 key で 1 枠に重複排除される。
    #[test]
    fn reservation_and_marker_same_pairing_dedup() {
        let base = isolated_dir();
        let now = Utc::now();
        write_bidi_pairing(&base, "ph", "pre-A", "post-A", now); // 1 pairing(2 marker)
        crate::reservation::reserve_pairing_at(&base, "ph", "pre-A", "post-A", now).unwrap(); // 同 pairing
        // marker(1 枠) + 同 pairing reservation(dedup) = 依然 1 枠。11 lone 追加で 12 → Conflict。
        for i in 0..11 {
            write_paired_record(&base, "ph", &format!("lone-{i:02}"), Role::Post, &iso(0, now), None, None);
        }
        assert!(
            check_record_exclusion_at(&base, "ph", now).is_conflict(),
            "1 pairing(marker+同 reservation を dedup) + 11 lone = 12 → Conflict",
        );
        assert_eq!(
            count_distinct_pairings_at(&base, "ph", now),
            12,
            "二重計上なし（marker と同 pairing reservation で 13 にならない）",
        );
    }

    /// B-127 裁定1/2: 片側 None / 不一致は各々 lone=1 枠（over-count 安全側）。
    /// POST→PRE 片方向（PRE が back-reference しない）= 2 marker が 2 枠（畳まれない）。
    #[test]
    fn one_directional_mismatch_counts_as_two() {
        let base = isolated_dir();
        let now = Utc::now();
        // POST は pre-X を指すが、PRE-X は post を指し返さない（paired_post=None）= 不一致。
        write_paired_record(&base, "ph", "post-Y", Role::Post, &iso(0, now), Some("pre-X"), None);
        write_paired_record(&base, "ph", "pre-X", Role::Pre, &iso(0, now), None, None);
        // 2 枠（lone-pre:pre-X と pre-X__post-Y）→ 10 lone 追加で 12 → Conflict。
        for i in 0..10 {
            write_paired_record(&base, "ph", &format!("lone-{i:02}"), Role::Post, &iso(0, now), None, None);
        }
        assert!(
            check_record_exclusion_at(&base, "ph", now).is_conflict(),
            "2 (片方向不一致) + 10 lone = 12 → Conflict（畳まれない=over-count 安全側）",
        );
    }

    /// PRE と POST が混ざって合計 12 でも Conflict（合計でカウント / role 別ではない）。
    #[test]
    fn mixed_pre_post_count_to_twelve_conflicts() {
        let base = isolated_dir();
        let now = Utc::now();
        // 6 PRE + 6 POST = 12
        for i in 0..6 {
            write_record_file(
                &base,
                "ph",
                &format!("iid-pre-{i:02}"),
                Role::Pre,
                "2026-04-17T14:32:08Z",
                &iso(5, now),
                false,
            );
        }
        for i in 0..6 {
            write_record_file(
                &base,
                "ph",
                &format!("iid-post-{i:02}"),
                Role::Post,
                "2026-04-17T14:32:08Z",
                &iso(5, now),
                false,
            );
        }
        let r = check_record_exclusion_at(&base, "ph", now);
        assert!(r.is_conflict(), "6+6 = 12 mixed PRE/POST → Conflict");
    }

    /// stale (heartbeat > 60s) はカウント対象外。fresh 11 + stale 5 = 計 16 でも Ok。
    #[test]
    fn stale_records_not_counted_toward_threshold() {
        let base = isolated_dir();
        let now = Utc::now();
        // fresh 11 件
        for i in 0..11 {
            write_record_file(
                &base,
                "ph",
                &format!("iid-fresh-{i:02}"),
                Role::Pre,
                "2026-04-17T14:32:08Z",
                &iso(5, now),
                false,
            );
        }
        // stale 5 件 (count されない)
        for i in 0..5 {
            write_record_file(
                &base,
                "ph",
                &format!("iid-stale-{i:02}"),
                Role::Pre,
                "2026-04-17T14:32:08Z",
                &iso(120, now),
                false,
            );
        }
        let r = check_record_exclusion_at(&base, "ph", now);
        assert_eq!(r, ExclusionResult::Ok, "11 fresh + 5 stale = 11 effective → Ok");
    }

    /// 異なる project_hash 配下で各 12 件 active でも、対象 project_hash 側の
    /// active が閾値未満なら Ok（project 隔離維持）。
    #[test]
    fn other_projects_do_not_count_toward_target_threshold() {
        let base = isolated_dir();
        let now = Utc::now();
        // 別 project に 12 件 (= 排他保持中) 配置
        for i in 0..MAX_ACTIVE_PER_PROJECT {
            write_record_file(
                &base,
                "other_ph",
                &format!("iid-{i:02}"),
                Role::Pre,
                "2026-04-17T14:32:08Z",
                &iso(5, now),
                false,
            );
        }
        // 対象 project には 1 件のみ
        write_record_file(
            &base,
            "ph",
            "iid-target",
            Role::Post,
            "2026-04-17T14:32:08Z",
            &iso(5, now),
            false,
        );
        let r = check_record_exclusion_at(&base, "ph", now);
        assert_eq!(r, ExclusionResult::Ok, "別 project の 12 件は対象に影響しない");
    }
}
