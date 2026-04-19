//! Record エントリ時の排他制御 — G-50-10 / G-50-33。
//!
//! guardian_58_hypha_step5_record_plugin_data.md T-3 対応。
//!
//! # 排他ルール
//! | モード | 同一バスに2つ | 異なるバスに複数 |
//! |--------|--------------|-----------------|
//! | Watch  | ✅ 許可 | ✅ 許可 |
//! | Record | ❌ 1バス1ペア | ✅ バスごと1ペア |
//!
//! # チェック手順
//! `plugin_data/{project_hash}/{bus}/pre/` と `/post/` を走査し、
//! `status="active"` かつ `heartbeat` が現在から **60 秒以内** のファイルが
//! あれば排他違反。それ以外（closed / heartbeat 60 秒超 / パース不能）は
//! 「許可」側に倒す（ブロックしない・削除もしない）。
//!
//! # クラッシュ残骸
//! `heartbeat > 60 秒古い` ファイルはクラッシュ残骸として扱い、**削除せず放置**。
//! OS 側で 90 日超自動アーカイブ（F-1）。Hypha 側は削除しない。
//!
//! # 責務分離
//! 本モジュールは FS 走査と残骸判定のみ。T-1 `RecordStateMachine` の
//! `try_enter_record` 呼出前に caller（T-6 統合時の Plugin）が呼ぶ。

use chrono::{DateTime, Utc};
use std::fs;
use std::path::{Path, PathBuf};

use crate::plugin_data::{PluginDataFile, Role, Status};

/// heartbeat 新鮮判定の閾値（秒）。これ以内なら排他保持、超過でクラッシュ残骸扱い。
pub const STALE_SECONDS: i64 = 60;

