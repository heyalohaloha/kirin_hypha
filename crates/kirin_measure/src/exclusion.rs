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
use std::path::{Path, PathBuf};

use crate::plugin_data::Role;
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
    _now: DateTime<Utc>,
) -> ExclusionResult {
    // G-115-365: cap 真実源 = O_EXCL 枠ファイルの**物理的存在のみ**（marker JSON を数えない /
    // 第二の独立 count を持たない）。advisory 用は **既存**枠数 >= MAX で満杯（新規 keep は 13 ペア目）。
    // engine の authoritative 強制は reservation を先に作ってから `count_distinct_pairings(...) > MAX`
    // で判定する（resolve_and_enter_keep）。`now` は枠存在ベースのため不使用。
    if reservation::count_frames(base_dir, project_hash) >= MAX_ACTIVE_PER_PROJECT {
        // holder は debug ログ専用（GUI 非表示）。枠存在ベースのため合成 holder を返す。
        return ExclusionResult::Conflict {
            holder: base_dir.join(project_hash).join(RESERVATION_SUBDIR),
            heartbeat: "(reservation)".to_string(),
            role: Role::Post,
        };
    }
    ExclusionResult::Ok
}

/// G-115-365: 同一 project_hash 内の **distinct pairing 数 = O_EXCL 枠ファイルの物理存在数**。
/// 真実源は枠存在のみ（[`reservation::count_frames`] / parse 失敗枠も存在側で数える・TTL を引かない）。
/// engine が reservation を作った**後**にこの値が `MAX_ACTIVE_PER_PROJECT` を **超えたら** 13 ペア目
/// （= reject）と判定するために使う。lifecycle（lone=1枠 / 双方向 pair=1枠共有 / 不活性 stale で解放）が
/// 枠の数を決め、本 count はそれを読むだけ。
pub fn count_distinct_pairings(base_dir: &Path, project_hash: &str) -> usize {
    reservation::count_frames(base_dir, project_hash)
}