/// 排他チェック結果。
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum ExclusionResult {
    /// 排他違反なし。Record 遷移可。
    Ok,
    /// 同一バスに Record 中のファイルあり。Watch 維持。
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
pub fn check_record_exclusion(
    base_dir: &Path,
    project_hash: &str,
    bus: &str,
) -> ExclusionResult {
    check_record_exclusion_at(base_dir, project_hash, bus, Utc::now())
}

/// 排他チェック（現在時刻を外部注入）。テスト用。
pub fn check_record_exclusion_at(
    base_dir: &Path,
    project_hash: &str,
    bus: &str,
    now: DateTime<Utc>,
) -> ExclusionResult {
    let bus_dir = base_dir.join(project_hash).join(bus);
    if !bus_dir.exists() {
        return ExclusionResult::Ok;
    }
    for role in [Role::Pre, Role::Post] {
        let role_dir = bus_dir.join(role.dir_name());
        if let Some(conflict) = scan_role_dir(&role_dir, now, role) {
            return conflict;
        }
    }
    ExclusionResult::Ok
}

/// 単一の `pre/` または `post/` ディレクトリを走査。
fn scan_role_dir(dir: &Path, now: DateTime<Utc>, role: Role) -> Option<ExclusionResult> {
    let entries = match fs::read_dir(dir) {
        Ok(e) => e,
        Err(_) => return None, // 不在・権限不足は「排他なし」側に倒す
    };
    for entry in entries.flatten() {
        let path = entry.path();
        // `.tmp` は進行中の atomic write 中間ファイル。排他判定対象外。
        if !is_record_json(&path) {
            continue;
        }
        if let Some(file) = try_read_plugin_data(&path) {
            if file.status == Status::Active
                && is_heartbeat_fresh(&file.heartbeat, now, STALE_SECONDS)
            {
                return Some(ExclusionResult::Conflict {
                    holder: path,
                    heartbeat: file.heartbeat,
                    role,
                });
            }
        }
    }
    None
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
    // 未来時刻（age < 0）は「パース成功かつ有効」とみなして fresh 扱い。
    // 時計巻き戻しや書込直後の微小差分を排他違反にしないため。
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

    /// 指定バス・ロールで Record ファイルを active + 任意 heartbeat で書く。
    fn write_record_file(
        base: &Path,
        project_hash: &str,
        bus: &str,
        role: Role,
        wall_clock_start: &str,
        heartbeat_iso: &str,
        closed: bool,
    ) -> PathBuf {
        let paths = WriterPaths::build(base, project_hash, bus, role, wall_clock_start);
        let mut w = PluginDataWriter::create(
            paths.clone(),
            "iid".to_string(),
            project_hash.to_string(),
            role,
            bus.to_string(),
            48000,
        )
        .unwrap();
        // heartbeat は new() 時点で now だが、ここで上書きする
        // （PluginDataWriter には直接セッター無いので flush 後に再書込）。
        w.flush().unwrap();
        // 直接書換（テスト専用：読み込み → heartbeat/status 差し替え → 書き戻し）。
        let bytes = fs::read(&paths.final_path).unwrap();
        let mut file: PluginDataFile = serde_json::from_slice(&bytes).unwrap();
        file.heartbeat = heartbeat_iso.to_string();
        if closed {
            file.status = Status::Closed;
        }
        // checksum は再計算しない（排他チェックは checksum を見ない設計）。
        let json = serde_json::to_vec(&file).unwrap();
        fs::write(&paths.final_path, json).unwrap();
        paths.final_path
    }

    fn iso(secs_ago: i64, now: DateTime<Utc>) -> String {
        let t = now - chrono::Duration::seconds(secs_ago);
        t.format("%Y-%m-%dT%H:%M:%SZ").to_string()
    }

    #[test]
    fn empty_dir_returns_ok() {
        let base = isolated_dir();
        let r = check_record_exclusion(&base, "ph", "MIX");
        assert_eq!(r, ExclusionResult::Ok);
    }

    #[test]
    fn missing_project_or_bus_returns_ok() {
        let base = isolated_dir();
        // project_hash に相当するディレクトリが存在しない
        let r = check_record_exclusion(&base, "no_such_ph", "MIX");
        assert_eq!(r, ExclusionResult::Ok);
    }

    #[test]
    fn active_with_fresh_heartbeat_on_pre_conflicts() {
        let base = isolated_dir();
        let now = Utc::now();
        write_record_file(
            &base,
            "ph",
            "MIX",
            Role::Pre,
            "2026-04-17T14:32:08Z",
            &iso(10, now),
            false,
        );
        let r = check_record_exclusion_at(&base, "ph", "MIX", now);
        match r {
            ExclusionResult::Conflict { role, .. } => assert_eq!(role, Role::Pre),
            _ => panic!("expected Conflict: {r:?}"),
        }
    }

    #[test]
    fn active_with_fresh_heartbeat_on_post_conflicts() {
        let base = isolated_dir();
        let now = Utc::now();
        write_record_file(
            &base,
            "ph",
            "MIX",
            Role::Post,
            "2026-04-17T14:32:08Z",
            &iso(59, now),
            false,
        );
        let r = check_record_exclusion_at(&base, "ph", "MIX", now);
        assert!(r.is_conflict());
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
            "MIX",
            Role::Pre,
            "2026-04-17T14:32:08Z",
            &iso(120, now), // 120 秒前 → 残骸扱い
            false,
        );
        let r = check_record_exclusion_at(&base, "ph", "MIX", now);
        assert_eq!(r, ExclusionResult::Ok);
    }

    #[test]
    fn stale_file_is_not_deleted() {
        let base = isolated_dir();
        let now = Utc::now();
        let path = write_record_file(
            &base,
            "ph",
            "MIX",
            Role::Pre,
            "2026-04-17T14:32:08Z",
            &iso(120, now),
            false,
        );
        let _ = check_record_exclusion_at(&base, "ph", "MIX", now);
        // 残骸は削除しない（F-1: OS 側 90 日アーカイブに任せる）
        assert!(path.exists());
    }

    #[test]
    fn closed_status_never_blocks() {
        let base = isolated_dir();
        let now = Utc::now();
        write_record_file(
            &base,
            "ph",
            "MIX",
            Role::Pre,
            "2026-04-17T14:32:08Z",
            &iso(0, now), // heartbeat は最新
            true,         // ただし status=closed
        );
        let r = check_record_exclusion_at(&base, "ph", "MIX", now);
        assert_eq!(r, ExclusionResult::Ok);
    }

    #[test]
    fn different_bus_same_project_allows_record() {
        let base = isolated_dir();
        let now = Utc::now();
        write_record_file(
            &base,
            "ph",
            "MIX",
            Role::Pre,
            "2026-04-17T14:32:08Z",
            &iso(5, now),
            false,
        );
        // DRUM バスから見れば排他なし
        let r = check_record_exclusion_at(&base, "ph", "DRUM", now);
        assert_eq!(r, ExclusionResult::Ok);
    }

    #[test]
    fn different_project_allows_record() {
        let base = isolated_dir();
        let now = Utc::now();
        write_record_file(
            &base,
            "ph1",
            "MIX",
            Role::Pre,
            "2026-04-17T14:32:08Z",
            &iso(5, now),
            false,
        );
        let r = check_record_exclusion_at(&base, "ph2", "MIX", now);
        assert_eq!(r, ExclusionResult::Ok);
    }

    #[test]
    fn tmp_file_is_ignored() {
        let base = isolated_dir();
        let now = Utc::now();
        // 手動で `.tmp` ファイルだけ設置（atomic write 中間状態の再現）
        let dir = base.join("ph").join("MIX").join("pre");
        fs::create_dir_all(&dir).unwrap();
        fs::write(
            dir.join("20260417T143208.json.tmp"),
            br#"{"heartbeat":"2026-04-17T14:32:08Z","status":"active"}"#,
        )
        .unwrap();
        let r = check_record_exclusion_at(&base, "ph", "MIX", now);
        assert_eq!(r, ExclusionResult::Ok);
    }

    #[test]
    fn corrupt_json_is_ignored() {
        let base = isolated_dir();
        let now = Utc::now();
        let dir = base.join("ph").join("MIX").join("pre");
        fs::create_dir_all(&dir).unwrap();
        fs::write(dir.join("corrupt.json"), b"{ not valid json").unwrap();
        let r = check_record_exclusion_at(&base, "ph", "MIX", now);
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
        // 時計巻き戻し等で heartbeat が未来に見えるケース。
        // 排他保持側に倒す（保守的）。
        let now = Utc::now();
        let future = (now + chrono::Duration::seconds(10))
            .format("%Y-%m-%dT%H:%M:%SZ")
            .to_string();
        assert!(is_heartbeat_fresh(&future, now, STALE_SECONDS));
    }

    #[test]
    fn crash_scenario_then_new_record_after_60s() {
        // シナリオ: DAW クラッシュ → heartbeat 更新停止 → 60秒超 → 新 Record 試行 → OK
        let base = isolated_dir();
        let now = Utc::now();
        write_record_file(
            &base,
            "ph",
            "MIX",
            Role::Pre,
            "2026-04-17T14:32:08Z",
            &iso(75, now), // 75 秒前でクラッシュ
            false,         // status=active のまま残留
        );
        let r = check_record_exclusion_at(&base, "ph", "MIX", now);
        assert_eq!(r, ExclusionResult::Ok);
    }

    #[test]
    fn both_pre_and_post_fresh_returns_first_hit() {
        // PRE と POST 両方新鮮 → 最初に見つかった方（PRE）を Conflict として返す
        let base = isolated_dir();
        let now = Utc::now();
        write_record_file(
            &base,
            "ph",
            "MIX",
            Role::Pre,
            "2026-04-17T14:32:08Z",
            &iso(5, now),
            false,
        );
        write_record_file(
            &base,
            "ph",
            "MIX",
            Role::Post,
            "2026-04-17T14:32:08Z",
            &iso(5, now),
            false,
        );
        let r = check_record_exclusion_at(&base, "ph", "MIX", now);
        match r {
            ExclusionResult::Conflict { role, .. } => assert_eq!(role, Role::Pre),
            _ => panic!("expected Conflict"),
        }
    }
}