/// [`count_distinct_pairings`] の時刻注入版（テスト互換のため `now` を受けるが、枠存在は時刻非依存）。
pub fn count_distinct_pairings_at(base_dir: &Path, project_hash: &str, _now: DateTime<Utc>) -> usize {
    reservation::count_frames(base_dir, project_hash)
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
    use crate::reservation::{reserve_pairing_at, RESERVATION_SUBDIR, RESERVATION_TTL_SECS};
    use std::sync::atomic::{AtomicU64, Ordering};

    fn isolated_dir() -> PathBuf {
        static COUNTER: AtomicU64 = AtomicU64::new(0);
        let n = COUNTER.fetch_add(1, Ordering::Relaxed);
        let pid = std::process::id();
        let dir = std::env::temp_dir().join(format!("kirin_exclusion_test_{pid}_{n}"));
        let _ = std::fs::remove_dir_all(&dir);
        std::fs::create_dir_all(&dir).unwrap();
        dir
    }

    /// G-115-365: cap 真実源 = O_EXCL 枠存在。N 個の distinct pairing 枠を作る。
    fn make_frames(base: &std::path::Path, ph: &str, n: usize, now: DateTime<Utc>) {
        for i in 0..n {
            reserve_pairing_at(base, ph, &format!("pre-{i:02}"), &format!("post-{i:02}"), now).unwrap();
        }
    }

    #[test]
    fn empty_dir_returns_ok() {
        let base = isolated_dir();
        assert_eq!(check_record_exclusion(&base, "ph"), ExclusionResult::Ok);
    }

    #[test]
    fn missing_project_returns_ok() {
        let base = isolated_dir();
        assert_eq!(check_record_exclusion(&base, "no_such_ph"), ExclusionResult::Ok);
    }

    #[test]
    fn eleven_frames_under_threshold_is_ok() {
        let base = isolated_dir();
        let now = Utc::now();
        make_frames(&base, "ph", 11, now);
        assert_eq!(
            check_record_exclusion(&base, "ph"),
            ExclusionResult::Ok,
            "11 frames < 12 → Ok"
        );
        assert_eq!(count_distinct_pairings(&base, "ph"), 11);
    }

    /// MAX_ACTIVE_PER_PROJECT = 12 pairings ちょうどで Conflict（枠=pairing 単位）。
    #[test]
    fn twelve_frames_conflicts() {
        let base = isolated_dir();
        let now = Utc::now();
        make_frames(&base, "ph", MAX_ACTIVE_PER_PROJECT, now);
        assert!(
            check_record_exclusion(&base, "ph").is_conflict(),
            "{} frames (= {} pairings) → Conflict",
            MAX_ACTIVE_PER_PROJECT,
            MAX_ACTIVE_PER_PROJECT
        );
        assert_eq!(count_distinct_pairings(&base, "ph"), MAX_ACTIVE_PER_PROJECT);
    }

    /// G-115-365 (2)/(a): parse 不能（壊れた/0byte）の枠も「存在」側で数える（under-count しない）。
    /// 11 正常枠 + 1 壊れ枠 = 12 → Conflict（13 本目を reject）。
    #[test]
    fn corrupt_frame_is_counted_as_occupied() {
        let base = isolated_dir();
        let now = Utc::now();
        make_frames(&base, "ph", 11, now);
        let res_dir = base.join("ph").join(RESERVATION_SUBDIR);
        std::fs::create_dir_all(&res_dir).unwrap();
        // 0 バイト = parse 不能（write_all 失敗の残骸相当）。存在するので 1 枠として数える。
        std::fs::write(res_dir.join("pre-X__post-X.json"), b"").unwrap();
        assert_eq!(
            count_distinct_pairings(&base, "ph"),
            12,
            "壊れた枠も存在として数える（parse skip しない）"
        );
        assert!(
            check_record_exclusion(&base, "ph").is_conflict(),
            "12 枠（うち 1 壊れ）→ Conflict（13 本目 reject）"
        );
    }

    /// G-115-365: count は TTL を差し引かない（古い枠も存在すれば数える / live 取得は pure existence）。
    #[test]
    fn old_frame_still_counted_no_ttl_subtraction() {
        let base = isolated_dir();
        let now = Utc::now();
        let old = now - chrono::Duration::seconds(RESERVATION_TTL_SECS + 100);
        for i in 0..11 {
            reserve_pairing_at(&base, "ph", &format!("old-pre-{i}"), &format!("old-post-{i}"), old)
                .unwrap();
        }
        reserve_pairing_at(&base, "ph", "fresh-pre", "fresh-post", now).unwrap();
        assert_eq!(
            count_distinct_pairings(&base, "ph"),
            12,
            "古い枠も TTL で引かず存在として数える（孤児回収は sweep の責務）"
        );
        assert!(check_record_exclusion(&base, "ph").is_conflict());
    }

    /// count は record_reservation/ の枠ファイルのみ数える（record_signal 等の予約 dir は無関係）。
    #[test]
    fn only_reservation_subdir_counted() {
        let base = isolated_dir();
        let now = Utc::now();
        make_frames(&base, "ph", 1, now);
        std::fs::create_dir_all(base.join("ph").join("record_signal")).unwrap();
        std::fs::write(base.join("ph").join("record_signal").join("x.json"), b"{}").unwrap();
        // active marker 相当の instance dir を作っても count には入らない（枠存在のみが真実源）。
        std::fs::create_dir_all(base.join("ph").join("some-iid").join("post")).unwrap();
        std::fs::write(base.join("ph").join("some-iid").join("post").join("m.json"), b"{}").unwrap();
        assert_eq!(
            count_distinct_pairings(&base, "ph"),
            1,
            "record_reservation 内の枠のみが cap 真実源"
        );
    }

    #[test]
    fn different_project_independent() {
        let base = isolated_dir();
        let now = Utc::now();
        make_frames(&base, "ph1", MAX_ACTIVE_PER_PROJECT, now);
        assert!(check_record_exclusion(&base, "ph1").is_conflict());
        assert_eq!(
            check_record_exclusion(&base, "ph2"),
            ExclusionResult::Ok,
            "別 project は独立"
        );
    }

    // ── is_heartbeat_fresh（pub / reservation sweep が marker 鮮度判定に流用）──────────
    #[test]
    fn boundary_60s_is_fresh_61s_is_stale() {
        let now = Utc::now();
        let iso60 = (now - chrono::Duration::seconds(60)).to_rfc3339();
        let iso61 = (now - chrono::Duration::seconds(61)).to_rfc3339();
        assert!(is_heartbeat_fresh(&iso60, now, STALE_SECONDS), "60s = fresh");
        assert!(!is_heartbeat_fresh(&iso61, now, STALE_SECONDS), "61s = stale");
    }

    #[test]
    fn invalid_heartbeat_iso_is_stale() {
        assert!(!is_heartbeat_fresh("not-a-date", Utc::now(), STALE_SECONDS));
    }

    #[test]
    fn future_heartbeat_is_treated_as_fresh() {
        let now = Utc::now();
        let future = (now + chrono::Duration::seconds(10)).to_rfc3339();
        assert!(is_heartbeat_fresh(&future, now, STALE_SECONDS), "未来時刻は安全側 fresh");
    }
}
